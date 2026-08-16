// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyCharacter.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Half-height and radius of the collision capsule, in centimetres. */
	constexpr float EnemyCapsuleRadius = 48.0f;
	constexpr float EnemyCapsuleHalfHeight = 80.0f;

}

ACataclysmEnemyCharacter::ACataclysmEnemyCharacter()
{
	// Every enemy is on the Monsters side, so no skill of one enemy's can hit
	// another. The exception the design asks for -- Madness, where "the enemy
	// attacks anything nearby, friend or foe" -- is a change of attitude for a
	// tagged actor and not a change of side. Issue #163 builds it.
	TeamId = UCataclysmTeams::IdFor(ECataclysmTeam::Monsters);

	// Its own brain, possessed as soon as it exists. Without both of these an
	// enemy stands where it was spawned for its whole life: nothing else in the
	// project gives a non-player pawn a controller.
	AIControllerClass = ACataclysmEnemyController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// It turns to face where it is walking, like the player character does.
	// Without this the cylinder slides sideways and there is no way to tell from
	// looking at it which way it thinks it is going.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 480.0f, 0.0f);

	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Minimal replication: no client owns an enemy, so no client needs full
	// gameplay effect data for one. Tags and cues are enough to drive visuals.
	// A dungeon floor can hold a great many enemies and this is where the
	// bandwidth goes if it is set wrong.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	// Three sets, not five. An enemy has no attribute points to spend and no
	// class tree, so the primary attribute set and the class resource set would
	// be dead weight replicated on every spawn, and a dungeon floor can hold a
	// great many enemies.
	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(TEXT("VitalAttributes"));
	CombatAttributes = CreateDefaultSubobject<UCataclysmCombatAttributeSet>(TEXT("CombatAttributes"));
	ResistanceAttributes = CreateDefaultSubobject<UCataclysmAllResistanceAttributeSet>(TEXT("ResistanceAttributes"));

	// A stand-in body, for the same reason the player has one: this project's
	// own Content folder holds no meshes at all, so without it an enemy is an
	// invisible capsule and there is no way to tell whether one is there.
	//
	// Slightly wider and shorter than the player's, so the two can be told
	// apart at a glance while both are cylinders.
	GetCapsuleComponent()->InitCapsuleSize(EnemyCapsuleRadius, EnemyCapsuleHalfHeight);

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(RootComponent);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(
		(EnemyCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(EnemyCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(EnemyCapsuleHalfHeight * 2.0f) / ACataclysmCharacterBase::BasicShapeSize));

	// Found by path rather than referenced as an asset, because these are engine
	// content. A failure here is not fatal: the capsule is still there and still
	// takes damage, it is just invisible.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	}

	// EVERY ENEMY TICKS, BECAUSE A CHARGE ADVANCES PER FRAME.
	// `ACataclysmCharacterBase` turns ticking off, and AdvanceCharge cannot be
	// driven by the brain instead: the brain thinks four times a second and a
	// charge covers metres in one of those. Issue #491.
	//
	// WHAT IT COSTS A CREATURE THAT NEVER CHARGES: one boolean test a frame.
	// Tick below returns immediately unless a charge is running.
	PrimaryActorTick.bCanEverTick = true;
}

void ACataclysmEnemyCharacter::HandleDeath()
{
	if (!UCataclysmSkillEffects::MarkDead(this))
	{
		// Already dead. Nothing here is safe to run twice.
		return;
	}

	// WHATEVER IT WAS DOING STOPS. A charge already in flight is the one that
	// matters: it advances per frame from Tick and would otherwise carry the
	// corpse across the room. The same reasoning as being stunned mid-charge,
	// issue #499.
	CancelCharge();

	// DisableMovement CLEARS THE VELOCITY TOO, so there is no separate call to
	// stop it. UCharacterMovementComponent::OnMovementModeChanged runs
	// StopMovementKeepPathing when the new mode is MOVE_None -- "Kill velocity
	// and clear queued up events". A StopMovementImmediately beside this was
	// written first and proved to be doing nothing: removing it failed no test,
	// which is how it was found.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->DisableMovement();
	}

	// The capsule stops blocking, so a corpse on its way out cannot push the
	// player around or stand in the way of another creature for the frame it
	// has left.
	SetActorEnableCollision(false);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				Destroy();
			}));
	}
}

void ACataclysmEnemyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AdvanceCharge(DeltaSeconds);
}

