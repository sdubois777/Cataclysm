// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmGatekeeperCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmImpCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"

// SEVAROG, FROM THE PARAGON SEVAROG PACK. 85,163 triangles across 155 bones and
// **3.11 metres tall**, which is the design document's "towering" as a
// measurement. Six material slots.
const TCHAR* ACataclysmGatekeeperCharacter::BodyMeshPath =
	TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Meshes/Sevarog.Sevarog");

const TCHAR* ACataclysmGatekeeperCharacter::AnimationFolder =
	TEXT("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations");

// 8.9000 seconds.
const TCHAR* ACataclysmGatekeeperCharacter::IdleAnimationName = TEXT("Idle");

// 9.0000 seconds, and **the only locomotion clip in the pack whose feet move at
// all**. `tools/probe_gatekeeper_foot_bones.py` measured `Walk_Fwd` and
// `Run_Fwd` moving no leg bone by more than 0.03 cm across their whole 1.6
// seconds. See JogPlayRate in the header for why the rate is a placeholder.
const TCHAR* ACataclysmGatekeeperCharacter::JogAnimationName = TEXT("Jog_Fwd");

// 1.1333 seconds into a 0.9714 second wind-up, so a play rate of 1.1667. The
// pack ships three swing chains at four speeds each; this is the shortest that
// is longer than the wind-up, so it fills it without being sped up hard.
const TCHAR* ACataclysmGatekeeperCharacter::CleaveAnimationName =
	TEXT("Swing1_Medium");

// 1.8333 seconds into a 1.2571 second wind-up, so 1.4584. A different clip from
// the sweep, because a lob and a hammer swing should not look the same.
const TCHAR* ACataclysmGatekeeperCharacter::SoulfallAnimationName =
	TEXT("Soul_Siphon");

// 2.8667 seconds, played across its own length because Call the Damned has no
// wind-up to fit: a Summon draws no marker.
const TCHAR* ACataclysmGatekeeperCharacter::CallAnimationName =
	TEXT("Subjugation");

// 2.6333 seconds into a 2.0 second wind-up, so 1.3167. The pack authors this
// ultimate as THREE clips -- a targeting start, a hold loop and this release --
// and only the release is played. Issue #779.
const TCHAR* ACataclysmGatekeeperCharacter::UltimateAnimationName =
	TEXT("Ultimate_Swing_120fps");

// ONE DEATH, THE FEWEST IN THE PROJECT along with the Brute's and the
// Succubus's. Measured 0.9667 seconds, well inside
// UCataclysmEnemyDeath::LongestCorpseSeconds.
const TCHAR* ACataclysmGatekeeperCharacter::DeathAnimationNames
	[DeathAnimationCount] = {
	TEXT("Death_front"),
};

// A HAMMER SWEEP THROUGH A CONE. `Type.AOE.PointBlank` is what makes it area
// damage, which cannot be evaded -- and this creature's target has no evasion
// worth speaking of anyway. `Type.Strike` is the shape.
//
// NO Keyword.CC, unlike the Brute's stomp: Dread Cleave does not stun. The
// design gives this creature no stun at all, and its threat is the size of the
// hit rather than the time it takes away.
const TCHAR* ACataclysmGatekeeperCharacter::CleaveTags =
	TEXT("Type.AOE.PointBlank, Type.Strike");

// THE SAME SHAPE AT FOUR TIMES THE WEIGHT. Identical tags because it is the same
// kind of blow: a full circle at the creature's feet rather than a cone.
const TCHAR* ACataclysmGatekeeperCharacter::SoulHarvestTags =
	TEXT("Type.AOE.PointBlank, Type.Strike");

/**
 * Seconds between the boss's swings, for tuning one while playing.
 */
static TAutoConsoleVariable<float> CVarGatekeeperAttackInterval(
	TEXT("Cataclysm.Gatekeeper.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Gatekeeper's Dread Cleave sweeps. 0 uses its "
		 "designed 3.0. Below about 2.75 the 0.97 second telegraph stops "
		 "fitting inside half the interval, which is the rule that keeps a "
		 "marker walkable."),
	ECVF_Default);

/**
 * Seconds before Soulfall may be lobbed again.
 */
