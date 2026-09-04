// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmSuccubusCharacter.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPath.h"

// THE COUNTESS, FROM THE PARAGON COUNTESS PACK. She is a hero rig rather than a
// minion one -- 126 bones and 16 material slots against the lane minions' 39 --
// and her skeleton is her own, so nothing here is shared with any other
// creature.
const TCHAR* ACataclysmSuccubusCharacter::BodyMeshPath =
	TEXT("/Game/ParagonCountess/Characters/Heroes/Countess/"
		 "Meshes/SM_Countess.SM_Countess");

const TCHAR* ACataclysmSuccubusCharacter::AnimationFolder =
	TEXT("/Game/ParagonCountess/Characters/Heroes/Countess/Animations");

// THE LONGEST IDLE IN THE PROJECT AT 42.3333 SECONDS, which is four times the
// Hellhound's ten and is what makes a creature standing about not read as a
// loop. `Idle_Straight` is the alternative at 7.5 seconds.
const TCHAR* ACataclysmSuccubusCharacter::IdleAnimationName =
	TEXT("Idle_Relaxed");

// 1.8000 seconds, carrying a planted foot at 321.0 cm/s. See JogPlayRate.
const TCHAR* ACataclysmSuccubusCharacter::JogAnimationName = TEXT("Jog_Fwd");

// 0.9000 seconds, into a 1.3 second wind-up. Issue #767 is the 0.4 seconds of
// held pose that leaves; the header records why the pack's 1.5 second
// `Primary_Attack_Slow` was not used instead.
const TCHAR* ACataclysmSuccubusCharacter::AttackAnimationName =
	TEXT("Primary_Attack_Normal");

// 1.1333 seconds. A DIFFERENT CLIP FROM THE BOLT'S ON PURPOSE: the design's
// counter to the curse is interrupting it, and a player cannot interrupt what
// they cannot tell apart from an ordinary attack.
const TCHAR* ACataclysmSuccubusCharacter::CastAnimationName = TEXT("Cast");

// ONE DEATH, THE FEWEST IN THE PROJECT along with the Brute's. Measured
// 2026-08-20 at 1.6667 seconds, inside
// UCataclysmEnemyDeath::LongestCorpseSeconds.
const TCHAR* ACataclysmSuccubusCharacter::DeathAnimationNames
	[DeathAnimationCount] = {
	TEXT("Death"),
};

// THE TWO EFFECT NAMES ARE THE ROWS OF game/Data/StatusEffects.csv, spelled as
// that file spells them. UCataclysmSkillShapes::StatusTagFor drops every
// character that is not a letter or a digit, so these become
// `Status.Debuff.WitheredTouch` and `Status.Buff.Commander`, both of which are in
// game/Config/Tags/CataclysmTags.ini.
const TCHAR* ACataclysmSuccubusCharacter::WitherEffectName =
	TEXT("Withered Touch");

const TCHAR* ACataclysmSuccubusCharacter::DominionEffectName =
	TEXT("Commander");

/**
 * Seconds between shots, for tuning one while playing.
 */
static TAutoConsoleVariable<float> CVarSuccubusAttackInterval(
	TEXT("Cataclysm.Succubus.AttackInterval"),
	0.0f,
	TEXT("Seconds between the Succubus's Soulfire bolts. 0 uses its designed "
		 "2.6. Remember that its telegraph is exactly half its interval and is "
		 "the largest the rules allow, so shortening this shortens the warning "
		 "too and the marker stops being legal below about 2.6."),
	ECVF_Default);

/**
 * Seconds before the curse may be cast again.
 */
static TAutoConsoleVariable<float> CVarSuccubusWitherCooldown(
	TEXT("Cataclysm.Succubus.WitherCooldown"),
	0.0f,
	TEXT("Seconds between the Succubus's curses. 0 uses its designed 10.0. "
		 "Below 5.0 the curse can be recast before the last one expires, so "
		 "the player never has a moment without it."),
	ECVF_Default);

