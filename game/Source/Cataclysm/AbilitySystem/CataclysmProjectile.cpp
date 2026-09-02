// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmMeshWidth.h"
#include "AbilitySystem/CataclysmProjectileEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace
{
	/**
	 * A projectile with less than this left to travel has arrived.
	 *
	 * Compared against the remaining range rather than against zero so that
	 * floating point cannot leave one with a hundredth of a centimetre to go and
	 * another step to take.
	 */
	constexpr float CataclysmProjectileArrivedWithinCm = 0.1f;

	/**
	 * A cap on how much of one frame a single step may cover.
	 *
	 * A step is swept, so a long one is not a correctness problem in itself: the
	 * capsule still covers every place the projectile passed. It is a problem
	 * for the ORDER of what it passed. A projectile that does not pierce should
	 * stop at the first enemy and then not exist for the second, and a step long
	 * enough to contain both makes that ordering a property of the sweep rather
	 * than of time. A tenth of a second at the fastest designed speed -- Chain
	 * of Coals at 2000 centimetres per second -- is two metres.
	 */
	constexpr float CataclysmProjectileLongestStepSeconds = 0.1f;

	/**
	 * How long a finished projectile hangs about before being destroyed.
	 *
	 * NOT DESTROYED OUTRIGHT, so that whatever is listening on OnFinished can
	 * still read where it stopped. Short enough that a real game never has one
	 * of these on screen for a frame that matters.
	 */
	constexpr float CataclysmProjectileLingerSeconds = 0.01f;
}

ACataclysmProjectile::ACataclysmProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// NOT DECORATION, and this project has been caught by its absence twice. An
	// actor whose components are all non-scene components has no root component,
	// and an actor with no root component reports its location as the world
	// origin however it was spawned.
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);

	// A COMPONENT FOR A PROJECTILE THAT IS A PHYSICAL OBJECT, and nothing in it
	// by default. Scaled in Fire, once BodyRadiusCm is known -- a piercing
	// skill's projectile is as wide as the line it hits along, and everything
	// else uses the standard body width.
	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(Anchor);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// IT USED TO LOAD `/Engine/BasicShapes/Sphere` HERE AND THAT WAS THE SINGLE
	// WORST-LOOKING THING IN THE GAME. The engine sphere has no material
	// assigned, so it renders with Unreal's default grey. Every player fire bolt
	// was a grey ball with a particle effect stuck around it, which is what the
	// project owner meant on 2026-08-22 by "still just shooting regular looking
	// grey orbs with some mediocre effects on them". Issue #811.
	//
	// THE REASON IT WAS ADDED HAS GONE. The comment that stood here said
	// "SOMETHING TO SEE. Until this, every projectile in the game was
	// invisible", and that was true: the actor's only component was the anchor
	// and no effect existed. `NS_Proj_Body` was built in #558 and
	// `UCataclysmProjectileEffect::AttachTo` gives every projectile a head, a
	// trail and a light. The particle system IS the body of a magic bolt, and a
	// grey ball inside it is not a fallback, it is something drawn on top of the
	// art.
	//
	// A PROJECTILE THAT IS A REAL OBJECT STILL DRAWS A MESH, which is why the
	// component stays. `SetBodyMesh` is how the Brute's thrown rock arrives, and
	// a rock is a thing rather than a spell.
	//
	// WHAT THIS COSTS. A projectile whose effect is refused draws nothing at
	// all. `FXT_Enemy` refuses one past 4000 cm, outside the view frustum, or
	// beyond twenty live instances -- so what is lost is a grey ball in
	// situations where the effect was already judged not worth drawing.
}

void ACataclysmProjectile::SetBodyMesh(UStaticMesh* Mesh)
{
	if (!PlaceholderBody)
	{
		return;
	}

	if (Mesh)
	{
		PlaceholderBody->SetStaticMesh(Mesh);
	}

	const UStaticMesh* Shown = PlaceholderBody->GetStaticMesh();
	if (!Shown)
	{
		// NO MESH IS THE NORMAL CASE AND NOT A FAILURE. Since 2026-08-22 the
		// constructor loads nothing, so every projectile that was not handed a
		// real object arrives here with an empty component and is drawn entirely
		// by NS_Proj_Body. There is nothing to size. It also covers the Brute
		// throwing without the Paragon pack installed: no rock, no mesh, and the
		// throw still flies and still deals damage.
		return;
	}

	// FROM THE MESH'S OWN BOUNDS, so that a rock out of an art pack and an
	// engine primitive both come out at BodyRadiusCm. The arithmetic is shared
	// with the debris burst and with the rock the Brute carries, because the
	// three of them drawing the same asset at different sizes is issue #453.
	const float Scale = CataclysmMeshWidth::ScaleFor(Shown, BodyRadiusCm);
	if (Scale <= 0.0f)
	{
		// Nothing to scale: no width, or no radius asked for. Left alone rather
		// than collapsed to nothing.
		return;
	}

	PlaceholderBody->SetRelativeScale3D(FVector(Scale, Scale, Scale));
}