static TAutoConsoleVariable<float> CVarGatekeeperSoulfallCooldown(
	TEXT("Cataclysm.Gatekeeper.SoulfallCooldown"),
	0.0f,
	TEXT("Seconds between the Gatekeeper's Soulfall gouts. 0 uses its designed "
		 "10.0. **The burning ground lasts 10 seconds whatever this says**, so "
		 "a shorter cooldown makes the patches accumulate rather than replace "
		 "one another, and the arena fills up."),
	ECVF_Default);

/**
 * Seconds before Soul Harvest may be used again.
 */
static TAutoConsoleVariable<float> CVarGatekeeperSoulHarvestCooldown(
	TEXT("Cataclysm.Gatekeeper.SoulHarvestCooldown"),
	0.0f,
	TEXT("Seconds between the Gatekeeper's Soul Harvest rings. 0 uses its "
		 "designed 20.0. It is worth 400% of an ordinary hit and kills from "
		 "full health, so a shorter cooldown puts a second one inside almost "
		 "every fight the player is already losing."),
	ECVF_Default);

ACataclysmGatekeeperCharacter::ACataclysmGatekeeperCharacter()
{
	// UpdateLoopingAnimation RUNS FROM Tick and is what returns the mesh to its
	// idle or its walk after a swing.
	PrimaryActorTick.bCanEverTick = true;

	// The row of game/Data/EnemyArchetypes.csv this creature reads.
	ArchetypeRow = TEXT("Gatekeeper");

	// ITS REACH IS DREAD CLEAVE'S RADIUS, which is what the model's
	// `ATTACK_REACH['Gatekeeper']` of 2.0 metres is: the basic attack's own
	// reach. `ACataclysmEnemyController::Think` treats it as the distance at
	// which the creature stops walking and starts attacking.
	MeleeReachCm = DreadCleaveRadiusCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;
	EvasionPercent = DesignedEvasionPercent;
	EnergyShieldFraction = DesignedEnergyShieldFraction;
	NoticeRadiusCm = GatekeeperNoticeRadiusCm;

	// **THE ONLY CREATURE IN THE GAME WITH PHASES.** Highest fraction first;
	// see ACataclysmEnemyCharacter::RefreshPhase, which counts the thresholds
	// at or below the creature's health fraction.
	PhaseHealthFractions = {SecondPhaseHealthFraction, ThirdPhaseHealthFraction};

	GetCapsuleComponent()->InitCapsuleSize(GatekeeperCapsuleRadius,
										   GatekeeperCapsuleHalfHeight);

	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmGatekeeperCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResolveBody(/*bIncludeAnimation=*/true);
	UpdateLoopingAnimation();
}

void ACataclysmGatekeeperCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// BEFORE THE LOOPING ANIMATION, so a sweep that becomes due this frame owns
	// the mesh for the rest of it rather than being overwritten by an idle that
	// was chosen a line earlier.
	StartPendingWindUpClip();
	UpdateLoopingAnimation();
}

float ACataclysmGatekeeperCharacter::CleavePlayRate()
{
	return StrikeAlignedPlayRate(CleaveStrikeSeconds, DreadCleaveWindUpSeconds,
								 MinimumPlayRate, MaximumPlayRate);
}

float ACataclysmGatekeeperCharacter::CleaveDelaySeconds()
{
	return StrikeAlignedDelaySeconds(CleaveStrikeSeconds,
									 DreadCleaveWindUpSeconds,
									 MinimumPlayRate, MaximumPlayRate);
}

void ACataclysmGatekeeperCharacter::StartPendingWindUpClip()
{
	if (PendingWindUpAbility == INDEX_NONE)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// **THE WIND-UP HAS TO STILL BE HAPPENING.** A cancelled one -- the target
	// lost, the creature stunned -- must not have its clip start afterwards.
	// Checked against the brain rather than remembered here, so there is one
	// answer to "is this creature still winding that up".
	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(GetController());
	if (!Brain || Brain->WindingUpAbility != PendingWindUpAbility)
	{
		PendingWindUpAbility = INDEX_NONE;
		return;
	}

	// COMPARED AGAINST THE CLOCK EVERY FRAME RATHER THAN SET AS A DEADLINE. A
	// timer fixes its deadline when it is created, which is how a held clip on
	// the Brute came to fire on the wrong side of the pass that landed its
	// ability. This cannot be out of order with anything.
	const float Elapsed = World->GetTimeSeconds() - WindUpBeganAtSeconds;
	if (Elapsed + UE_KINDA_SMALL_NUMBER < CleaveDelaySeconds())
	{
		return;
	}

	PendingWindUpAbility = INDEX_NONE;
	PlayOneShotAtRate(CleaveAnimation.Get(), CleavePlayRate());
}