ACataclysmSuccubusCharacter::ACataclysmSuccubusCharacter()
{
	// UpdateLoopingAnimation RUNS FROM Tick and is what returns the mesh to its
	// idle or its walk after a cast.
	PrimaryActorTick.bCanEverTick = true;

	// The row of game/Data/EnemyArchetypes.csv this creature reads. One word,
	// unlike the Corrupted Sentinel's `Corrupted_Sentinel`.
	ArchetypeRow = TEXT("Succubus");

	// ITS "MELEE REACH" IS ITS SHOT'S RANGE, AND THAT IS NOT A MISUSE.
	// `ACataclysmEnemyController::Think` treats the reach as the distance at
	// which the creature stops walking and starts attacking, and for a creature
	// that shoots 8 metres that distance is 8 metres.
	// `ATTACK_REACH['Succubus']` in the model is 8.0 for the same reason.
	MeleeReachCm = SoulfireRangeCm;
	AttackIntervalSeconds = DesignedAttackIntervalSeconds;
	ResistancePercent = DesignedResistancePercent;
	CritChancePercent = DesignedCritChancePercent;
	CritMultiplierPercent = DesignedCritMultiplierPercent;
	EvasionPercent = DesignedEvasionPercent;
	EnergyShieldFraction = DesignedEnergyShieldFraction;
	NoticeRadiusCm = SuccubusNoticeRadiusCm;

	GetCapsuleComponent()->InitCapsuleSize(SuccubusCapsuleRadius,
										   SuccubusCapsuleHalfHeight);

	GetCharacterMovement()->MaxWalkSpeed = DesignedWalkSpeedCmPerSecond;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, DesignedTurnRateDegreesPerSecond, 0.0f);
}

void ACataclysmSuccubusCharacter::BeginPlay()
{
	Super::BeginPlay();
	ResolveBody(/*bIncludeAnimation=*/true);

	// STANDING RATHER THAN THE REFERENCE POSE, from the first frame.
	UpdateLoopingAnimation();

	// THE AURA IS ON FROM THE MOMENT IT SPAWNS. There is nothing to trigger it
	// and nothing to aim it; it is a property of the creature being alive.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DominionTimer, this, &ACataclysmSuccubusCharacter::DominionTick,
			DominionRefreshSeconds, /*bLoop=*/true,
			/*InFirstDelay=*/DominionRefreshSeconds);
	}

	// AND THE FIRST SWEEP RUNS NOW RATHER THAN AFTER THE FIRST PERIOD, so a
	// creature that spawns beside an ally is buffing it on the frame it appears
	// rather than half a second later.
	PulseDominion();
}

void ACataclysmSuccubusCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// THE BUFF DOES NOT SURVIVE THE CREATURE LEAVING THE LEVEL. Without this a
	// Succubus destroyed outright -- rather than killed -- would leave its
	// allies buffed for DominionGrantSeconds with nothing to explain it.
	EndDominion();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DominionTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void ACataclysmSuccubusCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// BEFORE THE LOOPING ANIMATION, so a cast that becomes due this frame owns
	// the mesh for the rest of it rather than being overwritten by an idle
	// chosen a line earlier.
	StartPendingWindUpClip();
	UpdateLoopingAnimation();
}

float ACataclysmSuccubusCharacter::SoulfirePlayRate()
{
	return StrikeAlignedPlayRate(SoulfireReleaseSeconds, SoulfireWindUpSeconds,
								 MinimumPlayRate, MaximumPlayRate);
}

float ACataclysmSuccubusCharacter::SoulfireDelaySeconds()
{
	return StrikeAlignedDelaySeconds(SoulfireReleaseSeconds,
									 SoulfireWindUpSeconds,
									 MinimumPlayRate, MaximumPlayRate);
}