ACataclysmProjectile* ACataclysmProjectile::Fire(
	AActor* Instigator, const FVector& From, const FVector& To, float InRadiusCm,
	float InSpeed, int32 InPierce, bool bInReturns, float InDamagePercent,
	const FGameplayTagContainer& InSkillTags, bool bInBurns,
	UStaticMesh* InBodyMesh, float InFlightSeconds, float InCritChancePercent,
	float InSkillHealthCostPercent)
{
	UWorld* World = Instigator ? Instigator->GetWorld() : nullptr;
	if (!World || InRadiusCm <= 0.0f)
	{
		return nullptr;
	}
	if (InSpeed <= 0.0f && InFlightSeconds <= 0.0f)
	{
		// A speed of zero is a beam, not a projectile. Infernal Lance is written
		// that way and arrives at once, which the firing skill resolves itself.
		//
		// EITHER ONE WILL DO, because they are two ways of saying how quickly it
		// gets there and a lob is given the second rather than the first. A
		// caller passing neither has said nothing about how it moves.
		return nullptr;
	}

	FVector Along = To - From;
	Along.Z = 0.0f;
	const float RangeCm = Along.Size();
	if (RangeCm <= CataclysmProjectileArrivedWithinCm)
	{
		// Aimed at its own feet, which is what happens with no cursor. There is
		// no path to fly along, so the caller resolves it as a landing instead.
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Instigator;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmProjectile* Projectile = World->SpawnActor<ACataclysmProjectile>(
		From, Along.Rotation(), SpawnParams);
	if (!Projectile)
	{
		return nullptr;
	}

	Projectile->StartedAt = From;
	Projectile->FurthestReached = From;
	Projectile->Direction = Along / RangeCm;
	Projectile->RadiusCm = InRadiusCm;

	// REPLACED BELOW FOR A LOB, which works its ground speed out from its
	// flight time instead and does not use InSpeed at all.
	Projectile->SpeedCmPerSecond = InSpeed;
	Projectile->RemainingRangeCm = RangeCm;
	Projectile->bPierces = InPierce > 0;
	Projectile->PiercesLeft = FMath::Max(0, InPierce);

	// THE LOB, AND IT IS OFF UNLESS ASKED FOR. Issue #459. Direction above is
	// already flattened, so a projectile with no flight time flies exactly the
	// path it flew before this existed and every player skill is untouched.
	//
	// THE TWO HEIGHTS ARE KEPT BECAUSE A LOB HAS TO LAND SOMEWHERE. A straight
	// shot ignores the height difference between where it was fired and what it
	// was fired at, which is right for something travelling flat. One that
	// lobs has to end at the height it was aimed at, or the Brute's rock would
	// finish its parabola in the air at hand height.
	Projectile->FlightSeconds = FMath::Max(0.0f, InFlightSeconds);
	Projectile->TotalRangeCm = RangeCm;
	Projectile->LaunchZ = From.Z;
	Projectile->LandingZ = InFlightSeconds > 0.0f ? To.Z : From.Z;

	if (Projectile->FlightSeconds > 0.0f)
	{
		// THE WHOLE OF THE BALLISTIC SOLVE, AND IT IS TWO LINES. Issue #465.
		// Given two ends, a gravity and a time, the launch velocity is
		// `(delta - 0.5 * g * t * t) / t`. Unreal solves the same problem the
		// same way in UGameplayStatics::SuggestProjectileVelocity_MovingTarget,
		// at GameplayStatics.cpp:3140 in 5.8; it is not called here only
		// because that function insists on a live AActor* to aim at and this
		// aims at a point on the ground.
		//
		// SPLIT INTO THE TWO PARTS THIS CLASS ACTUALLY USES. The horizontal
		// part is a constant speed, which is what Step advances by. The
		// vertical part is folded into ApexHeightCm, because the height at any
		// point is worked out from the distance covered rather than integrated
		// step by step -- see ArcHeightAfter.
		Projectile->SpeedCmPerSecond = RangeCm / Projectile->FlightSeconds;

		// HOW FAR A PARABOLA SAGS BELOW ITS OWN CHORD, which over a flight of
		// t seconds is `g * t * t / 8` at the midpoint whatever the two ends
		// are. That makes the apex a function of the time alone: two lobs of
		// different lengths taking the same time rise the same height above
		// their chords.
		Projectile->ApexHeightCm = LobGravityCmPerSecondSquared
			* Projectile->FlightSeconds * Projectile->FlightSeconds / 8.0f;
	}
	else
	{
		Projectile->ApexHeightCm = 0.0f;
	}

	// A piercing skill's Radius is the half-width of the line it hits along, so
	// for one of those the flying object IS that wide. One that does not pierce
	// has a Radius that means the blast where it stops, so the object gets the
	// standard body width instead. See the comments on both fields.
	Projectile->BodyRadiusCm =
		Projectile->bPierces ? InRadiusCm : DefaultBodyRadiusCm;

	// SIZED TO WHAT IT ACTUALLY HITS WITH, so what the player sees and what the
	// sweep uses are the same width rather than two numbers that can disagree.
	// SetBodyMesh does both the swap and the sizing; a null mesh sizes the
	// placeholder sphere the constructor already put there.
	Projectile->SetBodyMesh(InBodyMesh);

	Projectile->bWillReturn = bInReturns;
	Projectile->DamagePercent = InDamagePercent;
	Projectile->SkillTags = InSkillTags;
	Projectile->CritChancePercent = InCritChancePercent;
	Projectile->SkillHealthCostPercent = InSkillHealthCostPercent;
	Projectile->bBurns = bInBurns;

	// LAST, AND THE ORDER MATTERS. The effect reads BodyRadiusCm for its size
	// and the firer for its colour, and the firer is the actor's Owner, which
	// SpawnActor set above. Attaching it any earlier would size every projectile
	// at the clamped minimum and colour every one of them white.
	//
	// ITS RETURN VALUE IS DELIBERATELY DROPPED. Null is the ordinary answer past
	// the effect type's cull distance, outside the view frustum, beyond twenty
	// live instances, and in every automation test, which runs with -nullrhi.
	// None of those is a reason not to fire the projectile.
	UCataclysmProjectileEffect::AttachTo(Projectile);

	UE_LOG(LogCataclysm, Verbose,
		TEXT("A projectile left %s at %.0f cm/s for %.0f cm, piercing %d."),
		*GetNameSafe(Instigator), InSpeed, RangeCm, InPierce);

	return Projectile;
}

void ACataclysmProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Step(DeltaSeconds);
}

