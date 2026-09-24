// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmGroundEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
// For walking the level's patches to answer whether a character is standing in
// one of its own. Issue #1162.
#include "EngineUtils.h"
#include "TimerManager.h"

ACataclysmGroundZone::ACataclysmGroundZone()
{
	// NOTHING TO DO PER FRAME, FOR ALMOST EVERY PATCH. It finds who is standing
	// in it on a timer a second apart, and ticking would ask sixty times as
	// often for the same answer.
	//
	// SO TICKING IS POSSIBLE AND OFF, RATHER THAN IMPOSSIBLE. A patch that
	// travels needs a per-frame step, and `bCanEverTick = false` cannot be
	// turned on later -- it is decided once, here. `TravelAt` enables it for
	// the one patch that asks. Issue #1649.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	// See the header. Without a root component the actor has no position and
	// every zone sweeps around the world origin.
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);
}

ACataclysmGroundZone* ACataclysmGroundZone::Spawn(
	AActor* Owner, const FVector& Location, float RadiusCm, float Duration,
	float DamagePerTick, FName InDamageType)
{
	// A circle is a path whose two ends are the same point.
	return SpawnAlong(Owner, Location, Location, RadiusCm, Duration, DamagePerTick,
					  /*bBurnsEveryone=*/false, InDamageType);
}

ACataclysmGroundZone* ACataclysmGroundZone::SpawnAlong(
	AActor* Owner, const FVector& Start, const FVector& End, float HalfWidthCm,
	float Duration, float DamagePerTick, bool bBurnsEveryone, FName InDamageType)
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
	// BEFORE FinishSpawning, because `BeginPlay` draws the patch inside it and
	// the type decides the colour of a patch with no `DrawnAsType`.
	Zone->DamageType = InDamageType;

	// AND NOW IT BEGINS PLAY, with everything above already set. This is the
	// second half of the deferred spawn and the whole reason for it: `BeginPlay`
	// asks to be drawn with the radius, the two ends and the remaining life span.
	Zone->FinishSpawning(Where);

	return Zone;
}