void ACataclysmSuccubusCharacter::StartPendingWindUpClip()
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

	// **THE WIND-UP HAS TO STILL BE HAPPENING.** Asked of the brain rather than
	// remembered here, so there is one answer to "is this creature still
	// winding that up" and a cancelled cast cannot start its clip afterwards.
	const ACataclysmEnemyController* Brain =
		Cast<ACataclysmEnemyController>(GetController());
	if (!Brain || Brain->WindingUpAbility != PendingWindUpAbility)
	{
		PendingWindUpAbility = INDEX_NONE;
		return;
	}

	// COMPARED AGAINST THE CLOCK EVERY FRAME RATHER THAN SET AS A DEADLINE, for
	// the reason the Brute's version gives: a timer fixes its deadline when it
	// is created and can fire on the wrong side of the pass that lands the
	// ability.
	const float Elapsed = World->GetTimeSeconds() - WindUpBeganAtSeconds;
	if (Elapsed + UE_KINDA_SMALL_NUMBER < SoulfireDelaySeconds())
	{
		return;
	}

	PendingWindUpAbility = INDEX_NONE;
	PlayOneShotAtRate(AttackAnimation.Get(), SoulfirePlayRate());
}

void ACataclysmSuccubusCharacter::HandleDeath()
{
	// **THE WHOLE POINT OF THE CREATURE, IN ONE CALL.** The design document's
	// sentence is "killing it first is the correct play, and this is what makes
	// that true". A buff that outlived the caster by even a second would make
	// that false.
	//
	// BEFORE Super, WHICH STOPS THE TICK AND THE TIMERS. HandleDeath on the base
	// disables this actor's tick and cancels what it was doing, and a cleared
	// timer cannot take the buff off afterwards.
	//
	// SAFE TO RUN TWICE. `UCataclysmSkillEffects::MarkDead` inside Super refuses
	// a second death, and EndDominion on an empty list does nothing.
	EndDominion();

	Super::HandleDeath();
}

float ACataclysmSuccubusCharacter::AttackIntervalSecondsInUse()
{
	const float Override = CVarSuccubusAttackInterval.GetValueOnAnyThread();
	return Override > 0.0f ? Override : DesignedAttackIntervalSeconds;
}

float ACataclysmSuccubusCharacter::WitherCooldownSecondsInUse()
{
	const float Override = CVarSuccubusWitherCooldown.GetValueOnAnyThread();
	return Override > 0.0f ? Override : WitherCooldownSeconds;
}

float ACataclysmSuccubusCharacter::DesignedSecondsBetweenAttacks() const
{
	return AttackIntervalSecondsInUse();
}

void ACataclysmSuccubusCharacter::AttackTarget(AActor* /*Target*/)
{
	// NOTHING, AND THAT IS THE WHOLE POINT OF THIS OVERRIDE.
	//
	// The base class's AttackTarget applies direct damage at `MeleeReachCm`, and
	// this creature's reach is its SHOT's range of 8 metres. Left to the base it
	// would deal a free melee hit at eight metres every 2.6 seconds on top of
	// the bolt it is already firing, and nothing on screen would say where the
	// damage came from. The Corrupted Sentinel found this first.
	//
	// ITS BASIC ATTACK IS Soulfire, which is an entry in EnemyAbilities because
	// it is telegraphed. See the class comment for why that needs no new
	// machinery.
}