bool ACataclysmProjectile::Step(float DeltaSeconds)
{
	if (bFinished || DeltaSeconds <= 0.0f)
	{
		return false;
	}

	// A long frame is taken as several steps rather than one, so that what a
	// projectile passes through, it passes through in order. See
	// CataclysmProjectileLongestStepSeconds.
	float SecondsLeft = DeltaSeconds;
	while (SecondsLeft > 0.0f && !bFinished)
	{
		const float ThisStep =
			FMath::Min(SecondsLeft, CataclysmProjectileLongestStepSeconds);
		SecondsLeft -= ThisStep;

		const FVector Previous = GetActorLocation();

		// THE SPEED IS ACROSS THE GROUND, AND IT IS THE SAME AT EVERY POINT OF
		// THE FLIGHT. Issue #465. That is the whole of what makes this
		// projectile motion rather than a shape being traced out: gravity acts
		// downward and nothing acts sideways, so the horizontal component of a
		// thrown object's velocity never changes and only the vertical one
		// does. `SpeedCmPerSecond` is already that number for both kinds of
		// shot -- given directly for a flat one, worked out from the flight
		// time in Fire for a lob.
		//
		// WHAT THIS REPLACED. #462 divided the step by `sqrt(1 + slope*slope)`
		// to hold the speed THROUGH THE AIR constant. That made the distance
		// flown correct and the distribution of it wrong: a steep part of the
		// path bought less ground than a shallow one, so the rock crossed most
		// of the way early and then sank onto the marker at 62% of the ground
		// speed it started with. The project owner reported it as throwing fast
		// and then dropping slowly.
		const float WantedCm =
			FMath::Min(SpeedCmPerSecond * ThisStep, RemainingRangeCm);

		FVector Wanted = Previous + Direction * WantedCm;

		// THE HEIGHT IS SET, NOT ADDED TO. Direction is flat, so the horizontal
		// part of the step above is the whole of the travel and an arcing
		// projectile's height is a function of how far along it is rather than
		// of where it was last frame. Working from the previous height instead
		// would accumulate every rounding error of the flight.
		//
		// ArcHeightAfter returns the launch height unchanged when nothing asked
		// for an arc, so this line does nothing at all to a straight shot.
		Wanted.Z = ArcHeightAfter(TotalRangeCm - RemainingRangeCm + WantedCm);

		// WORLD GEOMETRY FIRST, and only then who was standing there. A wall
		// between the caster and an enemy behind it has to stop the projectile
		// at the wall, so the sweep for enemies runs over the part of the step
		// that actually happened rather than the part that was asked for.
		const FVector Reached = TraceStep(Previous, Wanted);

		SetActorLocation(Reached);

		// THE HORIZONTAL DISTANCE, NOT THE DISTANCE THROUGH THE AIR. The range
		// a projectile is given is a distance across the ground -- the Brute's
		// rock reaches ten metres, meaning ten metres away, not ten metres of
		// flight. An arc is longer through the air than across the ground, so
		// counting the flown distance would land it short by the difference.
		//
		// IT CHANGES NOTHING FOR A STRAIGHT SHOT, because Direction is flattened
		// for every projectile, so its two distances are the same number.
		// Cataclysm.Skills.AStraightProjectileStillTravelsItsWholeRange is what
		// says so.
		RemainingRangeCm -= FVector::Dist2D(Previous, Reached);

		// Tracked as it goes, so that a projectile which turns round still
		// knows how far out it got. That is what the burning ground it leaves
		// is measured along.
		if (!bReturning)
		{
			FurthestReached = Reached;
		}

		if (HitAlongStep(Previous, Reached) || bFinished)
		{
			// Stopped on an enemy, or ran out of pierces part way through.
			Finish();
			return false;
		}
		if (bBlockedByGeometry)
		{
			Finish();
			return false;
		}
		if (RemainingRangeCm <= CataclysmProjectileArrivedWithinCm)
		{
			if (bWillReturn && !bReturning)
			{
				BeginReturn();
				continue;
			}
			Finish();
			return false;
		}
	}

	return !bFinished;
}

