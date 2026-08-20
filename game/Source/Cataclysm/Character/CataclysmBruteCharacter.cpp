// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmBruteCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmDebrisBurst.h"
#include "AbilitySystem/CataclysmMeshWidth.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
// For turning an ability's tag list into a container, the same way a
// player's skill row is read. Issue #519.
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Character/CataclysmEnemyController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/SoftObjectPath.h"

const TCHAR* ACataclysmBruteCharacter::BodyMeshPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rampage.Rampage");

const TCHAR* ACataclysmBruteCharacter::AnimationBlueprintPath =
	TEXT("/Game/Enemies/Demonic/Brute/ABP_Brute.ABP_Brute_C");

// The rock, out of the pack's own folder. Its material M_Rock_To_Throw comes
// with it without being named here: a static mesh carries its material slots.
const TCHAR* ACataclysmBruteCharacter::RockMeshPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Meshes/Rocks/"
		 "SM_Rock_To_Hold.SM_Rock_To_Hold");

// The hole the rock came out of. In the FX folder rather than beside the rock,
// which is why it is a separate path rather than a sibling of the one above.
const TCHAR* ACataclysmBruteCharacter::RockCraterMeshPath =
	TEXT("/Game/ParagonRampage/FX/Meshes/Debris/"
		 "SM_Rampage_Rock_Rip_Crater.SM_Rampage_Rock_Rip_Crater");

const FName ACataclysmBruteCharacter::AttackSlotName = TEXT("DefaultSlot");

// A BONE, NOT A SOCKET. The Rampage mesh has no sockets at all -- find_socket
// answers null for every name -- so the rock hangs off a bone.
//
// THE HAND, NOT THE PROP BONE. This was weapon_r until issue #470. The animator
// drives weapon_r as the ROCK rather than as the hand, and during the throw
// flings it up to 1253 cm from the creature, so a rock launched from it left
// nearly seven metres in front of the Brute. Measured by
// tools/measure_rock_launch_point.py. See the header for the figures.
const FName ACataclysmBruteCharacter::RockHoldBoneName = TEXT("hand_r");

// THE TWO ABILITY MONTAGES LIVE BESIDE THE ANIMATION BLUEPRINT, NOT IN THE
// PARAGON FOLDER. They are this project's own assets, built by
// tools/generate_brute_montages.py out of the pack's clips, so they belong under
// game/Content/Enemies/<Family>/<Enemy>/ by the convention in
// game/docs/content-layout.md. That also means they are committed, unlike the
// clips inside them, which the .gitignore excludes.
const TCHAR* ACataclysmBruteCharacter::StompMontagePath =
	TEXT("/Game/Enemies/Demonic/Brute/AM_Brute_Stomp.AM_Brute_Stomp");

const TCHAR* ACataclysmBruteCharacter::RockThrowMontagePath =
	TEXT("/Game/Enemies/Demonic/Brute/AM_Brute_RockThrow.AM_Brute_RockThrow");

const TCHAR* ACataclysmBruteCharacter::AttackAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
		 "Attack_Biped_Melee_A.Attack_Biped_Melee_A");

// THE ONE CLIP THIS CREATURE DIES WITH. Measured in the editor on 2026-08-19
// at 0.7667 seconds. The pack ships no second death clip.
const TCHAR* ACataclysmBruteCharacter::DeathAnimationPath =
	TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage/Animations/"
		 "Death_A.Death_A");

/**
 * Live override for the chase speed, for judging it by eye.
 *
 * THE DESIGNED FIGURE IS NOW 500 AND LIVES IN THE MODEL, as chase_speed on
 * ARCHETYPES["Brute"] in sim/cataclysm_sim/enemy_stats.py, so this is a tuning
 * aid rather than the source of the number. It started as the only way to try a
 * second speed at all, because the design had none for any enemy; playing it on
 * 2026-08-07 settled 500 and that went into the model.
 *
 * Zero means use the designed figure, so clearing it restores the design.
 */
static TAutoConsoleVariable<float> CVarBruteChaseSpeed(
	TEXT("Cataclysm.Brute.ChaseSpeed"),
	0.0f,
	TEXT("Centimetres per second the Brute moves while chasing. 0 uses its "
		 "designed 500. It wanders at 250 either way. The player now walks at "
		 "400, so the designed 500 is faster than the player and this creature "
		 "cannot be walked away from. Issue #417 is where that gets re-judged. "
		 "Anything at or below 375 also stops the four-legged chase animation "
		 "playing."),
	ECVF_Default);

/**
 * Live override for the seconds between swings, for finding the right pace.
 *
 * WHY IT IS WANTED. 2.8 seconds is the designed interval and the project owner
 * reported it on 2026-08-07 as "a pretty long delay", with the reasoning that an
 * enemy that is not attacking might as well be scenery. That is a balance
 * judgement about how the game feels, which is exactly the kind of number that
 * has to be found by playing rather than derived.
 *
 * IT DOES NOT TOUCH THE STOMP, which was the obvious worry and is not one. An
 * ability with a cooldown is telegraphed against that cooldown rather than the
 * attack interval, which section X of docs/Cataclysm_GDD_v2.md states and
 * Ability.cycle_seconds implements. The Stomp runs on its 8 second cooldown.
 *
 * WHAT IT DOES SIZE is the marker the ordinary swing could draw, which is
 * nothing: the Slam's 0.9 m radius is under the one metre floor at any interval.
 *
 * DO NOT GO BELOW 1.0. Attack_Biped_Melee_A, the swing clip, is 1.0000 seconds
 * long and PlayAttackAnimation passes no window, so it runs at its authored
 * speed. Under a second the creature starts a swing it has not finished.
 *
 * Zero means use the designed interval, which is 1.2 as of 2026-08-09.
 */
static TAutoConsoleVariable<float> CVarBruteAttackInterval(
	TEXT("Cataclysm.Brute.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Brute's swings. 0 uses its designed 1.2. Do NOT "
		 "go below 1.0: the swing clip is exactly a second long and is not "
		 "rate-scaled, so a shorter interval starts a swing that has not "
		 "finished. This does NOT affect the Stomp, which is telegraphed "
		 "against its own 8 second cooldown rather than the attack interval."),
	ECVF_Default);