float ACataclysmGatekeeperCharacter::AttackIntervalSecondsInUse()
{
	const float Override = CVarGatekeeperAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : DesignedAttackIntervalSeconds;
}

float ACataclysmGatekeeperCharacter::SoulfallCooldownSecondsInUse()
{
	const float Override = CVarGatekeeperSoulfallCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : SoulfallCooldownSeconds;
}

float ACataclysmGatekeeperCharacter::SoulHarvestCooldownSecondsInUse()
{
	const float Override =
		CVarGatekeeperSoulHarvestCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : SoulHarvestCooldownSeconds;
}

float ACataclysmGatekeeperCharacter::DesignedSecondsBetweenAttacks() const
{
	// **THIS ONE, NOT `SecondsBetweenAttacks`**, which is `final` on the enemy
	// base and divides this by whatever the creature's buffs are worth. A
	// creature overriding the other one ignores every buff in the game without
	// a word, which is why the base makes it a compile error.
	return AttackIntervalSecondsInUse();
}

void ACataclysmGatekeeperCharacter::AttackTarget(AActor* /*Target*/)
{
	// NOTHING, AND THAT IS THE WHOLE POINT OF THIS OVERRIDE.
	//
	// The base class's AttackTarget applies direct damage at `MeleeReachCm`, and
	// this creature's reach is Dread Cleave's own radius. Left to the base it
	// would deal a free single-target hit at two metres every three seconds on
	// top of the sweep it already made -- and at a damage share of 2.10 that is
	// the largest unexplained number in the game.
	//
	// ITS BASIC ATTACK IS Dread Cleave, which is an entry in EnemyAbilities
	// because it is telegraphed. The Corrupted Sentinel found this first and the
	// Succubus is the second creature with the same shape.
}