TArray<AActor*> ACataclysmProjectile::StruckEnemies() const
{
	TArray<AActor*> Struck;
	Struck.Reserve(AlreadyHit.Num());
	for (const TWeakObjectPtr<AActor>& One : AlreadyHit)
	{
		// HELD WEAKLY, because an enemy the bolt killed on its way through is
		// gone by the time anything asks. Dropping it here is why a caller does
		// not have to check.
		AActor* Alive = One.Get();
		if (IsValid(Alive))
		{
			Struck.Add(Alive);
		}
	}
	return Struck;
}

float ACataclysmProjectile::ArcHeightAfter(float HorizontalTravelledCm) const
{
	if (ApexHeightCm <= 0.0f || TotalRangeCm <= 0.0f)
	{
		// Not lobbing. Whatever height it was fired at, it keeps, which is what
		// this class did for every projectile before issue #459.
		return LaunchZ;
	}

	const float Along = FMath::Clamp(HorizontalTravelledCm / TotalRangeCm,
									 0.0f, 1.0f);

	// THE STRAIGHT LINE BETWEEN THE TWO ENDS, PLUS A PARABOLA ON TOP OF IT.
	// 4 * t * (1 - t) is zero at both ends and one in the middle, so the
	// parabola adds nothing at the launch or the landing and the full apex
	// halfway along. Splitting it this way is what lets the rock be thrown from
	// a hand well above the ground and still finish at the height it was aimed
	// at, rather than the arc being measured from one end.
	//
	// AND IT IS EXACTLY `z0 + vz*t - g*t*t/2`, WRITTEN IN THE OTHER VARIABLE.
	// Issue #465. Substituting a constant horizontal speed into the textbook
	// height-against-time turns it into this height-against-distance, with
	// ApexHeightCm standing for `g * flight * flight / 8`. So this line was
	// already real projectile motion before #465 and stayed unchanged by it;
	// what changed is that the projectile now walks it at a constant ground
	// speed, which is the other half of what makes a trajectory ballistic.
	const float Straight = FMath::Lerp(LaunchZ, LandingZ, Along);
	return Straight + ApexHeightCm * 4.0f * Along * (1.0f - Along);
}

