// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmGroundEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
// For walking the level's patches to answer whether a character is standing in
// one of its own. Issue #1162.
#include "EngineUtils.h"
#include "TimerManager.h"

ACataclysmGroundZone::ACataclysmGroundZone()
{
	// Nothing to do per frame. It sweeps on a timer, a second apart, and a tick
	// would run it sixty times more often for the same result.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// See the header. Without a root component the actor has no position and
	// every zone sweeps around the world origin.
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);
}

ACataclysmGroundZone* ACataclysmGroundZone::Spawn(
	AActor* Owner, const FVector& Location, float RadiusCm, float Duration,
	float DamagePerTick)
{
	// A circle is a path whose two ends are the same point.
	return SpawnAlong(Owner, Location, Location, RadiusCm, Duration, DamagePerTick);
}

ACataclysmGroundZone* ACataclysmGroundZone::SpawnAlong(
	AActor* Owner, const FVector& Start, const FVector& End, float HalfWidthCm,
	float Duration, float DamagePerTick, bool bBurnsEveryone)
{
	if (!IsValid(Owner) || HalfWidthCm <= 0.0f || Duration <= 0.0f)
	{
		return nullptr;
	}

	const FVector Location = Start;
	const float RadiusCm = HalfWidthCm;

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// SPAWNED IN TWO STEPS, AND A ONE-STEP SPAWN IS WRONG HERE IN A WAY THAT IS
	// SILENT. Issue #1153. `UWorld::SpawnActor` runs `BeginPlay` before it
	// returns, in any world that has already begun play -- which is every world
	// the game runs in. So every property set on the lines after it was set too
	// late for `BeginPlay` to see, and `BeginPlay` is where the zone asks to be
	// drawn. Every patch of burning ground in the game was drawn with a radius of
	// zero, a far end at the world origin and a duration of nothing. Twenty-two
	// rows leave burning ground, so that was all of them.
	//
	// THE DAMAGE WAS NEVER AFFECTED, WHICH IS WHY IT LASTED. `Sweep` reads the
	// radius, the far end and the damage when its timer fires, long after this
	// function has returned, so standing in a patch always burned for the right
	// amount over the right area. Only the drawing ran at `BeginPlay`.
	//
	// PROVEN ON A DIFFERENT ACTOR FIRST. `ACataclysmTerrain::Spawn` copied this
	// pattern, its wall raised no geometry at all, and switching it to a deferred
	// spawn fixed it. That comment records the same reasoning.
	const FTransform Where(FRotator::ZeroRotator, Location);
	ACataclysmGroundZone* Zone = World->SpawnActorDeferred<ACataclysmGroundZone>(
		ACataclysmGroundZone::StaticClass(), Where, Owner, /*Instigator=*/nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Zone)
	{
		return nullptr;
	}

	Zone->RadiusCm = RadiusCm;
	Zone->DamagePerTick = DamagePerTick;
	Zone->bBurnsEveryone = bBurnsEveryone;

	// Read back from the actor rather than trusting Start, so the near end is
	// wherever the actor actually is and the two ends cannot disagree. A deferred
	// spawn has not reached the point where a position would be adjusted, and
	// `AlwaysSpawn` does no adjusting anyway, so this is exact rather than a
	// guard now. It is written this way so this actor and `ACataclysmTerrain`
	// read alike.
	Zone->FarEnd = Zone->GetActorLocation() + (End - Start);

	Zone->SetLifeSpan(Duration);

	// AND NOW IT BEGINS PLAY, with everything above already set. This is the
	// second half of the deferred spawn and the whole reason for it: `BeginPlay`
	// asks to be drawn with the radius, the two ends and the remaining life span.
	Zone->FinishSpawning(Where);

	return Zone;
}

void ACataclysmGroundZone::BeginPlay()
{
	Super::BeginPlay();

	// DRAWN, WHICH UNTIL ISSUE #811 IT WAS NOT. Every patch of burning ground in
	// the game was invisible: this class had a scene component and nothing else,
	// and its own header said so. Eight of the sixteen designed Demonic skills
	// leave one, and a player could stand in any of them and see nothing.
	//
	// HERE RATHER THAN IN Spawn AND SpawnAlong, because both of those end at the
	// same actor and doing it once is one place to get wrong instead of two. It
	// also means a zone placed in a level by hand draws as well as one a skill
	// left.
	//
	// THE COLOUR COMES FROM THE OWNER AND IS NAME_None FOR A PLAYER, so a zone a
	// player leaves draws the system's authored white. That is issue #803 and
	// not a fault here: a zone carries no skill tags of its own to read an
	// Element.* tag from, unlike UCataclysmStrikeSkill which does.
	UCataclysmGroundEffect::PlayFor(this, GetActorLocation(), FarEnd, RadiusCm,
									GetLifeSpan(),
									UCataclysmSkillEffects::DamageTypeOf(GetOwner()));

	if (UWorld* World = GetWorld())
	{
		// First sweep a full tick in rather than at once, so that a zone left by
		// a skill that already hit everyone standing there does not hit them
		// twice in the same instant.
		World->GetTimerManager().SetTimer(
			SweepTimer, this, &ACataclysmGroundZone::Sweep,
			TickSeconds, /*bLoop=*/true, /*InFirstDelay=*/TickSeconds);
	}
}