TArray<FCataclysmEnemyAbility>
ACataclysmSuccubusCharacter::EnemyAbilities() const
{
	FCataclysmEnemyAbility Wither;
	Wither.Name = TEXT("Wither the Living");

	// NO MINIMUM RANGE. The curse marks no ground, so there is no circle that
	// could cover the creature's own feet -- which is the only reason any
	// ability here has a minimum. It curses whatever it can reach.
	Wither.MinRangeCm = 0.0f;
	Wither.MaxRangeCm = WitherRangeCm;
	Wither.CooldownSeconds = WitherCooldownSecondsInUse();

	// ZERO WIND-UP, AND THAT IS DESIGNED. A Debuff draws no marker, so a wind-up
	// would be the creature standing still for no visible reason. See
	// WitherWindUpSeconds in the header.
	Wither.WindUpSeconds = WitherWindUpSeconds;

	// THE FIRST ENEMY ABILITY IN THE GAME WITH THIS SHAPE.
	// `ACataclysmEnemyController::ShowWindUpMarker` already handles it: its
	// default arm draws nothing for Debuff and says so in as many words.
	Wither.Shape = ECataclysmSkillShape::Debuff;
	Wither.bArcsOntoItsTarget = false;
	Wither.MarkerRadiusCm = 0.0f;

	FCataclysmEnemyAbility Soulfire;
	Soulfire.Name = TEXT("Soulfire");

	// NO MINIMUM RANGE, AND THE DESIGN STATES NONE. A flat shot marks a LANE,
	// and a lane starts at the creature whatever the distance, so the argument
	// that gives a lobbed attack a minimum does not apply. The Corrupted
	// Sentinel's flat bolt is the same.
	Soulfire.MinRangeCm = 0.0f;
	Soulfire.MaxRangeCm = SoulfireRangeCm;

	// ZERO, WHICH IS WHAT MAKES IT THE BASIC ATTACK. The creature's own attack
	// interval is the only thing spacing it out, and
	// `ACataclysmEnemyController::UseAbilitiesOn` applies that to every ability.
	Soulfire.CooldownSeconds = SoulfireCooldownSeconds;
	Soulfire.WindUpSeconds = SoulfireWindUpSeconds;

	// A LANE, because it travels flat and hits what it passes.
	Soulfire.Shape = ECataclysmSkillShape::Projectile;
	Soulfire.bArcsOntoItsTarget = false;
	Soulfire.MarkerRadiusCm = SoulfireRadiusCm;

	// ORDER IS PRIORITY, AND THE CURSE HAS TO COME FIRST. See the enumeration in
	// the header: Soulfire has a cooldown of zero and the same 8 metre range, so
	// a Soulfire at the front of this array is the only thing the creature would
	// ever do and the curse would never be cast. Issue #491 is that defect on
	// the Abyssal Warden.
	return {Wither, Soulfire};
}

void ACataclysmSuccubusCharacter::BeginEnemyAbilityWindUp(int32 Index, AActor*)
{
	// ONLY SOULFIRE REACHES HERE. The curse has a wind-up of zero, and
	// `ACataclysmEnemyController::UseAbilitiesOn` only calls this when there is
	// a wind-up to begin; a zero-wind-up ability goes straight to
	// UseEnemyAbility, which is where the curse plays its own clip.
	if (Index != SoulfireAbility)
	{
		return;
	}

	// THE CLIP RUNS ACROSS THE WIND-UP, NOT ACROSS THE INTERVAL. The clip IS the
	// wind-up: the bolt leaves as the telegraph ends, so the cast should end
	// there too. The Corrupted Sentinel is the one creature that does this
	// differently, and its reason is particular to it -- a firing animation that
	// aims, fires and recovers rather than one that winds up.
	//
	// **THE CAST WAITS. IT DOES NOT START HERE.** Its clip releases 0.156
	// seconds in and the bolt is fired at 1.3, so starting it now released the
	// cast 1.14 seconds before the bolt appeared. Issue #784.
	// StartPendingWindUpClip, from Tick, starts it once the delay has passed.
	PendingWindUpAbility = INDEX_NONE;

	if (const UWorld* World = GetWorld())
	{
		WindUpBeganAtSeconds = World->GetTimeSeconds();
		if (SoulfireDelaySeconds() > 0.0f)
		{
			PendingWindUpAbility = SoulfireAbility;
			return;
		}
	}

	PlayOneShotAtRate(AttackAnimation.Get(), SoulfirePlayRate());
}