TArray<FCataclysmEnemyAbility>
ACataclysmGatekeeperCharacter::EnemyAbilities() const
{
	// --- Soul Harvest, phase 3 --------------------------------------------
	FCataclysmEnemyAbility Harvest;
	Harvest.Name = TEXT("Soul Harvest");
	Harvest.Phase = SoulHarvestPhase;

	// A RING AT ITS OWN FEET, so there is no minimum and the maximum is how far
	// the ring reaches. A player further out than the ring is a player the ring
	// cannot touch, and choosing it then would waste the cooldown.
	Harvest.MinRangeCm = 0.0f;
	Harvest.MaxRangeCm = SoulHarvestRadiusCm;
	Harvest.CooldownSeconds = SoulHarvestCooldownSecondsInUse();
	Harvest.WindUpSeconds = SoulHarvestWindUpSeconds;
	Harvest.Shape = ECataclysmSkillShape::Strike;
	Harvest.bArcsOntoItsTarget = false;
	Harvest.MarkerRadiusCm = SoulHarvestRadiusCm;

	// --- Call the Damned, phase 2 -----------------------------------------
	FCataclysmEnemyAbility Call;
	Call.Name = TEXT("Call the Damned");
	Call.Phase = CallTheDamnedPhase;
	Call.MinRangeCm = 0.0f;

	// AS FAR AS THE IMPS APPEAR FROM IT. Beyond that the summons would arrive
	// behind a player who has already walked away, which is the adds arriving
	// where the fight is not.
	Call.MaxRangeCm = CallTheDamnedRangeCm;
	Call.CooldownSeconds = CallTheDamnedCooldownSeconds;
	Call.WindUpSeconds = CallTheDamnedWindUpSeconds;

	// A SUMMON DRAWS NO MARKER, which
	// `ACataclysmEnemyController::ShowWindUpMarker` already handles: its default
	// arm draws nothing for Summon and says so.
	Call.Shape = ECataclysmSkillShape::Summon;
	Call.bArcsOntoItsTarget = false;
	Call.MarkerRadiusCm = 0.0f;

	// --- Soulfall, phase 1 -------------------------------------------------
	FCataclysmEnemyAbility Soulfall;
	Soulfall.Name = TEXT("Soulfall");
	Soulfall.Phase = 1;

	// A MINIMUM, BECAUSE IT MARKS A CIRCLE WHERE IT LANDS. Below `radius + own
	// body` that circle covers the creature's own feet, which is a melee attack
	// wearing a thrown attack's telegraph. Issue #475 on the Brute.
	Soulfall.MinRangeCm = SoulfallMinimumRangeCm;
	Soulfall.MaxRangeCm = SoulfallRangeCm;
	Soulfall.CooldownSeconds = SoulfallCooldownSecondsInUse();
	Soulfall.WindUpSeconds = SoulfallWindUpSeconds;

	// A CIRCLE WHERE IT LANDS, NOT A LANE ALONG THE WAY, because it is lobbed.
	// The gout rises over everything between and endangers only the ground it
	// comes down on. Issue #459.
	Soulfall.Shape = ECataclysmSkillShape::Projectile;
	Soulfall.bArcsOntoItsTarget = true;
	Soulfall.MarkerRadiusCm = SoulfallRadiusCm;

	// --- Dread Cleave, phase 1, and LAST ----------------------------------
	FCataclysmEnemyAbility Cleave;
	Cleave.Name = TEXT("Dread Cleave");
	Cleave.Phase = 1;
	Cleave.MinRangeCm = 0.0f;
	Cleave.MaxRangeCm = DreadCleaveRadiusCm;

	// ZERO, WHICH IS WHAT MAKES IT THE BASIC ATTACK. The creature's own 3.0
	// second attack interval is the only thing spacing it out.
	Cleave.CooldownSeconds = DreadCleaveCooldownSeconds;
	Cleave.WindUpSeconds = DreadCleaveWindUpSeconds;
	Cleave.Shape = ECataclysmSkillShape::Strike;
	Cleave.bArcsOntoItsTarget = false;
	Cleave.MarkerRadiusCm = DreadCleaveRadiusCm;

	// ORDER IS PRIORITY, AND DREAD CLEAVE HAS TO COME LAST. See the enumeration
	// in the header: `ChooseAbility` takes the first entry whose phase, range
	// and cooldown fit, and a zero-cooldown ability at the front is the only
	// thing the creature would ever do. Issue #491.
	//
	// AND THE OTHER THREE DESCEND BY WEIGHT, so the creature reaches for the
	// biggest thing it may use. In phase 1 the first two are skipped by phase
	// and the creature lobs or sweeps; in phase 3 everything is available and
	// the ring wins whenever it is off cooldown and the player is inside it.
	return {Harvest, Call, Soulfall, Cleave};
}

void ACataclysmGatekeeperCharacter::BeginEnemyAbilityWindUp(int32 Index, AActor*)
{
	// CALL THE DAMNED HAS NO WIND-UP, so it never reaches here: the controller
	// only calls this when there is one to begin, and a zero-wind-up ability
	// goes straight to UseEnemyAbility.
	//
	// THE CLIP RUNS ACROSS THE WIND-UP, NOT ACROSS THE INTERVAL. The clip IS the
	// wind-up: the blow lands as the telegraph ends, so the swing should end
	// there too. The Corrupted Sentinel is the one creature that does this
	// differently and its reason is particular to it.
	// **THE SWEEP WAITS. IT DOES NOT START HERE.** Its clip strikes 0.282
	// seconds in and the blow lands at 0.9714, so starting it now would put the
	// hammer through the player 0.73 seconds before the damage. Issue #784.
	// StartPendingWindUpClip, from Tick, starts it once the delay has passed.
	//
	// THE OTHER TWO STILL START AT ONCE, and that is not an oversight: their
	// strike moments have not been measured. Issue #526 measured the ordinary
	// attack of all seven creatures and nothing else.
	PendingWindUpAbility = INDEX_NONE;

	switch (Index)
	{
	case DreadCleaveAbility:
		if (const UWorld* World = GetWorld())
		{
			WindUpBeganAtSeconds = World->GetTimeSeconds();
			if (CleaveDelaySeconds() > 0.0f)
			{
				PendingWindUpAbility = DreadCleaveAbility;
				return;
			}
		}
		PlayOneShotAtRate(CleaveAnimation.Get(), CleavePlayRate());
		return;

	case SoulfallAbility:
		PlayOneShot(SoulfallAnimation.Get(), SoulfallWindUpSeconds);
		return;

	case SoulHarvestAbility:
		// THE PACK AUTHORS THIS AS THREE CLIPS and only the release is played.
		// Issue #779.
		PlayOneShot(UltimateAnimation.Get(), SoulHarvestWindUpSeconds);
		return;

	default:
		return;
	}
}