FVector ACataclysmProjectile::TraceStep(const FVector& From, const FVector& To)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return To;
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(CataclysmProjectileFlight),
								/*bInTraceComplex=*/false);
	Query.AddIgnoredActor(this);
	if (AActor* Firer = GetOwner())
	{
		Query.AddIgnoredActor(Firer);
	}

	// THE VISIBILITY CHANNEL, because it is the one that answers "does something
	// solid stand between these two points". Pawns deliberately do not block it,
	// so one character never counts as cover for the character behind them: who
	// a projectile passes through is decided by Pierce, not by collision.
	FHitResult Blocked;
	if (World->LineTraceSingleByChannel(Blocked, From, To, ECC_Visibility, Query))
	{
		bBlockedByGeometry = true;
		return Blocked.ImpactPoint;
	}
	return To;
}

bool ACataclysmProjectile::HitAlongStep(const FVector& Previous,
										const FVector& Current)
{
	AActor* Firer = GetOwner();
	if (!Firer)
	{
		return false;
	}

	// THE CAPSULE FROM WHERE IT WAS TO WHERE IT IS, not a sphere at where it is.
	// That is the volume the projectile actually swept, so nothing thin enough
	// to sit between two steps is missed. FindEnemiesInLine returns them sorted
	// from the start of the segment, which is the order the projectile reached
	// them and therefore the order a non-piercing one must consider them in.
	const TArray<AActor*> Along = UCataclysmTargeting::FindEnemiesInLine(
		GetWorld(), Firer, Previous, Current, BodyRadiusCm);

	for (AActor* Target : Along)
	{
		if (AlreadyHit.Contains(Target))
		{
			continue;
		}
		AlreadyHit.Add(Target);

		if (!bPierces)
		{
			// AN AXE THAT GLANCES HITS THIS ONE AND TURNS, rather than stopping.
			// The Axe's Carom: "it glances from them to the next nearest and
			// onward through three more". A glance is not a pierce: a piercing
			// bolt carries straight on and never detonates, while this turns and
			// keeps its detonation for wherever it finally stops.
			if (BouncesLeft > 0)
			{
				HitOne(Target);
				--BouncesLeft;
				++BouncesMade;

				// AND IT NO LONGER GOES OFF WHERE IT STOPS. It has struck what
				// it touched, so the blast a non-piercing projectile makes would
				// hit the last enemy a second time.
				bDetonatesWhenItStops = false;

				// "Every enemy it touches after the first adds 20% to its
				// damage." Raised after the hit, so the enemy that was struck
				// pays the old figure and the next pays the new one.
				DamagePercent += BounceDamagePercentPer;

				if (GlanceOnwardFrom(Target->GetActorLocation()))
				{
					return false;
				}

				// NOTHING LEFT TO GLANCE TO, so the throw is over. Its remaining
				// range is set to nothing rather than finishing outright, so a
				// throw that returns still comes back: Carom "glances ... onward
				// through three more ... then returns to your hand".
				RemainingRangeCm = 0.0f;
				return false;
			}

			// AN AXE THAT HAS ALREADY GLANCED HITS BY TOUCHING, so the enemy it
			// reaches with no glances left is struck here and the throw ends.
			// Three glances touch four enemies, which is what Carom's "onward
			// through three more" means. A test caught the fourth being missed
			// when the blast was turned off and nothing replaced it.
			//
			// ITS RANGE IS SET TO NOTHING RATHER THAN FINISHING OUTRIGHT, so a
			// throw that returns still comes back to the hand.
			if (!bDetonatesWhenItStops)
			{
				HitOne(Target);
				RemainingRangeCm = 0.0f;
				return false;
			}

			// It stops here. The hit itself happens in Finish, which detonates
			// in a radius and catches this target along with anything standing
			// near it, so hitting it now as well would hit it twice.
			return true;
		}

		HitOne(Target);

		--PiercesLeft;
		if (PiercesLeft <= 0)
		{
			// Out of pierces. It has already hit this one on the way through, so
			// it stops here, and a piercing projectile does not detonate.
			bFinished = true;
			return false;
		}
	}

	return false;
}