ACataclysmGroundZone* ACataclysmGroundZone::SpawnForTheFloor(
	AActor* Owner, const FVector& Start, const FVector& End, float HalfWidthCm,
	float DamagePerTick, bool bAffectsEveryone, FName InDrawnAsType,
	FName InDamageType)
{
	// NO DURATION TO REFUSE. The other two spawn functions check it because a
	// patch with no stated life would burn for nothing; this one has no stated
	// life by design and ends when the floor does.
	if (!IsValid(Owner) || HalfWidthCm <= 0.0f)
	{
		return nullptr;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// SPAWNED IN TWO STEPS, for the reason SpawnAlong gives at length:
	// UWorld::SpawnActor runs BeginPlay before it returns, and BeginPlay is
	// where a patch asks to be drawn and starts its timers. Everything it
	// reads has to be set before FinishSpawning.
	const FTransform Where(FRotator::ZeroRotator, Start);
	ACataclysmGroundZone* Zone = World->SpawnActorDeferred<ACataclysmGroundZone>(
		ACataclysmGroundZone::StaticClass(), Where, Owner, /*Instigator=*/nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Zone)
	{
		return nullptr;
	}

	Zone->RadiusCm = HalfWidthCm;
	Zone->DamagePerTick = DamagePerTick;
	Zone->bBurnsEveryone = bAffectsEveryone;
	Zone->bLastsTheFloor = true;
	Zone->FarEnd = Zone->GetActorLocation() + (End - Start);

	// BEFORE `FinishSpawning`, LIKE EVERY FIELD ABOVE IT, and the comment on the
	// deferred spawn says why: `BeginPlay` is where a patch asks to be drawn, and
	// it runs inside `FinishSpawning`. Set after it, the first drawing would be
	// the owner's colour and only the first redraw seconds later would be right.
	Zone->DrawnAsType = InDrawnAsType;
	// AND ITS DAMAGE TYPE, for the same reason: the first drawing reads it.
	Zone->DamageType = InDamageType;

	// AND NO SetLifeSpan AT ALL, WHICH IS THE WHOLE OF "LASTS THE FLOOR". The
	// floor changing is what ends this one; the declaration says which of two
	// functions destroys it.

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
	// THE COLOUR COMES FROM THE ZONE IF IT WAS GIVEN ONE AND FROM THE OWNER
	// OTHERWISE, and it is NAME_None for a player, so a zone a player leaves
	// draws the system's authored white. That is issue #803 and not a fault
	// here: a zone carries no skill tags of its own to read an Element.* tag
	// from, unlike UCataclysmStrikeSkill which does.
	//
	// THIS SAID "THE COLOUR COMES FROM THE OWNER" UNTIL `DrawnAsType` EXISTED,
	// and that was true of every zone in the game for as long as it was written.
	// `Pestilence_Fungal_Overgrowth` places two kinds of patch that share one
	// owner and have to be told apart, which is what made a per-zone colour
	// necessary. Issues #1820 and #41.
	// A TIMED PATCH IS DRAWN FOR ITS WHOLE LIFE SPAN, EXACTLY AS BEFORE. A
	// floor-lasting one has no life span, so GetLifeSpan() is zero and passing
	// it would draw nothing at all -- the patch would sweep, damage and curse
	// correctly while being invisible, which reads as working and is not.
	const float DrawSeconds = bLastsTheFloor ? FloorDrawSeconds : GetLifeSpan();

	Drawings = UCataclysmGroundEffect::PlayFor(
		this, GetActorLocation(), FarEnd, RadiusCm, DrawSeconds,
		TypeItIsDrawnAs());

	if (UWorld* World = GetWorld())
	{
		// First sweep a full tick in rather than at once, so that a zone left by
		// a skill that already hit everyone standing there does not hit them
		// twice in the same instant.
		World->GetTimerManager().SetTimer(
			SweepTimer, this, &ACataclysmGroundZone::Sweep,
			TickSeconds, /*bLoop=*/true, /*InFirstDelay=*/TickSeconds);

		// AND A FLOOR-LASTING PATCH ASKS TO BE DRAWN AGAIN BEFORE THE LAST
		// DRAWING ENDS. One drawing lasts FloorDrawSeconds and this fires every
		// FloorRedrawSeconds, which is shorter, so the two overlap rather than
		// meeting. See the two constants: making them equal leaves a frame with
		// nothing drawn and the patch blinks.
		if (bLastsTheFloor)
		{
			World->GetTimerManager().SetTimer(
				RedrawTimer, this, &ACataclysmGroundZone::Redraw,
				FloorRedrawSeconds, /*bLoop=*/true,
				/*InFirstDelay=*/FloorRedrawSeconds);
		}
	}
}

FName ACataclysmGroundZone::TypeItIsDrawnAs() const
{
	// ITS OWN COLOUR, THEN ITS OWN DAMAGE TYPE, THEN ITS OWNER'S. A patch given
	// neither is drawn exactly as every patch was before `DrawnAsType` existed. A
	// floor patch's type comes before its owner's because that owner, the floor's
	// shared hazard source, has none to give (issue #1924). See `DrawnAsType` for
	// why a patch may want a colour of its own.
	if (!DrawnAsType.IsNone())
	{
		return DrawnAsType;
	}
	return DamageType.IsNone()
		? UCataclysmSkillEffects::DamageTypeOf(GetOwner())
		: DamageType;
}

void ACataclysmGroundZone::Redraw()
{
	// THE ONES THAT HAVE ALREADY FINISHED ARE DROPPED FIRST. A patch lasting a
	// whole floor redraws every few seconds, and without this the list would
	// grow for as long as the floor lasts while almost every entry in it points
	// at a drawing that ended long ago.
	Drawings.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Weak)
	{
		return !Weak.IsValid();
	});

	Drawings.Append(UCataclysmGroundEffect::PlayFor(
		this, GetActorLocation(), FarEnd, RadiusCm, FloorDrawSeconds,
		TypeItIsDrawnAs()));

	// COUNTED SO A TEST CAN SEE IT. Nothing else about a drawing is observable
	// under the automation run's -nullrhi, so without this a patch that stopped
	// asking would be invisible in play and silent in the suite.
	++RedrawsAsked;
}