void ACataclysmGatekeeperCharacter::UseEnemyAbility(
	int32 Index, AActor* Target, const FVector& AimedAt)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (Index == DreadCleaveAbility)
	{
		StrikeAround(DreadCleaveRadiusCm, DreadCleaveAngleDegrees,
					 DreadCleaveDamagePercent);
		return;
	}

	if (Index == SoulHarvestAbility)
	{
		StrikeAround(SoulHarvestRadiusCm, SoulHarvestAngleDegrees,
					 SoulHarvestDamagePercent);
		return;
	}

	if (Index == SoulfallAbility)
	{
		// FROM THE MIDDLE OF THE CREATURE, WHICH IS A KNOWN APPROXIMATION, and
		// a coarser one here than anywhere: this creature's middle is 1.55
		// metres off the ground. The Brute fires from the bone its rock hangs
		// off because issue #454 found a rock appearing at its waist while the
		// animation threw it overhead; the same question is open here.
		const FVector From = GetActorLocation();

		// NO TAGS, WHICH IS WHAT EVERY ENEMY PROJECTILE PASSES TODAY. Tags scope
		// the caster's own stat modifiers, and an enemy carries none.
		const FGameplayTagContainer NoTags;

		// A FLIGHT TIME AND NO SPEED AT ALL, so it lobs. A ballistic shot has no
		// single speed -- slowest at the top of its arc, fastest as it lands --
		// so the projectile is told how long it has. Issue #465. The zero is not
		// a beam: a projectile is a beam when it is given NEITHER.
		LastGoutLobbed = ACataclysmProjectile::Fire(
			this, From, AimedAt, SoulfallRadiusCm, /*InSpeed=*/0.0f,
			SoulfallPierce, /*bInReturns=*/false, SoulfallDamagePercent,
			NoTags, /*bInBurns=*/true, /*InBodyMesh=*/nullptr,
			SoulfallFlightSecondsFor(AimedAt));

		// AND THE GROUND IT LANDS ON KEEPS BURNING. This is what the ability is
		// for: the arena shrinks by one circle per cycle, and because the ground
		// lasts exactly the cooldown, one patch is always down.
		//
		// PRICED ONCE, WHEN THE FIRE IS LAID, off this creature's own attack
		// damage. A patch outlives the ability that left it, so reading the
		// creature's damage on every tick would make a patch keep paying for a
		// buff that has since expired. The same rule the Hellhound's lane and
		// the player's ground effects follow.
		const float PerSecond =
			UCataclysmSkillEffects::WeaponDamageOf(
				UCataclysmTargeting::AbilitySystemOf(this))
			* SoulfallGroundPercent / 100.0f;

		// **IT BURNS THE PLAYER AND NOBODY ON THIS CREATURE'S OWN SIDE.** The
		// design gave it `GroundHitsAllies=1` so that the summoned Imps of phase
		// 2 burned in it, and on 2026-08-20 the project owner set a general rule
		// that a creature does not burn itself or its allies. So this is the
		// ordinary kind of ground zone, which is what `Spawn` makes: the one
		// that knows whose side it is on.
		//
		// AT THE PLACE IT WAS MARKED, not where the target is now. A player who
		// stepped out of the circle is not caught by the burst and does not
		// stand in the fire either, which is the whole of what a telegraph buys.
		LastGroundLeftBurning = ACataclysmGroundZone::Spawn(
			this, AimedAt, SoulfallGroundRadiusCm, SoulfallGroundSeconds,
			PerSecond);
		return;
	}

	if (Index == CallTheDamnedAbility)
	{
		// THE CLIP PLAYS HERE RATHER THAN IN THE WIND-UP HOOK, because there is
		// no wind-up: a Summon draws no marker, so there is no window to fill.
		PlayOneShot(CallAnimation.Get());

		// THE CAP COUNTS WHAT IS ALIVE, not what was ever summoned. Dead Imps
		// are forgotten first, which is what lets killing them buy the player
		// the ten seconds until the next cast.
		const int32 Alive = ImpsStillAlive();
		const int32 Room = CallTheDamnedMaxAlive - Alive;
		if (Room <= 0)
		{
			// FULL. The cooldown is still stamped by the controller, which is
			// right: the creature spent its cast and got nothing, and a player
			// who keeps the field full has earned that.
			UE_LOG(LogCataclysm, Verbose,
				TEXT("%s called the damned with %d already up, at a cap of %d, "
					 "so none arrived."),
				*GetNameSafe(this), Alive, CallTheDamnedMaxAlive);
			return;
		}

		const int32 Arriving = FMath::Min(CallTheDamnedCount, Room);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// ITS OWN STREAM, seeded from this creature and the moment it cast, so
		// two casts do not put the Imps in the same places. The same shape the
		// Imp's own swipe draw and the death draw use, salted differently so it
		// is not the stream either of those runs on.
		constexpr int32 CallDrawSalt = 0x1D0FF;
		FRandomStream Stream(GetUniqueID()
			^ static_cast<int32>(World->GetTimeSeconds() * 1000.0f)
			^ CallDrawSalt);

		for (int32 Index2 = 0; Index2 < Arriving; ++Index2)
		{
			// WITHIN `Range` OF THE CREATURE, AND OUTSIDE ITS OWN BODY. The
			// design says they claw out of the ground within 4 metres; the near
			// bound is this creature's capsule so an Imp does not arrive inside
			// it and get pushed out.
			const float Angle = Stream.FRandRange(0.0f, 2.0f * PI);
			const float Distance = Stream.FRandRange(
				GatekeeperCapsuleRadius + CallTheDamnedRadiusCm,
				CallTheDamnedRangeCm);

			const FVector Where = GetActorLocation()
				+ FVector(FMath::Cos(Angle) * Distance,
						  FMath::Sin(Angle) * Distance,
						  0.0f);

			ACataclysmImpCharacter* Imp =
				World->SpawnActor<ACataclysmImpCharacter>(
					ACataclysmImpCharacter::StaticClass(), Where,
					FRotator::ZeroRotator, SpawnParams);
			if (!Imp)
			{
				continue;
			}

			CalledImps.Add(Imp);
		}

		UE_LOG(LogCataclysm, Verbose,
			TEXT("%s called %d of the damned; %d are now up, at a cap of %d."),
			*GetNameSafe(this), Arriving, ImpsStillAlive(),
			CallTheDamnedMaxAlive);
		return;
	}
}