void ACataclysmEnemyCharacter::BeginCharge(const FVector& ToPoint,
										   float SpeedCmPerSecond,
										   float HalfWidthCm,
										   float DamagePercent,
										   float KnockbackCm)
{
	if (SpeedCmPerSecond <= 0.0f)
	{
		// A charge with no speed would never end, because nothing else finishes
		// one. Refused rather than started, so the creature keeps walking.
		return;
	}

	// ON THE CREATURE'S OWN HEIGHT, NOT THE POINT'S. The lane is a floor-plane
	// thing and ToPoint arrives flattened onto the ground -- the controller
	// captures it through FloorUnder for the same reason issue #471 flattened
	// the aim point. Charging toward a point at floor height would drag the
	// capsule down through the floor by its own half-height.
	ChargeEndPoint = FVector(ToPoint.X, ToPoint.Y, GetActorLocation().Z);

	ChargeSpeedCmPerSecond = SpeedCmPerSecond;
	ChargeHalfWidthCm = HalfWidthCm;
	ChargeDamagePercent = DamagePercent;
	ChargeKnockbackCm = KnockbackCm;
	ChargeTravelledCm = 0.0f;
	ChargeHitCount = 0;
	ChargeAlreadyHit.Reset();
	bCharging = true;
}

void ACataclysmEnemyCharacter::CancelCharge()
{
	// WHERE IT STOPPED IS WHERE IT STOPS. The creature is left standing at the
	// point it had reached, which is the honest outcome of being interrupted
	// mid-run. Nothing is rewound and it does not slide on.
	//
	// THE HIT LIST GOES WITH IT. It belongs to this charge, and the next one has
	// to be able to hit the same target again.
	bCharging = false;
	ChargeAlreadyHit.Reset();
}