void ACataclysmProjectile::HitOne(AActor* Target)
{
	AActor* Firer = GetOwner();
	if (!Firer || !Target)
	{
		return;
	}

	// WHETHER THIS CAN BE EVADED COMES FROM SkillTags, which is carried from the
	// skill that fired it. Emberhurl is `Type.Projectile` and nothing else, so it
	// is a direct hit; a projectile whose skill is tagged `Type.AOE.PointBlank`
	// detonates and cannot be evaded. Issue #513.
	// AND ITS CRITICAL STRIKE CHANCE IS THE FIRING SKILL'S, carried the same way
	// and for the same reason: a projectile lands after the skill that fired it
	// has finished, so anything read off the character at that moment could belong
	// to a different skill. -1 means the skill states none, which is every skill
	// today and every enemy attack. Issue #657.
	FCataclysmHitDelivery Delivery;
	Delivery.CritChancePercent = CritChancePercent;

	// AND WHAT THE FIRING SKILL COST, carried since it was fired for the same
	// reason the chance above is. A projectile lands after the skill that
	// fired it has finished, so asking the character at impact would credit
	// this blow with whatever was last paid. Issue #983.
	Delivery.SkillHealthCostPercent = SkillHealthCostPercent;

	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Firer, Target, DamagePercent, SkillTags, Delivery);
	if (Dealt > 0.0f)
	{
		++EnemiesHit;

		// A DESIGNED BURN, because `bBurns` is the firing row's own `Burn=1`.
		// Issue #917: a skill that states an ailment applies it whether or not
		// the blow hurt, and only an incidental one has to clear the damage
		// threshold.
		//
		// THE COMMENT THAT STOOD HERE SAID A BURN IS A SHARE OF THE HIT. That
		// stopped being true on 2026-08-24, when burn became a flat 25 a second.
		if (bBurns)
		{
			UCataclysmSkillEffects::ApplyBurn(Firer, Target, Dealt,
											  /*bScalesWithInstigator=*/true,
											  /*bBurnIsDesigned=*/true);
		}
	}
}

void ACataclysmProjectile::GlancesOnward(int32 InBounces, float InReachCm,
										float InDamagePercentPer)
{
	BouncesLeft = FMath::Max(0, InBounces);
	BounceReachCm = FMath::Max(0.0f, InReachCm);
	BounceDamagePercentPer = InDamagePercentPer;
}

bool ACataclysmProjectile::GlanceOnwardFrom(const FVector& At)
{
	AActor* Firer = GetOwner();
	if (!Firer || BounceReachCm <= 0.0f)
	{
		return false;
	}

	// NEAREST TO WHERE IT JUST LANDED, not to the caster. "It glances from them
	// to the next nearest" describes a chain travelling outward through a group,
	// which is what makes Carom worth throwing into a crowd.
	for (AActor* Candidate : UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), Firer, At, BounceReachCm))
	{
		if (AlreadyHit.Contains(Candidate))
		{
			continue;
		}

		// THE SAME THREE THINGS `BeginReturn` SETS, for the same reason: a
		// direction to travel, how far is left, and which way to face.
		const FVector Toward = Candidate->GetActorLocation() - At;
		const FVector Flat = FVector(Toward.X, Toward.Y, 0.0f);
		if (Flat.IsNearlyZero())
		{
			continue;
		}

		Direction = Flat.GetSafeNormal();
		RemainingRangeCm = Flat.Size();
		SetActorRotation(Direction.Rotation());
		return true;
	}

	return false;
}

void ACataclysmProjectile::BeginReturn()
{
	bReturning = true;
	Direction = -Direction;
	RemainingRangeCm = FVector::Dist(GetActorLocation(), StartedAt);

	// CLEARED, because Emberhurl "hits once going out and once returning to your
	// hand". Everything it passed on the way out is a legal target again on the
	// way back.
	AlreadyHit.Reset();

	SetActorRotation(Direction.Rotation());
}

void ACataclysmProjectile::Finish()
{
	if (bFinishedAndSettled)
	{
		return;
	}
	bFinishedAndSettled = true;
	bFinished = true;
	SetActorTickEnabled(false);

	// A PROJECTILE THAT DOES NOT PIERCE DETONATES WHERE IT STOPPED, and where it
	// stopped is now a real place rather than where it was aimed. Blood Pyre and
	// Magma Quake go off against the first thing they touch, against the wall
	// that blocked them, or at the end of the throw if they touch nothing.
	if (!bPierces && bDetonatesWhenItStops)
	{
		if (AActor* Firer = GetOwner())
		{
			for (AActor* Target : UCataclysmTargeting::FindEnemiesInSphere(
					GetWorld(), Firer, GetActorLocation(), RadiusCm))
			{
				HitOne(Target);
			}
		}
	}

	OnFinished.Broadcast(this);

	// Kept for a moment rather than destroyed here, so that whatever listened
	// can still read where it stopped in order to leave burning ground there.
	SetLifeSpan(CataclysmProjectileLingerSeconds);
}