void ACataclysmSuccubusCharacter::UseEnemyAbility(
	int32 Index, AActor* Target, const FVector& AimedAt)
{
	if (!GetWorld())
	{
		return;
	}

	if (Index == SoulfireAbility)
	{
		// FROM THE MIDDLE OF THE CREATURE, WHICH IS A KNOWN APPROXIMATION. The
		// Brute fires from the bone its rock hangs off, because issue #454 found
		// a rock appearing at its waist while the animation threw it overhead.
		// The same question is open here and issue #366 covers the measuring
		// this creature has not had.
		const FVector From = GetActorLocation();

		// NO TAGS, WHICH IS WHAT EVERY ENEMY PROJECTILE PASSES TODAY. Tags scope
		// the caster's own stat modifiers, and an enemy carries none.
		const FGameplayTagContainer NoTags;

		// AIMED WHERE IT WAS MARKED. AimedAt is where the target stood when the
		// wind-up began, so a player who stepped out of the lane is not hit --
		// which is the whole of what a telegraph buys.
		//
		// A SPEED AND NO FLIGHT TIME, so it travels flat. At 1200 cm/s it
		// crosses its whole 8 metre range in two thirds of a second.
		LastShotFired = ACataclysmProjectile::Fire(
			this, From, AimedAt, SoulfireRadiusCm,
			SoulfireSpeedCmPerSecond, SoulfirePierce, /*bInReturns=*/false,
			SoulfireDamagePercent, NoTags, /*bInBurns=*/false);
		return;
	}

	if (Index == WitherTheLivingAbility)
	{
		// THE CAST IS WHAT THE PLAYER READS IT OFF, and it is a different clip
		// from the bolt's. Played across its own length rather than across an
		// interval, because nothing is waiting for it: the curse has already
		// landed by the time this returns.
		PlayOneShot(CastAnimation.Get());

		// THE TARGET THE BRAIN ALREADY CHOSE, rather than a fresh search.
		// `MaxTargets` is 1, and the one thing this creature is fighting is the
		// one thing worth cursing. `UCataclysmDebuffSkill` searches because a
		// player's curse has a cursor to aim with and this has none.
		LastCursed = nullptr;
		if (!Target)
		{
			return;
		}

		const FGameplayTag Effect =
			UCataclysmSkillShapes::StatusTagFor(WitherEffectName);

		// A TAG AND NO MAGNITUDE, which is what issue #166 settled a buff or a
		// debuff is until there is something to apply its magnitude to.
		// Withered Touch "reduces player damage output and mana/energy shield",
		// and nothing in the project reads a modifier for any of those three
		// yet. Granting the tag makes the duration real and makes "is it up?" a
		// question with a true answer.
		if (UCataclysmSkillEffects::ApplyTagForDuration(
				this, Target, Effect, WitherDurationSeconds))
		{
			LastCursed = Target;
		}
		return;
	}
}