void ACataclysmEnemyCharacter::AdvanceCharge(float DeltaSeconds)
{
	if (!bCharging || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// SPLIT INTO STEPS NO LONGER THAN LongestChargeStepCm. See the header: one
	// long sweep can tunnel a thin wall. A whole frame's travel is covered
	// either way, so this changes where the charge is checked and not how far
	// it goes.
	float RemainingThisFrameCm = ChargeSpeedCmPerSecond * DeltaSeconds;

	while (bCharging && RemainingThisFrameCm > 0.0f)
	{
		const float StepCm = FMath::Min(RemainingThisFrameCm, LongestChargeStepCm);
		RemainingThisFrameCm -= StepCm;

		if (!StepCharge(StepCm))
		{
			return;
		}
	}
}

bool ACataclysmEnemyCharacter::StepCharge(float StepCm)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		bCharging = false;
		return false;
	}

	const FVector From = GetActorLocation();

	// HOW FAR IS LEFT, IN THE FLOOR PLANE. The lane is horizontal, so the
	// remaining distance is a 2D one; a 3D measure would charge the creature for
	// a height difference that the charge never crosses.
	const float RemainingCm = FVector::Dist2D(From, ChargeEndPoint);
	if (RemainingCm <= KINDA_SMALL_NUMBER)
	{
		bCharging = false;
		return false;
	}

	FVector Direction = ChargeEndPoint - From;
	Direction.Z = 0.0f;
	Direction = Direction.GetSafeNormal();

	// NEVER PAST THE END OF ITS OWN LANE. The marker was drawn to exactly this
	// point, so a step that overshot would take the creature onto ground it
	// never warned about.
	const float ThisStepCm = FMath::Min(StepCm, RemainingCm);
	const FVector To = From + Direction * ThisStepCm;

	// STOPPED BY THE LEVEL, NOT BY BODIES, AND THE DESIGN ASKS FOR BOTH HALVES.
	//
	// Not by bodies: the creature is committed and runs the full distance,
	// ending past its target -- "it ends up ten metres past the player, facing
	// away", which is the window the telegraph buys. A charge that stopped on
	// contact would arrive in melee range instead, which is the opposite of what
	// the design says a miss costs.
	//
	// By the level: a charge that ran through a wall would be the marker lying
	// about where the creature ends up.
	//
	// BY OBJECT TYPE, NOT BY CHANNEL, AND THAT DISTINCTION IS THE WHOLE THING.
	// SweepSingleByChannel(ECC_WorldStatic) asks "what BLOCKS the WorldStatic
	// channel", and a Pawn capsule blocks it -- so the first version of this
	// stopped dead on the creature's own capsule and travelled nothing, and
	// would have stopped on the player too. SweepSingleByObjectType asks "what
	// IS a WorldStatic object", which is the question actually being asked here.
	//
	// A SPHERE RATHER THAN THE CAPSULE, AND IT IS SMALLER ON PURPOSE. A capsule
	// sweep would be more faithful and is wrong here: the capsule's bottom rests
	// ON the floor, and the floor is WorldStatic, so a capsule swept along it
	// grazes the ground and every charge would stop on its first step. A sphere
	// of the capsule's radius centred at the capsule's centre sits well clear of
	// the floor -- 66 cm clear for the Warden -- and still meets a wall at body
	// height, which is what a charge should be stopped by.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CataclysmCharge),
								 /*bInTraceComplex=*/false, this);

	FHitResult Hit;
	const bool bBlocked = World->SweepSingleByObjectType(
		Hit, From, To, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(GetCapsuleComponent()
			? GetCapsuleComponent()->GetScaledCapsuleRadius()
			: 0.0f),
		Params);

	const FVector Landed = bBlocked ? Hit.Location : To;

	// NO SWEEP ON THE MOVE ITSELF, because the sweep above already decided where
	// it may go and a second one against Pawn would stop it on the very target
	// it is meant to run through.
	SetActorLocation(Landed, /*bSweep=*/false);
	ChargeTravelledCm += FVector::Dist2D(From, Landed);

	// EVERYTHING THIS STEP PASSED, ONCE EACH. "A charge hits everything on the
	// way" -- section X of docs/Cataclysm_GDD_v2.md, which distinguishes it from
	// a leap that hits only where it lands. Tested against the segment actually
	// travelled rather than the whole lane, so nothing is hit before the
	// creature reaches it.
	for (AActor* Caught : UCataclysmTargeting::FindEnemiesInLine(
			World, this, From, Landed, ChargeHalfWidthCm))
	{
		if (!Caught || ChargeAlreadyHit.Contains(Caught))
		{
			continue;
		}

		ChargeAlreadyHit.Add(Caught);
		++ChargeHitCount;
		UCataclysmSkillEffects::ApplyHit(this, Caught, ChargeDamagePercent);

		// AND IT SHOVES WHAT IT RUNS THROUGH ASIDE, when the ability asked for
		// it. The design settled on issue #310 that enemies displace the player
		// and gave the reason for a charge in particular: the player's own Bull
		// Rush and Cinder Rush charge through a crowd "knocking them aside", so a
		// charge that runs through the player does the same to them. Issue #625.
		//
		// AWAY FROM THE CREATURE AT THE MOMENT OF CONTACT, which is what
		// ApplyKnockback works out from the two positions. For a charge that is
		// diagonal rather than straight out: contact happens at the LEADING edge
		// of the lane, so the target is carried forward as well as out. It still
		// finishes outside the lane, which is what clears the ground.
		//
		// ONCE PER TARGET PER CHARGE, because it sits inside the same guard the
		// damage does. A charge that shoved on every step would push a target the
		// whole length of the lane.
		UCataclysmSkillEffects::ApplyKnockback(this, Caught, ChargeKnockbackCm);
	}

	if (bBlocked || FVector::Dist2D(Landed, ChargeEndPoint) <= KINDA_SMALL_NUMBER)
	{
		bCharging = false;
		return false;
	}

	return true;
}