void ACataclysmGatekeeperCharacter::StrikeAround(float RadiusCm,
												 float AngleDegrees,
												 float DamagePercent)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// PARSED ONCE RATHER THAN PER TARGET, the same as the Brute's stomp: a sweep
	// this size catches a crowd.
	const FGameplayTagContainer AbilityTags =
		UCataclysmSkillShapes::TagsFromCell(
			AngleDegrees >= 360.0f ? SoulHarvestTags : CleaveTags);

	// A FULL CIRCLE IS A SPHERE SEARCH AND A CONE IS A CONE SEARCH, and the two
	// are different functions rather than one with an angle of 360. Asking for a
	// 360 degree cone works out to the same set, but the sphere search is what
	// the Brute's stomp already uses for a ring and doing it the same way keeps
	// one shape per question.
	const TArray<AActor*> Caught = AngleDegrees >= 360.0f
		? UCataclysmTargeting::FindEnemiesInSphere(
			World, this, GetActorLocation(), RadiusCm)
		: UCataclysmTargeting::FindEnemiesInCone(
			World, this, GetActorLocation(), GetActorForwardVector(),
			RadiusCm, AngleDegrees);

	for (AActor* Hit : Caught)
	{
		// AREA DAMAGE, so it cannot be evaded. Said by its tags rather than by
		// this call, since issue #519: the `Type.AOE.PointBlank` in the tag
		// string is what ApplyHit reads, which is the same route a player skill
		// takes.
		UCataclysmSkillEffects::ApplyHit(this, Hit, DamagePercent, AbilityTags);
	}
}