void ACataclysmGroundZone::Sweep()
{
	// Named Source rather than Instigator: AActor already has a member of that
	// name, and shadowing it is an error at this project's warning level.
	AActor* Source = GetOwner();
	if (!IsValid(Source) || DamagePerTick <= 0.0f)
	{
		LastSweepCount = 0;
		return;
	}

	// Asked afresh every sweep. Standing in it is the cost, so who is inside has
	// to be a question about now rather than about when it was created.
	//
	// ONE SEARCH FOR BOTH SHAPES. FindEnemiesInLine with two ends at the same
	// point is a circle of RadiusCm at that point, because IsInLine treats a
	// segment of no length that way, so a round zone and a long one cannot drift
	// apart in behaviour.
	// WHICH SEARCH DEPENDS ON WHOSE FIRE IT IS. Almost every zone belongs to
	// whoever cast it and burns the other side. The Hellhound's lane burns
	// whatever is standing in it, the Hellhound included, which is the one
	// thing in the design that asks for it.
	const TArray<AActor*> Inside = bBurnsEveryone
		? UCataclysmTargeting::FindEveryoneInLine(
			GetWorld(), Source, GetActorLocation(), FarEnd, RadiusCm)
		: UCataclysmTargeting::FindEnemiesInLine(
			GetWorld(), Source, GetActorLocation(), FarEnd, RadiusCm);

	for (AActor* Target : Inside)
	{
		// AREA AND OVER TIME BOTH. A zone catches whatever is standing in it
		// rather than striking one target, so it cannot be evaded; and it is
		// damage over time, so an energy shield does not absorb it. Issue #513.
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		Delivery.bIsDamageOverTime = true;
		UCataclysmSkillEffects::ApplyDirectDamage(Source, Target, DamagePerTick,
												  Delivery);

		// AND THE CURSE, IF THIS ZONE CARRIES ONE. The Wand's Foul Wake: "the
		// ground you fled ... strips the Demonic resistance of anything that
		// walks into it". Laid on every sweep, which refreshes rather than
		// stacks, so the curse runs its own duration from the moment the target
		// last stood here.
		if (AppliedEffect.IsValid() && AppliedEffectSeconds > 0.0f)
		{
			UCataclysmSkillEffects::ApplyNamedEffect(
				Source, Target, AppliedEffect, AppliedEffectSeconds,
				AppliedEffectMagnitude, AppliedEffectDamageType);
		}
	}

	LastSweepCount = Inside.Num();
	++TicksElapsed;
}

void ACataclysmGroundZone::AlsoApply(FGameplayTag EffectTag, float Seconds,
									 float Magnitude, FName InDamageType)
{
	AppliedEffect = EffectTag;
	AppliedEffectSeconds = Seconds;
	AppliedEffectMagnitude = Magnitude;
	AppliedEffectDamageType = InDamageType;
}

void ACataclysmGroundZone::AlsoHealItsOwner(float Scale)
{
	// A SCALE OF ONE OR LESS IS NOT RECORDED, so `RegenerationScaleFor` can skip
	// a zone by reading one field. Every patch in the game but Blood Pyre's is
	// in that state and never asks the geometry question below.
	OwnersRegenerationScale = FMath::Max(1.0f, Scale);
}

bool ACataclysmGroundZone::Covers(const FVector& Point) const
{
	// THE SAME TEST THE SWEEP MAKES, and deliberately the same one:
	// `FindEnemiesInLine` and `FindEveryoneInLine` both decide who is inside
	// with `IsInLine`, and a segment of no length is a circle at that point. So
	// a round patch and a long one cannot disagree about their own extent, and
	// what heals the owner covers exactly the ground that burns everybody else.
	return UCataclysmTargeting::IsInLine(GetActorLocation(), FarEnd, Point,
										 RadiusCm);
}

float ACataclysmGroundZone::RegenerationScaleFor(const AActor* Who)
{
	if (!IsValid(Who))
	{
		return 1.0f;
	}

	UWorld* World = Who->GetWorld();
	if (!World)
	{
		return 1.0f;
	}

	float Scale = 1.0f;
	for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
	{
		const ACataclysmGroundZone* Zone = *It;

		// ITS OWN, WHICH IS THE WHOLE OF "YOUR OWN PYRE". A patch somebody else
		// left heals nobody, including whoever is standing in it.
		//
		// THE SCALE IS READ BEFORE THE GEOMETRY, so every other patch in the
		// level costs one float comparison rather than a containment test.
		if (!IsValid(Zone) || Zone->OwnersRegenerationScale <= 1.0f
			|| Zone->GetOwner() != Who)
		{
			continue;
		}

		if (Zone->Covers(Who->GetActorLocation()))
		{
			// THE LARGEST RATHER THAN THE PRODUCT. See the header: two patches
			// that each promise "doubles" do not promise a quadrupling.
			Scale = FMath::Max(Scale, Zone->OwnersRegenerationScale);
		}
	}

	return Scale;
}