// THE TWO COOLDOWNS AND THE ATTACK INTERVAL TOGETHER DECIDE HOW OFTEN THE
// CREATURE USES AN ABILITY RATHER THAN SWINGING, which is what the project owner
// reported on 2026-08-08 as "he basically uses an ability, waits a second,
// attacks once, uses another ability, waits a second, attacks once". Issue #452.
//
// THE ARITHMETIC, so a figure can be aimed at rather than guessed. Every attack
// is gated by the attack interval, and an ability that lands spends an interval
// slot exactly as a swing does. With an interval I and abilities on cooldowns C1
// and C2, the creature starts 1/C1 + 1/C2 abilities and 1/I attacks of any kind
// per second, so the ordinary swings between abilities are
//
//     (1/I - 1/C1 - 1/C2) / (1/C1 + 1/C2)
//
//     stomp 5, throw 5, interval 1.6   ->  0.56   (what was reported)
//     stomp 10, throw 10, interval 1.6 ->  2.12
//     stomp 8, throw 12, interval 1.2  ->  3.00   (settled by play 2026-08-09)
//
// THE TWO COOLDOWNS ARE NO LONGER EQUAL, WHICH CHANGES THE SHAPE AS WELL AS THE
// COUNT. At 5 and 5 both abilities came up together and the creature used them
// as a pair, which is why the original report describes a pair. At 8 and 12 they
// drift in and out of phase over a 24 second cycle, three stomps to every two
// throws, so what the player meets is not a repeating pair.
//
// RAISING THEM IS LEGAL; LOWERING THEM IS NOT. Both designed figures are floors
// rather than targets, and both floors now sit below the figures in use. The
// stomp must stay at or above the 5 second stun immunity window, because
// sim/cataclysm_sim/enemy_abilities.py records that stomping faster than that
// spends stomps on a target that cannot be stunned. The rock throw must stay
// above the approach time: the Brute crosses its own throwing range in 2
// seconds, so a shorter cooldown lets it throw twice per approach and it reads
// as a ranged enemy rather than a bruiser with a rock.
static TAutoConsoleVariable<float> CVarBruteStompCooldown(
	TEXT("Cataclysm.Brute.StompCooldown"),
	0.0f,
	TEXT("Seconds before the Brute may stomp again. 0 uses its designed 8. "
		 "Raising it makes the creature swing more often between abilities. "
		 "With the throw at 12 and the interval at 1.2, three ordinary swings "
		 "fall between abilities. Do NOT go below 5: that figure is the stun "
		 "immunity window, and a faster stomp spends itself on a player who "
		 "cannot be stunned yet."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarBruteRockThrowCooldown(
	TEXT("Cataclysm.Brute.RockThrowCooldown"),
	0.0f,
	TEXT("Seconds before the Brute may throw again. 0 uses its designed 12. "
		 "Do NOT go below 5: the creature crosses its own 10 metre throwing "
		 "range in 2 seconds, so a shorter cooldown lets it throw twice per "
		 "approach and it stops reading as a melee bruiser."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarBruteRockArc(
	TEXT("Cataclysm.Brute.RockArc"),
	0.0f,
	TEXT("How high the thrown rock rises above the straight line to where it "
		 "lands, as a fraction of the distance thrown. 0 uses the designed "
		 "0.25, which is what a projectile launched at 45 degrees reaches, so "
		 "it is a real trajectory rather than a chosen one. THIS MOVES THE "
		 "FLIGHT TIME TOO, and the two cannot be set apart: the rock falls "
		 "under real gravity, so an arc of f times the range takes "
		 "sqrt(8*f*range/980) seconds. At 0.25 a ten metre throw is in the air "
		 "1.43 seconds and a three metre one 0.78. Higher is a slower, loopier, "
		 "taller lob; below about 0.05 it is a flat throw again. Do NOT go far "
		 "above 0.3: the rock lands faster the longer it has been falling, and "
		 "it must not outrun the 1200 centimetres per second of the slowest "
		 "projectile any player skill uses."),
	ECVF_Default);

// A POINT-BLANK AREA THAT HOLDS WHAT IT CATCHES, which is exactly what the
// designed player skill Shockwave Leap carries. Issue #519.
const TCHAR* ACataclysmBruteCharacter::StompTags =
	TEXT("Type.AOE.PointBlank, Type.Strike, Keyword.CC");

// A THROWN ROCK. Emberfang and Hurl Cinders carry Type.Projectile; the
// Type.Ranged is this one's own, because the ability exists so that standing
// outside the Brute's reach is not free.
const TCHAR* ACataclysmBruteCharacter::RockThrowTags =
	TEXT("Type.Projectile, Type.Ranged");

ACataclysmBruteCharacter::ACataclysmBruteCharacter()
{
	// TICKS, UNLIKE EVERY OTHER CHARACTER IN THIS PROJECT, for two reasons.
	//
	// The walk speed has to follow what the brain is doing, and the brain runs
	// on its own quarter-second timer while movement is read every frame.
	//
	// And an ability montage has to start part way through its own telegraph,
	// which means comparing the clock against the brain's state every frame.
	// That job was done by timers until 2026-08-08, and doing it by deadline is
	// what let a held clip outlive the ability it belonged to; see
	// UpdateAbilityMontage.
	//
	// Choosing which locomotion clip to play was a third reason once, and is
	// now ABP_Brute's job.
	PrimaryActorTick.bCanEverTick = true;

	// WHICH ROW OF game/Data/EnemyArchetypes.csv THIS CREATURE IS. It is what
	// lets the hover panel call it a Brute; see ACataclysmEnemyCharacter::
	// ArchetypeRow. Nothing reads the creature's stats out of that row yet.
	ArchetypeRow = TEXT("Brute");

	// The designed numbers, overriding the base enemy's judgement figures. Each
	// one is cited on its declaration in the header.
	MeleeReachCm = DesignedMeleeReachCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;

	// The rest of the designed stat block, which nothing carried until issue
	// #372. Armour is not here because it depends on the encounter's score; see
	// the header, and ACataclysmEnemyCharacter::SetArmour.
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;

	// SEVEN METRES, NOT THE BASE'S FIFTEEN. The header derives it: the distance
	// this enemy covers in one attack cycle, 250 cm/s x 2.8 s. The base's 1500
	// is the longest range a designed player skill reaches, which is a sound
	// rule for a caster and a poor one for a melee enemy slower than the player,
	// because it starts a chase that can never end. Issue #383 asks for the rule
	// covering all seven; this changes only the Brute.
	NoticeRadiusCm = BruteNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(BruteCapsuleRadius, BruteCapsuleHalfHeight);

	// SLOW, AND SLOW TO TURN. These two are the whole of "heavily armored slow
	// melee. Can be outmaneuvered". Without them a Brute is a training dummy
	// with more health.
	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);

	// THE ROCK IT TEARS OUT AND CARRIES. Attached to the prop bone here rather
	// than at the moment of the throw, so that there is nothing to spawn on the
	// frame an attack starts. Hidden until the wind-up asks for it.
	//
	// NO COLLISION. It is held, not thrown -- the thing that is thrown is an
	// ACataclysmProjectile with its own sweep -- so a colliding mesh in the
	// creature's hand would be a second way to hit somebody.
	CarriedRock = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CarriedRock"));
	CarriedRock->SetupAttachment(GetMesh(), RockHoldBoneName);
	CarriedRock->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CarriedRock->SetHiddenInGame(true);
}

void ACataclysmBruteCharacter::BeginPlay()
{
	Super::BeginPlay();

	ResolveBody(/*bIncludeAnimation=*/true);
}

void ACataclysmBruteCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ApplyChaseSpeed();

	// EVERY FRAME, BECAUSE THE THING IT WATCHES CHANGES BETWEEN FRAMES. It
	// compares the clock against the delay an ability montage is waiting out,
	// and asks the brain whether that ability is still being wound up. Both
	// move on their own schedules, so a timer with a fixed deadline would
	// sooner or later fall on the wrong side of one of them -- which is the
	// fault pull request #411 fixed and this replaces outright.
	UpdateAbilityMontage();

	// EVERY FRAME AND FROM THE BRAIN, for the same reason as the line above.
	UpdateCarriedRock();

	// AND AGAIN FOR THE HOLE THE ROCK CAME OUT OF, which appears part way
	// through the same wind-up rather than at either end of it. Issue #432.
	UpdateRipCrater();
}

void ACataclysmBruteCharacter::ApplyChaseSpeed()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// TWO DESIGNED SPEEDS, PICKED BY WHAT THE BRAIN IS DOING. The console
	// variable overrides the chase one for tuning by eye and zero means "use
	// the designed figure", so clearing it mid-session restores the design
	// rather than leaving whatever was last typed. Written every frame for that
	// reason rather than only when the state changes.
	const float Override = CVarBruteChaseSpeed.GetValueOnAnyThread();
	const float ChaseSpeed = Override > 0.0f
		? Override : DesignedChaseSpeedCmPerSecond;

	Movement->MaxWalkSpeed = IsChasing()
		? ChaseSpeed : DesignedWalkSpeedCmPerSecond;
}

TArray<FCataclysmEnemyAbility> ACataclysmBruteCharacter::EnemyAbilities() const
{
	FCataclysmEnemyAbility Stomp;
	Stomp.Name = TEXT("Stomp");
	// FROM ITS OWN FEET OUT, so no minimum: a target pressed against the Brute
	// is inside the ring and should be hit by it.
	Stomp.MinRangeCm = 0.0f;
	Stomp.MaxRangeCm = StompRadiusCm;
	// READ THROUGH THE OVERRIDE, NOT STRAIGHT OFF THE CONSTANT. This array is
	// rebuilt every time ACataclysmEnemyController::IsAbilityReady asks, so a
	// console variable set mid-fight takes effect on the next thinking pass
	// rather than needing a restart. Issue #452.
	Stomp.CooldownSeconds = StompCooldownSecondsInUse();
	Stomp.WindUpSeconds = StompWindUpSeconds;

	// A RING ON THE GROUND, DRAWN FROM THE SAME CONSTANT THE DAMAGE USES.
	// UseEnemyAbility below sweeps FindEnemiesInSphere at StompRadiusCm, so the
	// circle the player sees is exactly the circle that hits them. Issue #396.
	Stomp.Shape = ECataclysmSkillShape::Strike;
	Stomp.MarkerRadiusCm = StompRadiusCm;

	FCataclysmEnemyAbility RockThrow;
	RockThrow.Name = TEXT("Rip and Toss");
	// NOT AT SOMETHING IT COULD HIT INSTEAD. There is no sense throwing a rock
	// at a target already within swinging distance, and the model says so: the
	// ability exists to answer standing off, not to replace the swing.
	//
	// FAR ENOUGH THAT THE CREATURE IS NOT INSIDE ITS OWN BLAST. Issue #475. This
	// was DesignedMeleeReachCm, 90 cm, until 2026-08-09 -- which is the distance
	// at which the two capsules are already touching, so it was a minimum range
	// in name only and the project owner reported the Brute throwing rocks at
	// point blank. The figure is derived rather than picked: below
	// `marker radius + own body radius` the circle the attack marks covers the
	// ground the caster is standing on, which is a melee attack wearing a
	// thrown attack's telegraph.
	//
	// THE SUM IS WRITTEN OUT HERE so that changing either term moves the
	// minimum with it. RockThrowMinimumRangeCm carries the same figure for
	// tests to read, and a static_assert below holds the two together.
	RockThrow.MinRangeCm = RockThrowRadiusCm + BruteCapsuleRadius;
	static_assert(
		RockThrowMinimumRangeCm == RockThrowRadiusCm + BruteCapsuleRadius,
		"RockThrowMinimumRangeCm has drifted from the marked radius plus the "
		"creature's body radius that it is supposed to be. Issue #475 records "
		"why the minimum is that sum: below it the Brute stands inside the "
		"circle its own throw marks.");
	RockThrow.MaxRangeCm = RockThrowRangeCm;
	RockThrow.CooldownSeconds = RockThrowCooldownSecondsInUse();
	RockThrow.WindUpSeconds = RockThrowWindUpSeconds;

	// A CIRCLE WHERE IT LANDS, NOT A LANE ALONG THE WAY, because it is lobbed.
	// The rock rises over everything between the creature and its target and
	// endangers only the ground it comes down on, so a lane would mark ground
	// nothing is going to happen on. Issue #459; it was a lane until then.
	//
	// The radius is the same constant UseEnemyAbility passes to
	// ACataclysmProjectile::Fire, so the circle drawn is the blast that follows.
	// Issue #396.
	RockThrow.Shape = ECataclysmSkillShape::Projectile;
	RockThrow.bArcsOntoItsTarget = true;
	RockThrow.MarkerRadiusCm = RockThrowRadiusCm;

	// ORDER IS PRIORITY. See the StompAbility enumeration in the header.
	return {Stomp, RockThrow};
}

void ACataclysmBruteCharacter::BeginEnemyAbilityWindUp(int32 Index, AActor*)
{
	// THE ANIMATION IS THE TELEGRAPH, for now. Nothing in this project draws a
	// ground marker -- issue #396 -- so the only warning a player gets before a
	// stomp or a thrown rock is the creature visibly starting the attack.
	//
	// BUT IT DOES NOT NECESSARILY START HERE, WHICH IS THE CHANGE OF 2026-08-08.
	// The montage is timed so that the blow arrives exactly when the attack
	// lands, and for the ground smash that means waiting about half a second
	// before beginning to raise its fists. Until then the creature stands in its
	// ordinary idle. The alternative, which this replaces, was to start at once
	// and freeze the montage on one frame to fill the difference; the project
	// owner reported that as the creature seizing up mid-swing.
	const UWorld* World = GetWorld();
	UAnimMontage* Montage = AbilityMontageFor(Index);

	PendingAbilityMontage = INDEX_NONE;

	if (!World || !Montage)
	{
		// Nothing to schedule against. Record the decision anyway.
		PlayAbilityMontage(Index);
		return;
	}

	AbilityWindUpBeganAtSeconds = World->GetTimeSeconds();

	if (MontageDelaySecondsFor(Montage, Index) <= 0.0f)
	{
		PlayAbilityMontage(Index);
		return;
	}

	PendingAbilityMontage = Index;
}

UAnimMontage* ACataclysmBruteCharacter::AbilityMontageFor(int32 Index) const
{
	if (Index == StompAbility)
	{
		return StompMontage.Get();
	}
	if (Index == RockThrowAbility)
	{
		return RockThrowMontage.Get();
	}
	return nullptr;
}

float ACataclysmBruteCharacter::WindUpSecondsFor(int32 Index)
{
	if (Index == StompAbility)
	{
		return StompWindUpSeconds;
	}
	if (Index == RockThrowAbility)
	{
		return RockThrowWindUpSeconds;
	}
	return 0.0f;
}

float ACataclysmBruteCharacter::StrikeIntoReleaseSecondsFor(int32 Index)
{
	if (Index == StompAbility)
	{
		return StompStrikeIntoReleaseSeconds;
	}
	if (Index == RockThrowAbility)
	{
		return RockThrowStrikeIntoReleaseSeconds;
	}
	return 0.0f;
}

float ACataclysmBruteCharacter::LandsAtSecondsFor(float WindUpSeconds)
{
	const float Grid = ACataclysmEnemyController::ThinkIntervalSeconds;
	if (WindUpSeconds <= 0.0f || Grid <= 0.0f)
	{
		return FMath::Max(WindUpSeconds, 0.0f);
	}

	// THE FIRST THINKING PASS AT OR AFTER THE TELEGRAPH EXPIRES, WHICH IS NOT
	// THE SAME MOMENT. ACataclysmEnemyController::ContinueWindUp returns early
	// while Now < WindUpLandsAt and otherwise lands the ability, and it only
	// runs on a pass of a looping quarter-second timer. So the ability lands on
	// a grid step at or after the telegraph, never in between.
	//
	// The stomp's 1.4 second telegraph is 5.6 steps, so it lands on step 6, at
	// 1.50. Sizing anything to the telegraph rather than to this is what made
	// the stomp's hold clip stop a tenth of a second before the attack it
	// existed to hold open, in pull request #409.
	//
	// THE ROCK THROW IS THE CASE THIS FIGURE CANNOT PROMISE. Its 1.0 second
	// telegraph is exactly four steps, and a timer pass carries up to a frame of
	// overshoot that differs between the pass which starts a wind-up and the
	// pass which lands it. Against a strict less-than that decides a whole
	// quarter second: simulated over 500 jittery frames it landed at 1.25 rather
	// than 1.00 in nearly half of them. Issue #413. Returning the earliest keeps
	// the blow from arriving after the damage, which is the worse of the two.
	return FMath::CeilToFloat(WindUpSeconds / Grid) * Grid;
}

float ACataclysmBruteCharacter::JoinSecondsFor(const UAnimMontage* Montage)
{
	// READ OFF THE ASSET RATHER THAN WRITTEN DOWN HERE, so that replacing the
	// wind-up clip with a longer one moves this without anyone remembering to.
	if (!Montage || Montage->SlotAnimTracks.Num() == 0)
	{
		return 0.0f;
	}

	const FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	if (Track.AnimSegments.Num() == 0)
	{
		return 0.0f;
	}

	return Track.AnimSegments[0].GetLength();
}

float ACataclysmBruteCharacter::ImpactSecondsFor(const UAnimMontage* Montage,
												 int32 Index)
{
	// THE JOIN IS NOT THE IMPACT. The release clip opens with the creature's
	// fists still overhead and they take StrikeIntoReleaseSeconds to arrive.
	// Assuming otherwise is what put a frozen frame in the middle of the stomp.
	const float Join = JoinSecondsFor(Montage);
	const float StrikeInClip = StrikeIntoReleaseSecondsFor(Index);

	if (!Montage || Montage->SlotAnimTracks.Num() == 0)
	{
		return Join + StrikeInClip;
	}

	const FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	if (Track.AnimSegments.Num() < 2)
	{
		return Join + StrikeInClip;
	}

	// READ THROUGH THE SEGMENT'S OWN TRIM AND PLAY RATE, WHICH IS WHAT MAKES
	// THIS SURVIVE THE MONTAGE BEING TUNED BY HAND.
	//
	// StrikeIntoReleaseSeconds is measured against the clip as it was authored:
	// the fists reach the ground 0.179 seconds into Ability_GroundSmash_End
	// played at normal speed. A montage segment need not play the clip that way.
	// It can start part way in, and it can play at any rate, and the montage
	// editor is where someone would change either -- which is the whole reason
	// the timing was moved into the asset in the first place.
	//
	// So the moment inside the clip has to be converted into a moment on the
	// montage's own timeline. Adding the raw figure was wrong, and was measured
	// to be wrong on 2026-08-08: with the slam segment slowed to 0.55 the strike
	// really arrives 0.324 seconds into that segment rather than 0.179.
	const FAnimSegment& Release = Track.AnimSegments[1];
	const float Rate = FMath::Abs(Release.GetValidPlayRate());
	if (Rate <= UE_KINDA_SMALL_NUMBER)
	{
		return Join + StrikeInClip;
	}

	const float IntoSegment = (StrikeInClip - Release.AnimStartTime) / Rate;

	// CLAMPED INTO THE SEGMENT, so that trimming the release clip past its own
	// strike gives the segment's end rather than a time outside the montage.
	return Join + FMath::Clamp(IntoSegment, 0.0f, Release.GetLength());
}

float ACataclysmBruteCharacter::MontageRateFor(float ImpactSeconds,
											   float LandsAtSeconds)
{
	if (ImpactSeconds <= 0.0f || LandsAtSeconds <= 0.0f)
	{
		return 1.0f;
	}

	// NEVER SLOWER THAN AUTHORED. ONLY FASTER, AND ONLY WHEN IT MUST BE.
	//
	// Where the montage reaches its blow sooner than the attack lands, this
	// returns 1 and MontageDelaySecondsFor waits instead. Slowing the animation
	// down to fill the gap was tried first and reported from a play session as
	// slow motion.
	//
	// COMPRESSION IS STILL NEEDED IN THE OTHER DIRECTION, and the rock throw
	// needs a great deal of it: the rock does not leave its hand until 1.672
	// seconds and its telegraph allows 1.000, so it plays at 1.67 and looks
	// hurried. Issue #416 asks whether its telegraph should be longer.
	return FMath::Clamp(FMath::Max(1.0f, ImpactSeconds / LandsAtSeconds),
						MinimumPlayRate, MaximumPlayRate);
}

float ACataclysmBruteCharacter::MontageDelaySecondsFor(
	const UAnimMontage* Montage, int32 Index)
{
	const float Impact = ImpactSecondsFor(Montage, Index);
	if (Impact <= 0.0f)
	{
		return 0.0f;
	}

	const float LandsAt = LandsAtSecondsFor(WindUpSecondsFor(Index));
	const float Rate = MontageRateFor(Impact, LandsAt);

	// WAIT OUT WHATEVER THE ANIMATION DOES NOT COVER. At rate 1 the ground smash
	// takes 1.012 seconds to reach the ground and the attack lands at 1.500, so
	// this is 0.488. Where the montage cannot fit at all the rate above has
	// already compressed it, and this is zero.
	return FMath::Max(0.0f, LandsAt - Impact / Rate);
}

void ACataclysmBruteCharacter::PlayAbilityMontage(int32 Index)
{
	UAnimMontage* Montage = AbilityMontageFor(Index);

	// RECORDED BEFORE ANYTHING IS ASKED TO PLAY IT, for the reason given on
	// LastPlayedAnimation in the header: playing needs a running animation
	// graph and choosing does not, so a test can check what was chosen in a
	// world where nothing can play anything.
	const float LandsAt = LandsAtSecondsFor(WindUpSecondsFor(Index));
	const float Rate = MontageRateFor(ImpactSecondsFor(Montage, Index), LandsAt);

	LastPlayedMontage = Montage;
	LastPlayedMontageRate = Montage ? Rate : 0.0f;
	ActiveAbilityMontage = Montage ? Index : INDEX_NONE;

	if (!Montage)
	{
		return;
	}

	// COUNTED, BECAUSE COMPARING WHICH MONTAGE IS PLAYING CANNOT DETECT A SECOND
	// PLAY OF THE SAME ONE. See AbilityMontagesStarted in the header.
	++AbilityMontagesStarted;

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	// THE ASSET CARRIES ITS OWN BLEND SETTINGS AND NOTHING IS PASSED HERE.
	// Montage_Play takes no blend arguments: it uses the montage's BlendIn,
	// BlendOut and BlendOutTriggerTime. Those are written by
	// tools/generate_brute_montages.py and checked against
	// AttackBlendInSeconds, AttackBlendOutSeconds and AbilityBlendOutTriggerTime
	// by Cataclysm.Brute.ItsAbilityMontagesAreBuiltCorrectly.
	//
	// The montage plays into the slot its own animation track names, which is
	// DefaultSlot -- the same slot as AttackSlotName and as the Slot node inside
	// ABP_Brute. A montage played into a slot the graph does not have is dropped
	// with no error at all, so those three agreeing is load bearing.
	AnimInstance->Montage_Play(Montage, Rate,
							   EMontagePlayReturnType::MontageLength);
}

void ACataclysmBruteCharacter::UpdateCarriedRock()
{
	if (!CarriedRock)
	{
		return;
	}

	// ASKED, NOT REMEMBERED. Every way a wind-up ends clears this on the
	// controller -- the attack landing, a stun cancelling it, the pawn being
	// unpossessed -- so one question covers all of them and this function does
	// not have to know what any of them are.
	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(GetController());
	const bool bHolding =
		Brain != nullptr && Brain->WindingUpAbility == RockThrowAbility;

	// A ROCK THAT DID NOT LOAD STAYS HIDDEN. Without the Paragon pack RockMesh
	// is null, and an empty mesh component shown in the hand is nothing to see
	// rather than a fault, but hiding it keeps the state honest.
	CarriedRock->SetHiddenInGame(!bHolding || RockMesh == nullptr);
}

float ACataclysmBruteCharacter::RipReachesGroundAtSeconds() const
{
	UAnimMontage* Montage = AbilityMontageFor(RockThrowAbility);

	const float LandsAt = LandsAtSecondsFor(WindUpSecondsFor(RockThrowAbility));
	const float Rate =
		MontageRateFor(ImpactSecondsFor(Montage, RockThrowAbility), LandsAt);

	// THE DELAY FIRST, THEN THE CLIP. The montage does not begin when the
	// wind-up does: MontageDelaySecondsFor waits out whatever the animation is
	// too short to cover. Adding the measured moment to the wind-up's start
	// instead would put the hole in the ground before the creature had reached
	// for it, by exactly that delay.
	const float Delay = MontageDelaySecondsFor(Montage, RockThrowAbility);

	return RipReachesGroundAtSeconds(Delay, Rate);
}

float ACataclysmBruteCharacter::RipReachesGroundAtSeconds(float DelaySeconds,
														 float Rate)
{
	// DIVIDED BY THE RATE, because the rip is compressed. The rock throw is
	// compressed hard -- 1.67 at the time of writing, which is what issue #416
	// is about -- so measuring 0.2644 s of authored clip and then waiting 0.2644
	// s of wall clock would be most of a fifth of a second late, and the hole
	// would appear after the creature had already lifted the rock out of it.
	return DelaySeconds
		+ RipReachesGroundSeconds / FMath::Max(Rate, UE_SMALL_NUMBER);
}

FVector ACataclysmBruteCharacter::RipCraterLocation() const
{
	// FORWARD OF THE CREATURE, NOT AT ITS FEET. The rip reaches down and out;
	// the two hands bottom out 52.9 cm ahead of the centre line. See the header
	// for the measurement and for why the axis is not the obvious one.
	FVector Where = GetActorLocation()
		+ GetActorForwardVector() * CraterAheadCm;

	// ON THE FLOOR RATHER THAN AT THE ACTOR'S HEIGHT. An actor's location is the
	// middle of its capsule, so a crater left there would hang in the air at
	// chest height. The hands stop 2.9 cm off the ground, which is the ground.
	if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Where.Z = GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	}

	return Where;
}