int32 ACataclysmGatekeeperCharacter::ImpsStillAlive()
{
	// THE DEAD ARE FORGOTTEN HERE rather than when they die, because nothing
	// tells this creature that one of its Imps has gone. A weak pointer that has
	// been collected reads as null, and one that is still valid may still be a
	// corpse waiting for its death clip -- `IsDead` is what separates those.
	CalledImps.RemoveAll([](const TWeakObjectPtr<ACataclysmImpCharacter>& Imp)
	{
		const ACataclysmImpCharacter* Alive = Imp.Get();
		return !Alive || UCataclysmSkillEffects::IsDead(Alive);
	});

	return CalledImps.Num();
}

float ACataclysmGatekeeperCharacter::SoulfallFlightSecondsFor(
	const FVector& LandsAt) const
{
	// A PARABOLA SAGS g * t * t / 8 BELOW ITS OWN CHORD, so a sag of
	// `fraction * range` is in the air for `sqrt(8 * fraction * range / g)`.
	// Inverting it here rather than stating a time is issue #474 on the Brute: a
	// stated time fixes the whole vertical part of the trajectory whatever the
	// distance, which made every short lob a near-vertical mortar.
	//
	// MEASURED ACROSS THE GROUND, because that is what the fraction is of.
	const float ApexCm = SoulfallApexFraction
					   * static_cast<float>(FVector::Dist2D(GetActorLocation(),
															LandsAt));
	if (ApexCm <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Sqrt(
		8.0f * ApexCm / ACataclysmProjectile::LobGravityCmPerSecondSquared);
}

void ACataclysmGatekeeperCharacter::UpdateLoopingAnimation()
{
	// THE GRAPH OWNS THE MESH WHEN THERE IS ONE. Two things setting the same
	// component's animation would fight, and the graph is the better of the two
	// because it can blend. This whole function is the fallback for not having
	// one.
	if (bAnimationBlueprintBound)
	{
		return;
	}

	const UWorld* World = GetWorld();
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!World || !MeshComponent)
	{
		return;
	}

	// A ONE-SHOT KEEPS THE MESH UNTIL IT ENDS. Its end is recorded rather than
	// asked of the component, because a single-node instance reports its own
	// position and not whether the caller considers it finished.
	if (World->GetTimeSeconds() < OneShotEndsAtSeconds)
	{
		return;
	}

	const bool bWalking = GetVelocity().Size2D() > WalkingThresholdCmPerSecond;
	UAnimSequence* Wanted = bWalking ? JogAnimation.Get() : IdleAnimation.Get();

	if (!Wanted)
	{
		return;
	}

	// ONLY ON A CHANGE. PlayAnimation restarts the clip from the beginning, so
	// calling it every frame would freeze the creature on the first pose of
	// whichever loop it is in.
	if (Wanted == CurrentLoopingAnimation)
	{
		return;
	}

	CurrentLoopingAnimation = Wanted;
	MeshComponent->PlayAnimation(Wanted, /*bLooping=*/true);

	// **THE WALK RATE IS A PLACEHOLDER AND NOT A DERIVATION.** See JogPlayRate
	// in the header: the stride measurement cannot read this rig. Issue #778.
	MeshComponent->SetPlayRate(bWalking ? JogPlayRate : 1.0f);
}

float ACataclysmGatekeeperCharacter::PlayOneShot(UAnimSequence* Animation,
												 float HoldSeconds)
{
	const float Length = Animation ? Animation->GetPlayLength() : 0.0f;

	// NEVER SLOWER THAN AUTHORED, ONLY FASTER, AND ONLY WHEN IT MUST BE. The
	// same rule every other creature here uses: stretching a short clip across a
	// long window was tried and read as slow motion. A clip shorter than its
	// window holds its last pose instead.
	//
	// THIS LINES UP THE CLIP'S END WITH THE WINDOW'S END, which is right for a
	// clip that finishes on its blow. The sweep does not -- it strikes a quarter
	// of the way in -- so it is played through PlayOneShotAtRate instead.
	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;
	const float Rate = Hold > 0.0f
		? FMath::Clamp(FMath::Max(1.0f, Length / Hold),
					   MinimumPlayRate, MaximumPlayRate)
		: 1.0f;

	return PlayOneShotAtRate(Animation, Rate);
}