int32 ACataclysmSuccubusCharacter::PulseDominion()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	// A DEAD SUCCUBUS BUFFS NOBODY. HandleDeath clears the field, and this guard
	// is what stops a sweep that was already in flight putting it back.
	if (UCataclysmSkillEffects::IsDead(this))
	{
		return 0;
	}

	const FGameplayTag Effect =
		UCataclysmSkillShapes::StatusTagFor(DominionEffectName);
	if (!Effect.IsValid())
	{
		// The tag vocabulary does not carry Status.Buff.Commander. Nothing can be
		// granted and nothing can be taken away, so say so once rather than
		// failing silently sixty times a minute.
		UE_LOG(LogCataclysm, Warning,
			TEXT("Dominion cannot grant %s: no such gameplay tag. See "
				 "game/Config/Tags/CataclysmTags.ini and "
				 "tools/generate_gameplay_tags.py."),
			DominionEffectName);
		return 0;
	}

	// ALLIES, NOT ENEMIES, AND THIS IS THE ONLY PLACE IN THE GAME THAT ASKS FOR
	// THEM ON A CREATURE'S BEHALF. `FindAlliesInSphere` excludes the instigator
	// itself, which is what the design wants: Dominion buffs "every allied enemy
	// within 8 metres" and the Succubus is not its own ally. It also refuses
	// corpses, so a dead ally is not buffed.
	const TArray<AActor*> Inside = UCataclysmTargeting::FindAlliesInSphere(
		World, this, GetActorLocation(), DominionRadiusCm);

	// TAKEN OFF WHOEVER WALKED OUT, BEFORE IT IS PUT ON WHOEVER WALKED IN. The
	// order does not matter for correctness -- the two sets are disjoint -- and
	// doing the removals first keeps DominionHolders from growing across a sweep
	// in which everything left.
	for (const TWeakObjectPtr<AActor>& Held : DominionHolders)
	{
		AActor* Holder = Held.Get();
		if (Holder && !Inside.Contains(Holder))
		{
			UCataclysmSkillEffects::RemoveEffectsGranting(Holder, Effect);
		}
	}

	DominionHolders.Reset();

	for (AActor* Ally : Inside)
	{
		// REFRESHED RATHER THAN STACKED. ApplyTagForDuration builds a
		// single-stack effect, so a second application replaces the first
		// instead of adding to it, which is what the design requires of every
		// effect in the game.
		if (UCataclysmSkillEffects::ApplyTagForDuration(
				this, Ally, Effect, DominionGrantSeconds))
		{
			DominionHolders.Add(Ally);
		}
	}

	return DominionHolders.Num();
}

void ACataclysmSuccubusCharacter::EndDominion()
{
	const FGameplayTag Effect =
		UCataclysmSkillShapes::StatusTagFor(DominionEffectName);

	// EVEN IF THE TAG IS INVALID, THE LIST IS EMPTIED. Holding names of things
	// that are no longer buffed would make a later sweep try to unbuff them
	// twice.
	if (Effect.IsValid())
	{
		for (const TWeakObjectPtr<AActor>& Held : DominionHolders)
		{
			if (AActor* Holder = Held.Get())
			{
				// TWO SUCCUBI OVER ONE ALLY IS A KNOWN APPROXIMATION. The
				// effects are single-stack, so the ally holds one Commander
				// whichever of them granted it, and this removal takes it away
				// for both. The survivor's next sweep puts it back within
				// DominionRefreshSeconds. Making that exact would need the
				// effect handles kept per grantor, which is more machinery than
				// a half-second gap is worth.
				UCataclysmSkillEffects::RemoveEffectsGranting(Holder, Effect);
			}
		}
	}

	DominionHolders.Reset();
}

void ACataclysmSuccubusCharacter::DominionTick()
{
	PulseDominion();
}

float ACataclysmSuccubusCharacter::JogPlayRate()
{
	if (AuthoredJogSpeedCmPerSecond <= 0.0f)
	{
		return 1.0f;
	}

	// THE RATIO OF WHAT IT MOVES AT TO WHAT THE CLIP CARRIES IT AT. A planted
	// foot travels backwards at the clip's authored speed; the body travels
	// forwards at the designed speed; playing at the ratio makes the two cancel
	// and the foot stay put.
	//
	// 350 / 321.0 IS 1.090, THE GENTLEST WALK IN THE PROJECT, against the Imp's
	// 1.699 and the Hellhound's 2.478.
	return FMath::Clamp(
		DesignedWalkSpeedCmPerSecond / AuthoredJogSpeedCmPerSecond,
		MinimumPlayRate, MaximumPlayRate);
}

void ACataclysmSuccubusCharacter::UpdateLoopingAnimation()
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
	MeshComponent->SetPlayRate(bWalking ? JogPlayRate() : 1.0f);
}