void ACataclysmBruteCharacter::UpdateRipCrater()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ASKED, NOT REMEMBERED, exactly as UpdateCarriedRock does. Every way a
	// wind-up can end clears this on the controller, so clearing the flag here
	// covers the attack landing, a stun cancelling it and the pawn being
	// unpossessed without this function knowing what any of them are.
	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(GetController());
	if (!Brain || Brain->WindingUpAbility != RockThrowAbility)
	{
		bCraterLeftForThisThrow = false;
		return;
	}

	if (bCraterLeftForThisThrow || !RockCraterMesh)
	{
		return;
	}

	// COMPARED AGAINST THE CLOCK EVERY FRAME. See the header, and the much
	// longer note on UpdateAbilityMontage that this follows.
	const float Elapsed = World->GetTimeSeconds() - AbilityWindUpBeganAtSeconds;
	if (Elapsed + UE_KINDA_SMALL_NUMBER < RipReachesGroundAtSeconds())
	{
		return;
	}

	bCraterLeftForThisThrow = true;

	// ONE PIECE, NO SPREAD. ACataclysmDebrisBurst is the project's way of
	// putting meshes on the ground for a short time and taking them away again,
	// and issue #422 built it generic on purpose -- it names no project content
	// and knows nothing about rocks. A crater is that mechanism with a single
	// piece, which is why there is no second actor here.
	//
	// IT NEEDS THE MATERIAL FOR THE SAME REASON THE FRAGMENTS DO. The crater
	// mesh carries the engine's checkerboard placeholder.
	ACataclysmDebrisBurst::Scatter(
		this, RipCraterLocation(), {RockCraterMesh}, RockMaterial,
		/*SpreadCm=*/0.0f, CraterRadiusCm, CraterSecondsOnTheGround);
}