float ACataclysmGatekeeperCharacter::PlayOneShotAtRate(UAnimSequence* Animation,
													   float Rate)
{
	LastPlayedAnimation = Animation;

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!Animation || !MeshComponent)
	{
		return 0.0f;
	}

	const float Length = Animation->GetPlayLength();
	if (Length <= 0.0f || Rate <= 0.0f)
	{
		return 0.0f;
	}

	MeshComponent->PlayAnimation(Animation, /*bLooping=*/false);
	MeshComponent->SetPlayRate(Rate);

	// AND THE MESH IS OWED BACK WHEN IT FINISHES. A one-shot in single-node mode
	// plays once and then HOLDS ITS LAST FRAME forever.
	if (const UWorld* World = GetWorld())
	{
		OneShotEndsAtSeconds = World->GetTimeSeconds() + Length / Rate;
	}

	// CLEARED SO THE LOOP RESTARTS AFTERWARDS. UpdateLoopingAnimation only acts
	// on a change, and without this the creature would still be "already
	// playing" the loop the swing interrupted, so nothing would put it back.
	CurrentLoopingAnimation = nullptr;

	return Length / Rate;
}

bool ACataclysmGatekeeperCharacter::ResolveBody(bool bIncludeAnimation)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return false;
	}

	USkeletalMesh* Body =
		Cast<USkeletalMesh>(FSoftObjectPath(BodyMeshPath).TryLoad());
	if (!Body)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Gatekeeper art not found at %s, so it is keeping the "
				 "placeholder cylinder. This is expected without the Paragon "
				 "Sevarog pack; see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM, AND THE ENGINE'S YAW. A skeletal mesh is
	// authored with its origin at the feet and the capsule's origin is its
	// centre, so the mesh drops by the half-height -- which for this creature is
	// 1.55 metres, by far the largest drop in the project. The -90 degree yaw is
	// the engine's convention for character meshes, which face -Y while the
	// actor faces +X.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -GatekeeperCapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	if (bIncludeAnimation)
	{
		IdleAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, IdleAnimationName))
				.TryLoad());
		JogAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, JogAnimationName))
				.TryLoad());
		CleaveAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, CleaveAnimationName))
				.TryLoad());
		SoulfallAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, SoulfallAnimationName))
				.TryLoad());
		CallAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, CallAnimationName))
				.TryLoad());
		UltimateAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, UltimateAnimationName))
				.TryLoad());

		// A NULL ENTRY IS KEPT rather than skipped, for the reason
		// ACataclysmEnemyCharacter::PlayDeathAnimation gives: dropping one would
		// change how many clips there are and therefore which one is drawn.
		// There is only one here, so today that changes nothing.
		DeathAnimations.Reset();
		for (const TCHAR* Name : DeathAnimationNames)
		{
			DeathAnimations.Add(Cast<UAnimSequence>(
				FSoftObjectPath(ClipPathIn(AnimationFolder, Name)).TryLoad()));
		}

		if (!IdleAnimation || !JogAnimation || !CleaveAnimation
			|| !SoulfallAnimation || !CallAnimation || !UltimateAnimation)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Gatekeeper animations missing: idle %s, walk %s, sweep "
					 "%s, gout %s, summon %s, ultimate %s. It will fight with "
					 "nothing to show for it. This is expected without the "
					 "Paragon Sevarog pack."),
				IdleAnimation ? TEXT("found") : TEXT("MISSING"),
				JogAnimation ? TEXT("found") : TEXT("MISSING"),
				CleaveAnimation ? TEXT("found") : TEXT("MISSING"),
				SoulfallAnimation ? TEXT("found") : TEXT("MISSING"),
				CallAnimation ? TEXT("found") : TEXT("MISSING"),
				UltimateAnimation ? TEXT("found") : TEXT("MISSING"));
		}
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE CREATURE.
	// `ACataclysmEnemyCharacter` creates PlaceholderBody in its constructor and
	// nothing about assigning a skeletal mesh removes it.
	// `test_every_dressed_enemy_hides_its_placeholder` refuses a dressed enemy
	// that does not do this.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}

	UE_LOG(LogCataclysm, Verbose, TEXT("Gatekeeper is wearing %s."),
		   BodyMeshPath);
	return true;
}