void ACataclysmGroundZone::TravelAt(const FVector& CentimetresPerSecond)
{
	TravelPerSecond = CentimetresPerSecond;

	// ONLY A PATCH THAT ACTUALLY MOVES TICKS. Setting this to zero turns it
	// back off, so a patch that stops travelling stops costing a frame.
	SetActorTickEnabled(!TravelPerSecond.IsNearlyZero());
}

void ACataclysmGroundZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TravelStep(DeltaSeconds);
}

void ACataclysmGroundZone::TravelStep(float StepSeconds)
{
	if (StepSeconds <= 0.0f || TravelPerSecond.IsNearlyZero())
	{
		return;
	}

	const FVector Delta = TravelPerSecond * StepSeconds;

	// NOT SWEPT AGAINST THE WORLD, AND THAT IS NOT AN OVERSIGHT. This actor's
	// root is a bare `USceneComponent` with no collision shape, so a swept move
	// would test nothing and simply cost more. What stops a patch leaving the
	// floor is whoever decides where to send it, not this.
	AddActorWorldOffset(Delta);

	// THE FAR END IS A WORLD POSITION, SO IT HAS TO BE CARRIED BY HAND. Moving
	// the actor alone would drag the near end away and leave the far end where
	// it was, stretching the shape instead of moving it -- and `IsLong` decides
	// by comparing the two, so a round patch that travelled would start
	// reporting itself as a long one.
	FarEnd += Delta;

	// AND THE VISUAL EFFECTS, WHICH ARE NOT ATTACHED TO THIS ACTOR.
	// `UCataclysmGroundEffect::PlayFor` spawns them at a location rather than
	// parenting them -- its header says so in as many words -- so a patch that
	// moved without this would slide out from under its own fire.
	for (const TWeakObjectPtr<UNiagaraComponent>& Drawing : Drawings)
	{
		if (Drawing.IsValid())
		{
			Drawing->AddWorldOffset(Delta);
		}
	}

	TravelledCm += Delta.Size();
}