void ACataclysmBruteCharacter::UpdateAbilityMontage()
{
	if (PendingAbilityMontage == INDEX_NONE)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ABANDONED IF THE ABILITY IS NO LONGER BEING WOUND UP. A pawn unpossessed
	// or destroyed mid-telegraph leaves no landing to hook, and both of those
	// used to strand a clip playing for its full length. Here the montage simply
	// never starts, which is the whole fault class gone rather than patched.
	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(GetController());
	if (!Brain || Brain->WindingUpAbility != PendingAbilityMontage)
	{
		PendingAbilityMontage = INDEX_NONE;
		return;
	}

	UAnimMontage* Montage = AbilityMontageFor(PendingAbilityMontage);
	const float Elapsed = World->GetTimeSeconds() - AbilityWindUpBeganAtSeconds;

	// COMPARED AGAINST THE CLOCK EVERY FRAME RATHER THAN SET AS A DEADLINE. A
	// timer fixes its deadline when it is created, which is how a held clip came
	// to fire on the wrong side of the pass that landed its ability in pull
	// request #411. This cannot be out of order with anything.
	if (Elapsed + UE_KINDA_SMALL_NUMBER
			< MontageDelaySecondsFor(Montage, PendingAbilityMontage))
	{
		return;
	}

	const int32 Index = PendingAbilityMontage;
	PendingAbilityMontage = INDEX_NONE;
	PlayAbilityMontage(Index);
}