float ACataclysmSuccubusCharacter::PlayOneShot(UAnimSequence* Animation,
											   float HoldSeconds)
{
	const float Length = Animation ? Animation->GetPlayLength() : 0.0f;

	// NEVER SLOWER THAN AUTHORED, ONLY FASTER, AND ONLY WHEN IT MUST BE. The
	// same rule every other creature here uses, for the reason recorded there:
	// stretching a short clip across a long window was tried and read as slow
	// motion. A clip shorter than its window holds its last pose instead.
	//
	// THIS LINES UP THE CLIP'S END WITH THE WINDOW'S END, which is right for a
	// clip that finishes on its blow. The cast does not -- it releases a sixth
	// of the way in -- so it goes through PlayOneShotAtRate instead. Issue #784.
	const float Hold = HoldSeconds > 0.0f ? HoldSeconds : Length;
	const float Rate = Hold > 0.0f
		? FMath::Clamp(FMath::Max(1.0f, Length / Hold),
					   MinimumPlayRate, MaximumPlayRate)
		: 1.0f;

	return PlayOneShotAtRate(Animation, Rate);
}

float ACataclysmSuccubusCharacter::PlayOneShotAtRate(UAnimSequence* Animation,
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
	// playing" the loop the cast interrupted, so nothing would put it back.
	CurrentLoopingAnimation = nullptr;

	return Length / Rate;
}

bool ACataclysmSuccubusCharacter::ResolveBody(bool bIncludeAnimation)
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
			TEXT("Succubus art not found at %s, so it is keeping the "
				 "placeholder cylinder. This is expected without the Paragon "
				 "Countess pack; see game/docs/enemy-source-assets.md."),
			BodyMeshPath);
		return false;
	}

	MeshComponent->SetSkeletalMesh(Body);

	// FEET ON THE CAPSULE BOTTOM, AND THE ENGINE'S YAW. A skeletal mesh is
	// authored with its origin at the feet and the capsule's origin is its
	// centre, so the mesh drops by the half-height. The -90 degree yaw is the
	// engine's convention for character meshes, which face -Y while the actor
	// faces +X -- and the stride measurement found this rig's forward axis to be
	// -Y, which is what says the convention holds for it.
	//
	// AT THE SIZE IT WAS AUTHORED. The mesh is 180.8 cm tall, which is a person,
	// and this creature is designed as one. The Imp is the only creature here
	// that is worn scaled.
	MeshComponent->SetRelativeLocationAndRotation(
		FVector(0.0f, 0.0f, -SuccubusCapsuleHalfHeight),
		FRotator(0.0f, -90.0f, 0.0f));

	if (bIncludeAnimation)
	{
		IdleAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, IdleAnimationName))
				.TryLoad());
		JogAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, JogAnimationName))
				.TryLoad());
		AttackAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, AttackAnimationName))
				.TryLoad());
		CastAnimation = Cast<UAnimSequence>(
			FSoftObjectPath(ClipPathIn(AnimationFolder, CastAnimationName))
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

		if (!IdleAnimation || !JogAnimation || !AttackAnimation
			|| !CastAnimation)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Succubus animations missing: idle %s, walk %s, attack %s, "
					 "cast %s. It will fight with nothing to show for it. This "
					 "is expected without the Paragon Countess pack."),
				IdleAnimation ? TEXT("found") : TEXT("MISSING"),
				JogAnimation ? TEXT("found") : TEXT("MISSING"),
				AttackAnimation ? TEXT("found") : TEXT("MISSING"),
				CastAnimation ? TEXT("found") : TEXT("MISSING"));
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

	// bAnimationBlueprintBound IS LEFT ALONE HERE, which matches every other
	// creature: no animation Blueprint exists for any of them, so the flag stays
	// false and `UpdateLoopingAnimation` takes the single-node path. It is read
	// rather than written so that binding a graph later is one assignment.

	UE_LOG(LogCataclysm, Verbose, TEXT("Succubus is wearing %s."), BodyMeshPath);
	return true;
}