void ACataclysmGroundZone::Sweep()
{
	// Named Source rather than Instigator: AActor already has a member of that
	// name, and shadowing it is an error at this project's warning level.
	AActor* Source = GetOwner();

	// WHAT THIS PATCH ACTUALLY DOES TO WHOEVER STANDS IN IT. Either or both.
	const bool bDamages = DamagePerTick > 0.0f;
	const bool bCurses = AppliedEffect.IsValid() && AppliedEffectSeconds > 0.0f;

	// A PATCH THAT DOES NEITHER IS SKIPPED, AND UNTIL ISSUE #1649 THE TEST WAS
	// ONLY ABOUT DAMAGE. A patch carrying a curse and no damage returned here
	// without ever asking who was inside, so it silently did nothing while
	// looking authored -- and `LastSweepCount` reported zero, which reads as
	// "nobody was standing in it" rather than "it never looked".
	//
	// SINGULARITY WELLS IS THE ROW THAT NEEDS THIS: "Pulsing void orbs pull
	// players and projectiles toward them, dealing void damage and slowing
	// movement by 40%." A well authored to slow without damaging would have
	// been the first thing to hit it.
	if (!IsValid(Source) || (!bDamages && !bCurses))
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
	// whoever cast it and burns the other side.
	//
	// NO PRODUCTION CODE SETS THIS FLAG, AND THIS COMMENT USED TO SAY OTHERWISE.
	// It read: "The Hellhound's lane burns whatever is standing in it, the
	// Hellhound included, which is the one thing in the design that asks for
	// it." The lane passes `/*bBurnsEveryone=*/false`. Checked one by one on
	// 2026-09-14: the player's skill ground, the Gatekeeper's, the Hellhound's
	// lane, Infernal Rain, Singularity Wells, Grasping Tentacles and Withered
	// Ground all take the default or pass false outright. The only place it is
	// set true is `CataclysmHellhoundTests.cpp`, which writes it on a lane to
	// exercise this branch.
	//
	// SO THE BRANCH IS REAL AND UNUSED, which is a different thing from dead:
	// the test above proves it works, and the first thing in the design to ask
	// for it is the dungeon rule `War_Artillery_Strike`, whose row says "Enemies
	// and players can be hit". THAT RULE DOES NOT SET THIS FLAG EITHER. The
	// circle it places deals no damage, so a flag on it would decide nothing;
	// the rule asks `FindEveryoneInLine` directly when the shell lands. Whether
	// the Hellhound's own lane should burn its own side is a question about the
	// Hellhound and is not answered here.
	const TArray<AActor*> Inside = bBurnsEveryone
		? UCataclysmTargeting::FindEveryoneInLine(
			GetWorld(), Source, GetActorLocation(), FarEnd, RadiusCm)
		: UCataclysmTargeting::FindEnemiesInLine(
			GetWorld(), Source, GetActorLocation(), FarEnd, RadiusCm);

	for (AActor* Target : Inside)
	{
		// AREA AND OVER TIME BOTH. A zone catches whatever is standing in it
		// rather than striking one target, so it cannot be evaded; and it is
		// damage over time, which restarts an energy shield's refill wait. Issue
		// #513. It is not a bleed, so the shield absorbs it (issue #2014).
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		Delivery.bIsDamageOverTime = true;
		// AND ITS OWN TYPE WHEN IT HAS ONE, which a floor rule's patch does. The
		// owner passed below is then the floor's shared hazard source, which
		// carries none. Issue #1924.
		Delivery.DamageType = DamageType;
		// ONLY IF THERE IS DAMAGE TO DEAL. A patch that only curses reaches here
		// now, and a hit of zero is still a hit: it would announce itself, count
		// towards anything that reacts to being struck, and read in a combat log
		// as an attack that did nothing.
		if (bDamages)
		{
			// THE FIRST SWEEP MAY DEAL ITS OWN FIGURE. Issue #1686. `TicksElapsed`
			// is still nought during it, because it moves after the sweep.
			const float ThisSweep = TicksElapsed == 0 && FirstSweepDamage >= 0.0f
				? FirstSweepDamage
				: DamagePerTick;
			UCataclysmSkillEffects::ApplyDirectDamage(Source, Target,
													  ThisSweep, Delivery);
		}

		// AND THE CURSE, IF THIS ZONE CARRIES ONE. The Wand's Foul Wake: "the
		// ground you fled ... strips the Demonic resistance of anything that
		// walks into it". Laid on every sweep, which refreshes rather than
		// stacks, so the curse runs its own duration from the moment the target
		// last stood here.
		if (bCurses)
		{
			UCataclysmSkillEffects::ApplyNamedEffect(
				Source, Target, AppliedEffect, AppliedEffectSeconds,
				AppliedEffectMagnitude, AppliedEffectDamageType);
		}
	}

	LastSweepCount = Inside.Num();
	++TicksElapsed;
}

void ACataclysmGroundZone::LifeSpanExpired()
{
	// RECORDED BEFORE THE BASE DESTROYS THIS, because the base call is what
	// reaches EndPlay, and EndPlay is where the flag is read.
	bExpiredNaturally = true;

	Super::LifeSpanExpired();
}

void ACataclysmGroundZone::EndPlay(const EEndPlayReason::Type Reason)
{
	// A PATCH THAT FINISHED ITS OWN LIFE LEAVES ITS DRAWINGS ALONE, which is
	// the whole of the recorded decision to spawn them detached: the fire burns
	// out rather than vanishing at the instant the actor goes.
	//
	// ANYTHING ELSE ENDS THEM. See the header: this is "expired, or anything
	// else", not "floor change or expiry". A patch cut short leaves fire
	// burning where it used to be, and on a floor change that is a place with
	// nothing in it, because everything else on that floor went in the same
	// instant. Issue #1660.
	if (!bExpiredNaturally)
	{
		UCataclysmGroundEffect::EndFor(Drawings);
	}

	Drawings.Reset();

	Super::EndPlay(Reason);
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