float ACataclysmBruteCharacter::RockThrowFlightSecondsFor(
	const FVector& LandsAt) const
{
	// A PARABOLA SAGS g * t * t / 8 BELOW ITS OWN CHORD, so a sag of
	// `fraction * range` is in the air for sqrt(8 * fraction * range / g).
	// Inverting it here rather than stating the time is issue #474: a stated
	// time fixes the whole vertical part of the trajectory whatever the
	// distance, which made every short throw a near-vertical mortar.
	const float ApexCm = RockThrowApexCmFor(LandsAt);
	if (ApexCm <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Sqrt(
		8.0f * ApexCm / ACataclysmProjectile::LobGravityCmPerSecondSquared);
}

float ACataclysmBruteCharacter::StompCooldownSecondsInUse() const
{
	const float Override = CVarBruteStompCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : StompCooldownSeconds;
}

float ACataclysmBruteCharacter::RockThrowCooldownSecondsInUse() const
{
	const float Override = CVarBruteRockThrowCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : RockThrowCooldownSeconds;
}

FVector ACataclysmBruteCharacter::RockLaunchLocation() const
{
	// THE SAME BONE THE CARRIED ROCK HANGS FROM, so what is in the hand and
	// what leaves it start from the same place. CarriedRock is attached to
	// RockHoldBoneName in the constructor.
	if (const USkeletalMeshComponent* Body = GetMesh())
	{
		if (Body->DoesSocketExist(RockHoldBoneName))
		{
			return Body->GetSocketLocation(RockHoldBoneName);
		}
	}

	// WITHOUT THE PARAGON PACK THERE IS NO SKELETON AND NO BONE, which is the
	// state on a fresh clone and in every test world. Falling back to the
	// capsule centre is what this did before issue #454, so the throw still
	// works and still arcs; it simply starts lower.
	return GetActorLocation();
}

float ACataclysmBruteCharacter::RockThrowApexCmFor(const FVector& LandsAt) const
{
	const float Override = CVarBruteRockArc.GetValueOnAnyThread();
	const float Fraction = Override > 0.0f ? Override : RockThrowApexFraction;

	// A FRACTION OF THE DISTANCE THROWN, so a rock lobbed two metres is not
	// given the same loop as one thrown ten. Measured across the ground rather
	// than through the air, because that is what the fraction is a fraction of.
	return Fraction * FVector::Dist2D(RockLaunchLocation(), LandsAt);
}

void ACataclysmBruteCharacter::UseEnemyAbility(int32 Index, AActor* Target,
											   const FVector& AimedAt)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// NOTHING IS PLAYED HERE, AND THAT IS THE CHANGE. The release half of the
	// attack is already in the montage this ability started when its wind-up
	// began, sitting immediately after the wind-up half. All that has to happen
	// at the moment of impact is nothing at all: the montage was started part
	// way into the telegraph precisely so that its blow arrives now.
	//
	// WHAT THAT REMOVES. Playing a second clip here meant the release was a
	// separate montage that had to blend against the first, be sized to its own
	// window, and be started in the right order relative to a hold clip that was
	// on a timer. Pull requests #409, #410 and #411 were three different failures
	// of that arrangement. Inside one montage there is no seam and no ordering.

	if (Index == StompAbility)
	{
		// EVERYTHING IN THE RING, not just the target. Angle=360 in the model,
		// which is what stops the answer to a Brute being "stand behind it".
		//
		// THE STUN IS THE POINT OF THIS ATTACK, NOT A RIDER ON IT. The Brute is
		// the first thing in the game that stuns the player, and this is the
		// attack that does it. It is applied after the damage rather than before
		// so the hit is what the threshold rule would see if this were an
		// ordinary stun -- it is not, it is a designed one, but ordering the two
		// the other way round would make that distinction invisible.
		// PARSED ONCE RATHER THAN PER TARGET. A stomp catches a crowd.
		const FGameplayTagContainer AbilityTags =
			UCataclysmSkillShapes::TagsFromCell(StompTags);

		for (AActor* Caught : UCataclysmTargeting::FindEnemiesInSphere(
				World, this, GetActorLocation(), StompRadiusCm))
		{
			// AREA DAMAGE, so it cannot be evaded. The design says evasion
			// avoids a direct attack only and that area damage lands
			// regardless, and this swept a sphere. Issue #513.
			//
			// SAID BY ITS TAGS AND NOT BY THIS CALL, since issue #519. The
			// Type.AOE.PointBlank in StompTags is what ApplyHit reads, which
			// is the same route a player skill takes. Passing an area
			// delivery here as well would be a second answer to one
			// question.
			const float Dealt =
				UCataclysmSkillEffects::ApplyHit(this, Caught, StompDamagePercent,
												 AbilityTags);

			UCataclysmSkillEffects::ApplyStun(this, Caught, StompStunSeconds,
											  Dealt, /*bStunIsDesigned=*/true);

			// AND IT SHOVES, WHICH IS THE FIRST THING IN THE GAME TO MOVE THE
			// PLAYER. The design settled on issue #310 that enemies displace the
			// player and named this as one of the three abilities that do it;
			// issue #625 is the implementation. Five player skills already grant
			// immunity to displacement and until now protected against nothing.
			//
			// AFTER THE STUN, AND THE ORDER DOES NOT CHANGE THE OUTCOME. Neither
			// reads the other. It is written last because a shove is the least of
			// the three things this attack does, and reading it in the order the
			// player feels them is worth more than any saving.
			//
			// OUTWARD FROM THE CREATURE, which ApplyKnockback works out from the
			// two positions. That is right for a 360 degree slam: everything
			// caught is pushed away from the middle rather than in one direction.
			UCataclysmSkillEffects::ApplyKnockback(this, Caught,
												   StompKnockbackCm);
		}
		return;
	}

	if (Index == RockThrowAbility)
	{
		// AIMED WHERE IT WAS MARKED. AimedAt is where the target stood when the
		// wind-up began, so a player who moved has moved out of the line.
		//
		// IT THROWS THE PACK'S ROCK, NOT A GREY SPHERE, and it passes the mesh
		// rather than the projectile knowing about it: every player skill fires
		// through the same class, so a rock baked in there would arm every fire
		// bolt with one. RockMesh is null without the Paragon pack, which is the
		// state on a fresh clone, and the projectile then keeps its engine
		// sphere. Issue #404.
		//
		// IT LEAVES NOTHING WHERE IT LANDS. Issue #434 broke the rock into five
		// pieces on impact and the project owner removed that on 2026-08-08 as
		// unwanted, which is issue #455. The projectile's OnFinished delegate is
		// untouched and still reports where a shot stopped; nothing on the Brute
		// binds to it any more.
		//
		// FROM THE HAND, NOT FROM THE MIDDLE OF THE CREATURE. Issue #454. It
		// used to leave GetActorLocation(), which is the capsule centre and 110
		// cm up, so the rock appeared at the creature's waist while the
		// animation threw it overhead.
		//
		// AND IT LOBS, WHICH IS NOT SEPARABLE FROM THAT. Issue #459. Before
		// this the projectile flattened every shot and flew level, so firing
		// from a hand well above 250 cm would have sent the rock horizontally
		// over the head of a player whose own is about 192. The launch point
		// and the trajectory had to change together.
		//
		// A FLIGHT TIME, AND NO SPEED AT ALL. Issue #465. A ballistic shot has
		// no single speed to give -- it is slowest at the top of its arc and
		// fastest as it lands -- so the projectile is told how long it has and
		// works the rest out. The zero here is not a beam: a projectile is a
		// beam when it is given NEITHER a speed nor a flight time.
		ACataclysmProjectile::Fire(
			this, RockLaunchLocation(), AimedAt,
			RockThrowRadiusCm, /*InSpeed=*/0.0f,
			/*InPierce=*/0, /*bInReturns=*/false, RockThrowDamagePercent,
			UCataclysmSkillShapes::TagsFromCell(RockThrowTags),
			/*bInBurns=*/false, RockMesh,
			RockThrowFlightSecondsFor(AimedAt));
		return;
	}
}