void ACataclysmEnemyCharacter::SetHealth(float NewMaxHealth)
{
	if (NewMaxHealth <= 0.0f)
	{
		return;
	}

	// REMEMBERED AS WELL AS APPLIED, and that is what makes the order of calls
	// not matter. A spawner naturally sets health on the line after SpawnActor,
	// which on an actor whose BeginPlay has not run yet is before the attribute
	// sets are registered -- so writing then is either lost or, worse, raises an
	// engine ensure. Storing it means InitAbilityActorInfo applies it when the
	// ability system is genuinely ready.
	StartingMaxHealth = NewMaxHealth;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::SetAttackDamage(float NewAttackDamage)
{
	if (NewAttackDamage < 0.0f)
	{
		return;
	}

	StartingAttackDamage = NewAttackDamage;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::SetRarityStep(int32 NewStep)
{
	// A NEGATIVE STEP IS A CALLER ERROR, ANSWERED WITH COMMON. There is nothing
	// below Common on the ladder, and clamping beats letting a bad value make
	// IsBoss's comparison quietly meaningless.
	RarityStep = FMath::Max(0, NewStep);
}

void ACataclysmEnemyCharacter::ApplyStartingAttributes()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	// Writing to an attribute the ability system does not hold yet raises an
	// engine ensure rather than failing quietly, so each is checked rather than
	// attempted. HasAttributeSetForAttribute is false before the component has
	// been initialised.
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (StartingMaxHealth > 0.0f
		&& AbilitySystemComponent->HasAttributeSetForAttribute(MaxHealth))
	{
		// MAXIMUM FIRST, THEN CURRENT, and the order is not incidental. The vital
		// attribute set clamps health to the maximum in PreAttributeChange, so
		// raising the current value before the maximum would clamp it straight
		// back down to whatever the old maximum was.
		AbilitySystemComponent->SetNumericAttributeBase(MaxHealth, StartingMaxHealth);
		AbilitySystemComponent->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), StartingMaxHealth);
	}

	const FGameplayAttribute Damage =
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	if (StartingAttackDamage > 0.0f
		&& AbilitySystemComponent->HasAttributeSetForAttribute(Damage))
	{
		AbilitySystemComponent->SetNumericAttributeBase(Damage, StartingAttackDamage);
	}

	// --- the rest of the designed stat block. Issue #372 ---
	//
	// UNTIL THIS LANDED AN ENEMY HELD THREE ATTRIBUTES OUT OF ROUGHLY TWENTY.
	// Armour, every resistance, evasion and both crit figures sat at the
	// attribute sets' own defaults and nothing ever wrote to them, so the Brute
	// -- which the design document calls heavily armoured and gives the
	// second-highest armour share of the seven vertical slice enemies -- was a
	// slow enemy with extra health and nothing else.
	//
	// WRITTEN DIRECTLY RATHER THAN THROUGH A GameplayEffect, which was the open
	// question on that issue. These are BASE values, and a modifier layers on
	// top of the base either way, so the effect's one real advantage does not
	// apply here. It would need an asset per enemy or a programmatic effect, and
	// issue #355 rebuilds this transport anyway once the archetype numbers are
	// game data. Two mechanisms for one job is worse than one.
	//
	// NO ZERO CHECKS BELOW, unlike the two writes above. Zero armour, zero
	// resistance and zero evasion are all designed values -- the Imp's armour
	// share really is 0.0 -- so treating zero as "not configured" would make an
	// unarmoured creature impossible to express.
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetArmorAttribute(), StartingArmour);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetEvasionAttribute(), EvasionPercent);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetCritChanceAttribute(),
				CritChancePercent);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(),
				CritMultiplierPercent);

	// ONE FIGURE, AND THIS CREATURE HAS NO TYPED RESISTANCES AT ALL -- it does not
	// hold the attribute set they live in. The design model says so in as many
	// words: "percent of all incoming damage resisted, whatever its type. One
	// figure, not eight." A per-type enemy profile is not something the design
	// has, and inventing one here would be inventing design.
	//
	// IT USED TO BE THE SAME NUMBER WRITTEN INTO ALL EIGHT TYPED SLOTS, and that
	// resisted nothing. `ResistanceFor` in CataclysmDamageCalculation.cpp picks a
	// slot from the incoming hit's damage type, and player damage carries no type
	// -- deliberately, because this creature resists everything equally, so a type
	// would be choosing between eight copies of one number. With no type there was
	// no slot to pick and all eight were skipped. Issue #486.
	ApplyIfHeld(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(),
				ResistancePercent);

	// A CREATURE DOES NOT REGENERATE, AND THAT IS A STATED POSITION RATHER THAN
	// AN OMISSION. Issue #653 wired up the three regeneration attributes, which
	// until then no code read at all. UCataclysmVitalAttributeSet's constructor
	// sets health and mana regeneration to 1.0 as a placeholder for a character
	// with no class attached, so leaving them alone would have handed every
	// creature in the game a heal nobody designed: a Brute recovering while the
	// player disengages, and anything walked away from returning to full.
	//
	// THE DESIGN GIVES REGENERATION TO CLASSES, NOT TO CREATURES. Each of the
	// three Demonic class stat lines states a health and a mana regeneration
	// figure. The enemy archetype table has no column for either, and
	// `stats_for` in sim/cataclysm_sim/enemy_stats.py computes no such figure. A
	// creature that should regenerate would be expressed as an archetype
	// property or a modifier, which is issue #355's territory rather than a
	// default nobody chose.
	//
	// ZERO RATHER THAN A SKIPPED TICK, so the rule lives in the creature's own
	// numbers where it can be read and changed, rather than inside the
	// mechanism as a special case for one kind of character.
	ApplyIfHeld(UCataclysmVitalAttributeSet::GetHealthRegenAttribute(), 0.0f);
	ApplyIfHeld(UCataclysmVitalAttributeSet::GetManaRegenAttribute(), 0.0f);
	ApplyIfHeld(UCataclysmVitalAttributeSet::GetEnergyShieldRegenAttribute(),
				0.0f);

	// THE ENERGY SHIELD, WHICH NOTHING WROTE AT ALL UNTIL ISSUE #485. The
	// fraction reached the generated archetype table and the row struct that
	// reads it, and then stopped: there was no property on this class and no
	// write here, so every enemy in the editor had a shield of zero whatever the
	// design said. The layer itself works -- it is step 7 of the eight in
	// UCataclysmDamageCalculation::Resolve -- so the number was the only thing
	// missing.
	//
	// COMPUTED FROM THE FRACTION AND THE MAXIMUM HEALTH, which is the same
	// arithmetic `stats_for` in sim/cataclysm_sim/enemy_stats.py does:
	// `health * energy_shield_fraction`. Storing a second absolute number here
	// would be a figure that could disagree with the health beside it.
	//
	// READ BACK OFF THE ATTRIBUTE RATHER THAN FROM StartingMaxHealth, because
	// this function runs from the setters AND from InitAbilityActorInfo, and a
	// creature whose health was never set through SetHealth still has the
	// attribute set's own maximum. Reading the attribute gives the right answer
	// in both cases; StartingMaxHealth is zero in the second.
	//
	// MAXIMUM FIRST, THEN CURRENT, for the same reason the health write above
	// says: the vital attribute set clamps the current shield to the maximum, so
	// filling it before raising the maximum would clamp it straight back down.
	//
	// NO ZERO CHECK, like the four writes above it and unlike the two at the top.
	// Five of the seven designed enemies have a fraction of exactly 0.00, so
	// treating zero as "not configured" would make an unshielded creature
	// impossible to express.
	const FGameplayAttribute MaxShield =
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute();
	if (AbilitySystemComponent->HasAttributeSetForAttribute(MaxShield))
	{
		const float Health = AbilitySystemComponent->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		const float Shield = FMath::Max(0.0f, Health * EnergyShieldFraction);

		AbilitySystemComponent->SetNumericAttributeBase(MaxShield, Shield);
		AbilitySystemComponent->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), Shield);
	}
}