float ACataclysmBruteCharacter::DesignedSecondsBetweenAttacks() const
{
	const float Override = CVarBruteAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : AttackIntervalSeconds;
}

void ACataclysmBruteCharacter::AttackTarget(AActor* Target)
{
	Super::AttackTarget(Target);
	PlayAttackAnimation();
}

void ACataclysmBruteCharacter::PlayAttackAnimation()
{
	// THE ANIMATION'S OWN LENGTH, NOT THE ATTACK INTERVAL, which is what
	// passing no duration means. The swing is 1.0 seconds and the interval
	// between swings is 1.6, so holding the mesh for the interval would leave
	// the Brute frozen in its finishing pose after every hit.
	PlayOneShot(AttackAnimation);
}

float ACataclysmBruteCharacter::PlayOneShot(UAnimSequence* Animation,
											float HoldSeconds,
											float BlendOutTriggerTime)
{
	const UWorld* World = GetWorld();
	if (!World || !Animation)
	{
		return 0.0f;
	}

	const float Length = Animation->GetPlayLength();
	if (Length <= 0.0f)
	{
		return 0.0f;
	}

	// No duration asked for means play it at normal speed for as long as it is.
	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;

	// NEVER SLOWER THAN IT WAS AUTHORED. ONLY FASTER, AND ONLY WHEN IT MUST BE.
	//
	// Stretching a short clip across a long window was tried first and is
	// wrong. The ground smash wind-up is 0.83 seconds inside a 1.4 second
	// telegraph, so filling the window played it at 0.59 speed: the Brute
	// raised its arms in slow motion and then the release ran at full speed,
	// which was reported from a play session as a glitch rather than as one
	// movement. The clips are authored at one speed and changing it on only
	// half of them is what looks wrong.
	//
	// HOLDING THE LAST POSE IS WHAT THE PACK EXPECTS. Rampage ships a separate
	// Ability_GroundSmash_Loop of 0.03 seconds, whose only purpose is to hold a
	// wind-up open for as long as the telegraph needs. A creature poised with
	// its arms up for the last half second of a telegraph is the intended
	// reading, and it is also the clearest warning the player gets.
	//
	// COMPRESSION IS STILL NEEDED IN THE OTHER DIRECTION. The rock throw
	// wind-up clip is longer than its 1.0 second telegraph, so at authored
	// speed it is cut off before the rock comes free -- which is the other half
	// of what that play session reported.
	const float Rate = FMath::Clamp(FMath::Max(1.0f, Length / Hold),
									MinimumPlayRate, MaximumPlayRate);

	// HOW LONG IT REALLY TAKES, WHICH IS NOT ITS LENGTH ONCE IT IS COMPRESSED.
	// The rock throw's wind-up clip is 1.13 seconds played at a rate of 1.13,
	// so it occupies exactly 1.00 second of wall clock. A caller that scheduled
	// anything from the clip's own length would be 0.13 seconds late.
	const float PlaysFor = Length / Rate;

	PlayInAttackSlot(Animation, Rate, BlendOutTriggerTime);

	return PlaysFor;
}

void ACataclysmBruteCharacter::PlayInAttackSlot(UAnimSequence* Animation,
												float Rate,
												float BlendOutTriggerTime)
{
	// EVERY PLAIN CLIP THIS CREATURE PLAYS GOES THROUGH HERE, which since
	// 2026-08-08 is the ordinary swing and nothing else. The two abilities play
	// montage assets, which carry their own blend settings; see
	// PlayAbilityMontage.
	//
	// The blend settings are recorded and used in the same breath, from the
	// same two locals, so what is recorded cannot drift from what was asked
	// for. It drifted once already: a caller recorded AttackBlendInSeconds while
	// passing a literal zero, so a test written against the record passed while
	// the creature snapped rather than blended.
	const float BlendIn = AttackBlendInSeconds;
	const float BlendOut = AttackBlendOutSeconds;

	// RECORDED BEFORE ANYTHING IS ASKED TO PLAY IT. Playing needs a running
	// animation graph and deciding does not, so a test can check what was
	// chosen in a world where nothing can play anything -- which is every
	// automation test world and every clone without the Paragon art.
	LastPlayedAnimation = Animation;
	LastPlayedRate = Rate;
	LastPlayedBlendInSeconds = BlendIn;
	LastPlayedBlendOutTriggerTime = BlendOutTriggerTime;

	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	// THROUGH THE ANIMATION SLOT, NOT BY REPLACING WHAT THE MESH IS PLAYING.
	//
	// WHAT THIS FIXED. The previous version drove the mesh in
	// EAnimationMode::AnimationSingleNode, which plays exactly one clip and
	// cannot blend between two. Each ability is two clips -- a wind-up and a
	// release -- so the moment the release started, the mesh jumped from the
	// last pose of the wind-up to the first pose of the release in a single
	// frame. Reported from a play session on 2026-08-08 as the stomp reading
	// wrong. Nothing in C++ could fix it, because the fault was the animation
	// mode rather than the code driving it.
	//
	// A dynamic montage needs no montage asset on disk: it wraps a plain
	// sequence and plays it in the named slot of ABP_Brute's animation graph,
	// blending in over AttackBlendInSeconds and back out over
	// AttackBlendOutSeconds. Locomotion keeps running underneath and is
	// blended back to when the clip ends, which is also why nothing has to
	// hold the mesh open for the duration any more.
	//
	// THE SEVENTH ARGUMENT IS NOT OPTIONAL DRESSING. See
	// AbilityBlendOutTriggerTime in the header: leaving it at the engine's
	// default throws away the last AttackBlendOutSeconds of every clip.
	UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	// ONE CALL AGAIN, BECAUSE THE FRACTIONAL LOOP COUNT IS GONE. This used to
	// build the montage by hand through
	// CreateSlotAnimationAsDynamicMontage_WithFractionalLoops, purely so a hold
	// clip could be repeated some fraction of a time. Nothing needs a fraction
	// of a repeat any more: the swing plays exactly once.
	AnimInstance->PlaySlotAnimationAsDynamicMontage(
		Animation, AttackSlotName, BlendIn, BlendOut, Rate, /*LoopCount=*/1,
		BlendOutTriggerTime);
}

bool ACataclysmBruteCharacter::IsChasing() const
{
	// ASKED OF THE BRAIN RATHER THAN INFERRED FROM SPEED. ABP_Brute does infer
	// it from speed, because by the time the animation graph runs the two
	// states really are two different speeds -- 250 wandering and 500 chasing.
	// This is asked earlier than that: ApplyChaseSpeed is what SETS those two
	// speeds, so it cannot read them back to decide which one to use.
	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(GetController());
	return Brain && Brain->LastAction == ECataclysmBrainAction::Chasing;
}