void ACataclysmEnemyCharacter::ApplyIfHeld(const FGameplayAttribute& Attribute,
										   float Value)
{
	// THE CHECK IS THE WHOLE POINT OF THE HELPER. Writing to an attribute the
	// ability system does not hold yet raises an engine ensure rather than
	// failing quietly, and HasAttributeSetForAttribute is false until the
	// component has been initialised. ApplyStartingAttributes runs both from the
	// setters and from InitAbilityActorInfo, so it really does run before that
	// sometimes.
	if (AbilitySystemComponent
		&& AbilitySystemComponent->HasAttributeSetForAttribute(Attribute))
	{
		AbilitySystemComponent->SetNumericAttributeBase(Attribute, Value);
	}
}

void ACataclysmEnemyCharacter::SetArmour(float NewArmour)
{
	if (NewArmour < 0.0f)
	{
		return;
	}

	StartingArmour = NewArmour;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::SetEnergyShieldFraction(float NewFraction)
{
	if (NewFraction < 0.0f)
	{
		return;
	}

	EnergyShieldFraction = NewFraction;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::AttackTarget(AActor* Target)
{
	// The same path a player's skill takes: written into the Damage meta
	// attribute and resolved through the full mitigation order. An enemy with no
	// attack damage set deals nothing and says so once, which ApplyHit handles.
	UCataclysmSkillEffects::ApplyHit(this, Target, AttackPercentOfOwnDamage);
}

UAbilitySystemComponent* ACataclysmEnemyCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACataclysmEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Owner and avatar are both this actor, so there is no possession or
	// replication ordering to wait for. BeginPlay is sufficient on both sides.
	InitAbilityActorInfo();
}

void ACataclysmEnemyCharacter::InitAbilityActorInfo()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, this);

	// Now that the attribute sets are registered, whatever health and attack
	// damage a spawner asked for before this point can finally be written.
	ApplyStartingAttributes();

	if (HasAuthority() && StartingAbilitySet)
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystemComponent);
		StartingAbilitySet->GiveToAbilitySystem(AbilitySystemComponent, &GrantedHandles, this);
	}
}