bool ACataclysmBruteCharacter::ResolveBody(bool bIncludeAnimation)
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return false;
	}

	USkeletalMesh* Body = Cast<USkeletalMesh>(
		FSoftObjectPath(BodyMeshPath).TryLoad());

	if (!Body)
	{
		// NOT AN ERROR, AND SAID OUT LOUD RATHER THAN LEFT TO BE NOTICED. The
		// Paragon packs are gitignored, so this is the expected state on a fresh
		// clone and in continuous integration. The placeholder cylinder the base
		// class made stays visible, so the Brute is still there to fight -- it
		// just looks like every other enemy.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Brute art not found at %s, so it is keeping the placeholder "
				 "cylinder. This is expected without the Paragon Rampage pack; "
				 "see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// RESOLVED HERE RATHER THAN AT THE MOMENT OF THE THROW. Loading an asset
	// synchronously is a hitch, and the moment of the throw is the worst frame
	// in the fight to take one on. Null without the pack, which is what a fresh
	// clone and continuous integration both have; the projectile then keeps its
	// engine sphere and the throw still works. Issue #404.
	RockMesh = Cast<UStaticMesh>(FSoftObjectPath(RockMeshPath).TryLoad());

	// THE SAME MESH THE THROW FLIES, AND NOW THE SAME SIZE. One asset was never
	// enough on its own: until issue #453 nothing scaled this component, so it
	// drew at whatever the artist authored while the thrown rock was scaled to
	// the width the projectile acts at. Measured 2026-08-09 with
	// tools/measure_rock_sizes.py, SM_Rock_To_Hold is authored 206.6 cm across
	// against a Brute whose whole capsule is 96, so the creature held a rock
	// twice its own width and then threw one 80 cm across.
	//
	// SCALED TO WHAT THE THROWN ONE WILL BE. ACataclysmProjectile::Fire gives a
	// projectile that does not pierce its DefaultBodyRadiusCm, and the rock does
	// not pierce, so that is the width it will fly at. Sharing the arithmetic
	// through CataclysmMeshWidth is what stops the two drifting again.
	if (CarriedRock)
	{
		CarriedRock->SetStaticMesh(RockMesh);

		const float Scale = CataclysmMeshWidth::ScaleFor(
			RockMesh, ACataclysmProjectile::DefaultBodyRadiusCm);
		if (Scale > 0.0f)
		{
			CarriedRock->SetRelativeScale3D(FVector(Scale, Scale, Scale));
		}
	}

	// THE MATERIAL COMES FROM THE ROCK ITSELF. The rip crater is the only thing
	// that wears it now, and the crater mesh ships with the engine's grey
	// checkerboard placeholder rather than a rock material of its own.
	RockMaterial = RockMesh ? RockMesh->GetMaterial(0) : nullptr;

	// THE HOLE IT CAME OUT OF. Null without the pack, which leaves the rip
	// exactly as it was rather than breaking it -- UpdateRipCrater checks.
	// It wears RockMaterial for the reason just above. Issue #432.
	RockCraterMesh =
		Cast<UStaticMesh>(FSoftObjectPath(RockCraterMeshPath).TryLoad());

	if (!RockMesh)
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Brute rock not found at %s, so its throw will fly a "
				 "placeholder sphere. This is expected without the Paragon "
				 "Rampage pack."),
			RockMeshPath);
	}

	// FEET ON THE CAPSULE BOTTOM. A skeletal mesh is authored with its origin at
	// the feet, and the capsule's origin is its centre, so the mesh drops by the
	// half-height. The yaw is the engine's convention for character meshes,
	// which face -Y while the actor faces +X.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -BruteCapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	if (bIncludeAnimation)
	{
		ResolveAnimationBlueprint(MeshComponent);

		AttackAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(AttackAnimationPath).TryLoad());

		// THE DEATH CLIP, AND A NULL ENTRY IS KEPT rather than skipped. See
		// ACataclysmEnemyCharacter::PlayDeathAnimation: dropping one would
		// change how many clips there are and therefore which one is drawn.
		DeathAnimations.Reset();
		DeathAnimations.Add(Cast<UAnimSequence>(
			FSoftObjectPath(DeathAnimationPath).TryLoad()));

		// TWO MONTAGES INSTEAD OF SIX CLIPS. Each one holds its ability's
		// wind-up and release back to back, so this class no longer loads,
		// stores or sequences the individual clips at all.
		//
		// THE MONTAGE ASSETS ARE COMMITTED BUT THE CLIPS INSIDE THEM ARE NOT,
		// which makes a failure here mean something different from a missing
		// mesh. The .uasset will load on a fresh clone; what it references will
		// not, so it loads with an empty animation track and a play length of
		// zero. JoinSecondsFor returns zero for that, MontageRateFor returns 1,
		// and nothing divides by it, so the creature attacks invisibly rather
		// than misbehaving.
		StompMontage = Cast<UAnimMontage>(
			FSoftObjectPath(StompMontagePath).TryLoad());
		RockThrowMontage = Cast<UAnimMontage>(
			FSoftObjectPath(RockThrowMontagePath).TryLoad());

		if (!AttackAnimation)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Brute attack animation not found at %s, so its swings "
					 "will be invisible."),
				AttackAnimationPath);
		}

		if (!StompMontage || !RockThrowMontage)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Brute ability montages not found at %s and %s, so its "
					 "stomp and rock throw will be invisible. Build them with "
					 "tools/generate_brute_montages.py."),
				StompMontagePath, RockThrowMontagePath);
		}
	}

	// OTHERWISE THE CYLINDER SITS INSIDE THE DEMON. The base class creates
	// PlaceholderBody in its constructor and nothing about assigning a skeletal
	// mesh removes it.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetVisibility(false);
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Brute is wearing %s."), BodyMeshPath);

	return true;
}

bool ACataclysmBruteCharacter::ResolveAnimationBlueprint(
	USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return false;
	}

	// THE GENERATED CLASS, NOT THE BLUEPRINT ASSET. SetAnimInstanceClass wants a
	// UClass, and an animation Blueprint's runtime class is its asset path with
	// _C on the end. Loading the asset itself and casting would silently give
	// null, because a UAnimBlueprint is not a UAnimInstance subclass.
	UClass* AnimationClass =
		FSoftClassPath(AnimationBlueprintPath).TryLoadClass<UAnimInstance>();

	if (!AnimationClass)
	{
		// NOT AN ERROR, FOR THE SAME REASON THE MISSING MESH IS NOT. ABP_Brute
		// is committed, but every animation it plays comes from the gitignored
		// Paragon Rampage pack, so on a fresh clone the graph has nothing to
		// reference. The Brute still fights; it just holds its reference pose.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Brute animation Blueprint not found at %s, so it will hold "
				 "its reference pose and its attacks will be invisible. This is "
				 "expected without the Paragon Rampage pack; see "
				 "game/docs/enemy-source-assets.md."),
			AnimationBlueprintPath);
		return false;
	}

	// THE MODE AS WELL AS THE CLASS. SetAnimInstanceClass sets the mode too, but
	// saying it outright is what stops a later reader assuming this component is
	// still in the single-node mode it used until 2026-08-08.
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	MeshComponent->SetAnimInstanceClass(AnimationClass);

	return true;
}
