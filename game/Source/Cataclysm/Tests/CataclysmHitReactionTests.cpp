// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmHitReaction.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Issue #745: a creature that is hit flinches, away from the blow, unless it is busy.
 *
 * WHAT WAS WRONG. A creature that was hit showed nothing: it kept walking, or kept standing, in the pose it was in,
 * and only the floating number and the impact burst said the blow had landed. All seven Demonic creatures' packs ship
 * four directional hit-reaction clips and nothing loaded them.
 *
 * WHAT THESE GUARD, ruled 2026-11-02 by the coordinating session under the owner's delegation:
 *
 *   (a) a flinch never cancels or delays a wind-up or an attack, and none starts during one;
 *   (b) no flinch starts while one is playing -- the clip's own length is the rate limit;
 *   (c) the clip is the nearest of four quarters to the side the blow came from;
 *   (d) a damage-over-time tick never flinches, only a landed hit;
 *   (e) a dead, stunned, knocked-down or charging creature does not flinch.
 *
 * EVERY BLOW HERE IS A REAL ONE. A plain creature on the players' side swings at the creature under test through
 * `ACataclysmEnemyCharacter::AttackTarget`, which is `UCataclysmSkillEffects::ApplyHit`, so the flinch is reached
 * through `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` -- the hook -- and not by calling `FlinchFrom`.
 *
 * WITHOUT THE PARAGON ART NO CLIP PLAYS, and the choice is still recorded: which direction, and which clip's path.
 * Those are checked everywhere. That a clip loaded and holds the body is checked only with the art, and a test that
 * could not check it says so through `CataclysmTestSkip::ReportSkippedHalf`.
 *
 * NO WORLD IS TICKED. A world built by `UWorld::CreateWorld` never is, so a test moves the world clock by hand. Only
 * the damage-over-time test runs the timer manager, because a tick is a timer; it stands the striker far beyond every
 * creature's notice so the brain's own timer, which runs too, finds nobody.
 */

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

namespace CataclysmHitReactionTest
{
	/** Where a striker stands from the creature it strikes, and far enough that no two fixtures touch. */
	constexpr float StrikerStandsCm = 300.0f;

	/** How far apart two fixtures are placed, so no striker stands nearer another fixture than its own. */
	constexpr float FixturesApartCm = 1500.0f;

	/** Longer than any flinch clip a Paragon pack ships, so a clip that played is over. */
	constexpr double LongerThanAnyFlinchSeconds = 5.0;

	static void PushTheClockOn(UWorld* World, double Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	static float WorldNowIn(const UWorld* World)
	{
		return static_cast<float>(World->GetTimeSeconds());
	}

	/**
	 * Nothing a blow could miss by or be soaked by: evasion and block chance written to zero AFTER the health,
	 * because `SetHealth` re-runs `ApplyStartingAttributes`, which writes the creature's designed evasion.
	 */
	static void LeaveNothingToMiss(ACataclysmEnemyCharacter* Creature)
	{
		if (UAbilitySystemComponent* System = Creature ? Creature->GetAbilitySystemComponent() : nullptr)
		{
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
		}
	}

	/** The creature under test, facing +X, with health no blow here can take. */
	static ACataclysmEnemyCharacter* SpawnCreatureToStrike(UWorld* World, UClass* Class, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			Class, At, FRotator::ZeroRotator, Params);
		if (Creature)
		{
			Creature->SetHealth(1000000.0f);
			LeaveNothingToMiss(Creature);
		}
		return Creature;
	}

	/**
	 * Whoever strikes: a plain creature on the players' side, whose ordinary attack is one blow dealt at once. It
	 * names no hit-reaction clips, so it never flinches itself.
	 */
	static ACataclysmEnemyCharacter* SpawnFlinchStriker(UWorld* World, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmEnemyCharacter* Striker = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), At, FRotator::ZeroRotator, Params);
		if (Striker)
		{
			Striker->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Striker->SetAttackDamage(100.0f);
		}
		return Striker;
	}

	/** One landed blow: the striker's ordinary attack, the same `ApplyHit` every blow in the game is. */
	static void LandABlow(ACataclysmEnemyCharacter* Striker, AActor* Struck)
	{
		Striker->ACataclysmEnemyCharacter::AttackTarget(Struck);
	}

	static float HealthLeftOn(const ACataclysmEnemyCharacter* Creature)
	{
		const UAbilitySystemComponent* System = Creature ? Creature->GetAbilitySystemComponent() : nullptr;
		return System
			? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
			: 0.0f;
	}

	/** What a swing is aimed at in the two tests that order one: on the players' side, so the blow may land. */
	static ACataclysmEnemyCharacter* SpawnSomethingToSwingAt(UWorld* World, const FVector& At)
	{
		ACataclysmEnemyCharacter* Target = SpawnCreatureToStrike(World, ACataclysmEnemyCharacter::StaticClass(), At);
		if (Target)
		{
			Target->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		}
		return Target;
	}

	/** A direction in the floor plane, in degrees from +X toward +Y: from a creature facing +X, toward its right. */
	static FVector FloorDirectionAtDegrees(float Degrees)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0f);
	}

	/** One of the seven Demonic creatures and the four clip names it should choose, front, back, left, right. */
	struct FHitReactionFixtureRow
	{
		UClass* Class = nullptr;
		const TCHAR* Who = nullptr;
		const TCHAR* Names[UCataclysmHitReaction::DirectionCount] = {};
	};
}

CATACLYSM_TEST(FCataclysmHitReactionDirectionTest,
	"Cataclysm.HitReaction.TheDirectionIsTheNearestOfFourQuarters")
{
	using namespace CataclysmHitReactionTest;
	using Rule = UCataclysmHitReaction;

	auto Expect = [this](const TCHAR* What, const FVector& Facing, const FVector& ToSource,
						 ECataclysmHitDirection Want)
	{
		TestEqual(What, static_cast<int32>(Rule::DirectionOf(Facing, ToSource)), static_cast<int32>(Want));
	};

	const FVector AlongX(1.0f, 0.0f, 0.0f);

	// THE FOUR STRAIGHT LINES.
	Expect(TEXT("straight ahead is front"), AlongX, FVector(1.0f, 0.0f, 0.0f), ECataclysmHitDirection::Front);
	Expect(TEXT("straight behind is back"), AlongX, FVector(-1.0f, 0.0f, 0.0f), ECataclysmHitDirection::Back);
	Expect(TEXT("+Y beside a creature facing +X is its right"), AlongX, FVector(0.0f, 1.0f, 0.0f),
		ECataclysmHitDirection::Right);
	Expect(TEXT("-Y beside a creature facing +X is its left"), AlongX, FVector(0.0f, -1.0f, 0.0f),
		ECataclysmHitDirection::Left);

	// EITHER SIDE OF EVERY BOUNDARY, A DEGREE AWAY.
	Expect(TEXT("44 degrees right of ahead is still front"), AlongX, FloorDirectionAtDegrees(44.0f), ECataclysmHitDirection::Front);
	Expect(TEXT("46 degrees right of ahead is right"), AlongX, FloorDirectionAtDegrees(46.0f), ECataclysmHitDirection::Right);
	Expect(TEXT("44 degrees left of ahead is still front"), AlongX, FloorDirectionAtDegrees(-44.0f), ECataclysmHitDirection::Front);
	Expect(TEXT("46 degrees left of ahead is left"), AlongX, FloorDirectionAtDegrees(-46.0f), ECataclysmHitDirection::Left);
	Expect(TEXT("134 degrees right of ahead is still right"), AlongX, FloorDirectionAtDegrees(134.0f),
		ECataclysmHitDirection::Right);
	Expect(TEXT("136 degrees right of ahead is back"), AlongX, FloorDirectionAtDegrees(136.0f), ECataclysmHitDirection::Back);
	Expect(TEXT("134 degrees left of ahead is still left"), AlongX, FloorDirectionAtDegrees(-134.0f),
		ECataclysmHitDirection::Left);
	Expect(TEXT("136 degrees left of ahead is back"), AlongX, FloorDirectionAtDegrees(-136.0f), ECataclysmHitDirection::Back);

	// EXACTLY ON THE FOUR DIAGONALS, WHICH GO TO FRONT OR BACK. Exact in floating point: both projections are 1.
	Expect(TEXT("exactly front-right goes to front"), AlongX, FVector(1.0f, 1.0f, 0.0f),
		ECataclysmHitDirection::Front);
	Expect(TEXT("exactly front-left goes to front"), AlongX, FVector(1.0f, -1.0f, 0.0f),
		ECataclysmHitDirection::Front);
	Expect(TEXT("exactly back-right goes to back"), AlongX, FVector(-1.0f, 1.0f, 0.0f),
		ECataclysmHitDirection::Back);
	Expect(TEXT("exactly back-left goes to back"), AlongX, FVector(-1.0f, -1.0f, 0.0f),
		ECataclysmHitDirection::Back);

	// NOTHING TO JUDGE IS FRONT.
	Expect(TEXT("a source standing where the creature stands is front"), AlongX, FVector::ZeroVector,
		ECataclysmHitDirection::Front);
	Expect(TEXT("a creature with no facing hears every blow as front"), FVector::ZeroVector,
		FVector(0.0f, -1.0f, 0.0f), ECataclysmHitDirection::Front);

	// HEIGHT IS NOT A DIRECTION.
	Expect(TEXT("a blow from above and to the left is left"), AlongX, FVector(0.0f, -1.0f, 5.0f),
		ECataclysmHitDirection::Left);
	Expect(TEXT("a blow from straight above is front"), AlongX, FVector(0.0f, 0.0f, 100.0f),
		ECataclysmHitDirection::Front);

	// THE CREATURE'S FACING, NOT THE WORLD'S AXES. Facing +Y, its right is -X.
	const FVector AlongY(0.0f, 1.0f, 0.0f);
	Expect(TEXT("facing +Y, +X is its left"), AlongY, FVector(1.0f, 0.0f, 0.0f), ECataclysmHitDirection::Left);
	Expect(TEXT("facing +Y, -X is its right"), AlongY, FVector(-1.0f, 0.0f, 0.0f), ECataclysmHitDirection::Right);
	Expect(TEXT("facing +Y, -Y is its back"), AlongY, FVector(0.0f, -1.0f, 0.0f), ECataclysmHitDirection::Back);

	// NEITHER VECTOR NEED BE UNIT LENGTH: the actor's forward is, the gap to the striker is not.
	Expect(TEXT("a long facing and a long gap still read left"), FVector(5.0f, 0.0f, 0.0f),
		FVector(0.0f, -300.0f, 0.0f), ECataclysmHitDirection::Left);
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionRefusalOrderTest,
	"Cataclysm.HitReaction.TheFirstRefusalThatAppliesIsTheOneReported")
{
	using Rule = UCataclysmHitReaction;

	auto Expect = [this](const TCHAR* What, const FCataclysmFlinchFacts& Facts, ECataclysmFlinchRefusal Want)
	{
		TestEqual(What, static_cast<int32>(Rule::RefusalFor(Facts)), static_cast<int32>(Want));
	};

	FCataclysmFlinchFacts Plain;
	Expect(TEXT("a creature naming no clips never flinches"), Plain, ECataclysmFlinchRefusal::NoHitReactions);

	FCataclysmFlinchFacts Idle;
	Idle.bHasHitReactions = true;
	Expect(TEXT("an idle creature with clips flinches"), Idle, ECataclysmFlinchRefusal::None);

	FCataclysmFlinchFacts Dead = Idle;
	Dead.bDead = true;
	Expect(TEXT("a dead one does not"), Dead, ECataclysmFlinchRefusal::Dead);

	FCataclysmFlinchFacts Held = Idle;
	Held.bCannotAct = true;
	Expect(TEXT("nor a stunned or knocked-down one"), Held, ECataclysmFlinchRefusal::CannotAct);

	FCataclysmFlinchFacts Charging = Idle;
	Charging.bCharging = true;
	Expect(TEXT("nor one mid-charge"), Charging, ECataclysmFlinchRefusal::Charging);

	FCataclysmFlinchFacts WindingUp = Idle;
	WindingUp.bWindingUp = true;
	Expect(TEXT("nor one winding up"), WindingUp, ECataclysmFlinchRefusal::WindingUp);

	FCataclysmFlinchFacts Flinching = Idle;
	Flinching.bFlinching = true;
	Expect(TEXT("nor one already flinching"), Flinching, ECataclysmFlinchRefusal::AlreadyFlinching);

	FCataclysmFlinchFacts Attacking = Idle;
	Attacking.bAttacking = true;
	Expect(TEXT("nor one attacking"), Attacking, ECataclysmFlinchRefusal::Attacking);

	// THE ORDER DECIDES ONLY WHICH REASON IS REPORTED.
	FCataclysmFlinchFacts Everything = Idle;
	Everything.bDead = Everything.bCannotAct = Everything.bCharging = Everything.bWindingUp =
		Everything.bFlinching = Everything.bAttacking = true;
	Expect(TEXT("everything at once reports death, the first"), Everything, ECataclysmFlinchRefusal::Dead);

	FCataclysmFlinchFacts WindUpAndSwing = Idle;
	WindUpAndSwing.bWindingUp = WindUpAndSwing.bAttacking = true;
	Expect(TEXT("a wind-up outranks a swing"), WindUpAndSwing, ECataclysmFlinchRefusal::WindingUp);

	FCataclysmFlinchFacts FlinchAndClip = Idle;
	FlinchAndClip.bFlinching = FlinchAndClip.bAttacking = true;
	Expect(TEXT("a flinch still playing is reported as itself, not as the one-shot it is"), FlinchAndClip,
		ECataclysmFlinchRefusal::AlreadyFlinching);
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionEachCreatureTest,
	"Cataclysm.HitReaction.EachDemonicCreatureFlinchesWithTheClipForTheSideItWasStruckFrom")
{
	using namespace CataclysmHitReactionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE SEVEN, AND THE FOUR CLIPS EACH SHOULD CHOOSE, in ECataclysmHitDirection order. Written out here rather than
	// read from the creatures, so a creature naming its clips in the wrong order fails rather than agreeing with
	// itself. `game/docs/enemy-source-assets.md` records the same names.
	const FHitReactionFixtureRow Rows[] = {
		{ ACataclysmBruteCharacter::StaticClass(), TEXT("the Brute"),
		  { TEXT("HitReact_Front"), TEXT("HitReact_Back"), TEXT("HitReact_Left"), TEXT("HitReact_Right") } },
		{ ACataclysmAbyssalWardenCharacter::StaticClass(), TEXT("the Abyssal Warden"),
		  { TEXT("HitReact_Front"), TEXT("HitReact_Back"), TEXT("HitReact_Left"), TEXT("HitReact_Right") } },
		{ ACataclysmImpCharacter::StaticClass(), TEXT("the Imp"),
		  { TEXT("HitReact_Front"), TEXT("HitReact_Back"), TEXT("HitReact_Left"), TEXT("HitReact_Right") } },
		{ ACataclysmCorruptedSentinelCharacter::StaticClass(), TEXT("the Corrupted Sentinel"),
		  { TEXT("HitReact_Front_Planted"), TEXT("HitReact_Back_Planted"), TEXT("HitReact_Left_Planted"),
			TEXT("HitReact_Right_Planted") } },
		{ ACataclysmHellhoundCharacter::StaticClass(), TEXT("the Hellhound"),
		  { TEXT("IggyScorch_HitReact_Front"), TEXT("IggyScorch_HitReact_Back"), TEXT("IggyScorch_HitReact_Left"),
			TEXT("IggyScorch_HitReact_Right") } },
		{ ACataclysmGatekeeperCharacter::StaticClass(), TEXT("the Gatekeeper"),
		  { TEXT("Hitreact_Front"), TEXT("Hitreact_Back"), TEXT("Hitreact_Left"), TEXT("Hitreact_Right") } },
		{ ACataclysmSuccubusCharacter::StaticClass(), TEXT("the Succubus"),
		  { TEXT("Hitreact_Fwd"), TEXT("Hitreact_Bwd"), TEXT("Hitreact_Left"), TEXT("Hitreact_Right") } },
	};

	// WHERE THE STRIKER STANDS FOR EACH SIDE, from a creature facing +X: its right is +Y.
	const FVector StrikerOffsets[UCataclysmHitReaction::DirectionCount] = {
		FVector(StrikerStandsCm, 0.0f, 0.0f),
		FVector(-StrikerStandsCm, 0.0f, 0.0f),
		FVector(0.0f, -StrikerStandsCm, 0.0f),
		FVector(0.0f, StrikerStandsCm, 0.0f),
	};
	const TCHAR* SideWords[UCataclysmHitReaction::DirectionCount] = {
		TEXT("in front"), TEXT("behind"), TEXT("the left"), TEXT("the right"),
	};

	// ONE FRESH CREATURE PER SIDE, so a clip that played for one side cannot refuse the next: nothing ends a clip in
	// a world that is never ticked, and the Brute's graph would report its montage playing for ever.
	int32 ClipsMissing = 0;
	int32 Placed = 0;
	for (const FHitReactionFixtureRow& Row : Rows)
	{
		for (int32 Side = 0; Side < UCataclysmHitReaction::DirectionCount; ++Side)
		{
			const FVector At(FixturesApartCm * Placed, 0.0f, 0.0f);
			++Placed;

			ACataclysmEnemyCharacter* Creature = SpawnCreatureToStrike(World, Row.Class, At);
			ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, At + StrikerOffsets[Side]);
			if (!TestNotNull(Row.Who, Creature)
				|| !TestNotNull(TEXT("a striker"), Striker))
			{
				return false;
			}

			LandABlow(Striker, Creature);

			const TCHAR* Name = Row.Names[Side];
			TestEqual(*FString::Printf(TEXT("%s, struck from %s, chooses one flinch"), Row.Who, SideWords[Side]),
				Creature->FlinchesChosen, 1);
			TestEqual(*FString::Printf(TEXT("%s, struck from %s, flinches toward that side"), Row.Who,
					SideWords[Side]),
				static_cast<int32>(Creature->LastFlinchDirection), Side);
			TestTrue(*FString::Printf(TEXT("%s, struck from %s, chooses %s (it chose %s)"), Row.Who,
					SideWords[Side], Name, *Creature->LastFlinchClipPath),
				Creature->LastFlinchClipPath.EndsWith(FString::Printf(TEXT("/%s.%s"), Name, Name)));

			// WITH THE ART, THE CLIP LOADED AND HOLDS THE BODY.
			const bool bLoaded = Creature->HitReactionAnimations.IsValidIndex(Side)
				&& Creature->HitReactionAnimations[Side] != nullptr;
			if (!bLoaded)
			{
				++ClipsMissing;
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s's %s loaded and is the clip played"), Row.Who, Name),
				Creature->LastFlinchClip != nullptr && Creature->LastFlinchClip->GetName() == Name);
			TestTrue(*FString::Printf(TEXT("%s is flinching until the clip ends"), Row.Who),
				Creature->IsFlinching());
		}
	}

	if (ClipsMissing > 0)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this, FString::Printf(
			TEXT("%d of the 28 clips did not load, which is every one without the Paragon packs, so for those the "
				 "clip's loading and playing were not checked; which direction and which clip path each blow chose "
				 "were."), ClipsMissing));
	}
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionNoClipsTest,
	"Cataclysm.HitReaction.ACreatureWithNoClipsNamedNeverFlinches")
{
	using namespace CataclysmHitReactionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE PLAIN CREATURE EVERY OTHER TEST STRIKES, and every creature that is not one of the seven.
	ACataclysmEnemyCharacter* Plain =
		SpawnCreatureToStrike(World, ACataclysmEnemyCharacter::StaticClass(), FVector::ZeroVector);
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -StrikerStandsCm, 0.0f));
	if (!TestNotNull(TEXT("a plain creature"), Plain) || !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}

	const float Full = HealthLeftOn(Plain);
	LandABlow(Striker, Plain);
	TestTrue(TEXT("set-up: the blow landed"), HealthLeftOn(Plain) < Full);
	TestEqual(TEXT("a creature naming no clips chooses no flinch"), Plain->FlinchesChosen, 0);
	TestEqual(TEXT("and says so"), static_cast<int32>(Plain->LastFlinchRefusal),
		static_cast<int32>(ECataclysmFlinchRefusal::NoHitReactions));
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionWindUpTest,
	"Cataclysm.HitReaction.NoFlinchStartsDuringAWindUpAndTheWindUpIsUntouched")
{
	using namespace CataclysmHitReactionTest;
	using Warden_t = ACataclysmAbyssalWardenCharacter;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Warden =
		SpawnCreatureToStrike(World, Warden_t::StaticClass(), FVector::ZeroVector);
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -StrikerStandsCm, 0.0f));
	if (!TestNotNull(TEXT("a Warden"), Warden) || !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}
	ACataclysmEnemyController* Brain = Cast<ACataclysmEnemyController>(Warden->GetController());
	if (!TestNotNull(TEXT("the Warden's brain"), Brain))
	{
		return false;
	}

	// THE CONTROL: idle, the same blow flinches it.
	LandABlow(Striker, Warden);
	if (!TestEqual(TEXT("set-up: an idle Warden flinches"), Warden->FlinchesChosen, 1))
	{
		return false;
	}

	// PAST ANY CLIP THAT PLAYED, then committed to Molten Roar as the brain commits: the ability, when it lands, and
	// how many passes are left. Set by hand, as `Cataclysm.Gatekeeper` sets its sweep's.
	PushTheClockOn(World, LongerThanAnyFlinchSeconds);
	const float LandsAt = WorldNowIn(World) + Warden_t::MoltenRoarWindUpSeconds;
	Brain->WindingUpAbility = Warden_t::MoltenRoarAbility;
	Brain->WindUpLandsAt = LandsAt;
	Brain->WindUpPassesLeft = 8;

	LandABlow(Striker, Warden);
	TestEqual(TEXT("a hit during Molten Roar's wind-up starts no flinch"), Warden->FlinchesChosen, 1);
	TestEqual(TEXT("and says the wind-up is why"), static_cast<int32>(Warden->LastFlinchRefusal),
		static_cast<int32>(ECataclysmFlinchRefusal::WindingUp));

	// RULING (a): THE WIND-UP IS NEITHER CANCELLED NOR DELAYED.
	TestEqual(TEXT("the wind-up is still Molten Roar"), Brain->WindingUpAbility,
		static_cast<int32>(Warden_t::MoltenRoarAbility));
	TestEqual(TEXT("landing when it was going to"), Brain->WindUpLandsAt, LandsAt, 0.0001f);
	TestEqual(TEXT("with as many passes still to go"), Brain->WindUpPassesLeft, 8);

	Brain->WindingUpAbility = INDEX_NONE;
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionDuringASwingTest,
	"Cataclysm.HitReaction.NoFlinchStartsDuringASwingAndItsBlowLandsWhenItWould")
{
	using namespace CataclysmHitReactionTest;
	using Brute_t = ACataclysmBruteCharacter;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Brute = SpawnCreatureToStrike(World, Brute_t::StaticClass(), FVector::ZeroVector);
	// INSIDE THE BRUTE'S 90 CM REACH, the distance `Cataclysm.EnemyHitTiming` uses.
	ACataclysmEnemyCharacter* Target = SpawnSomethingToSwingAt(World, FVector(80.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -StrikerStandsCm, 0.0f));
	if (!TestNotNull(TEXT("a Brute"), Brute) || !TestNotNull(TEXT("its target"), Target)
		|| !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}
	Brute->SetAttackDamage(100.0f);

	const float Full = HealthLeftOn(Target);
	Brute->AttackTarget(Target);
	if (!TestTrue(TEXT("set-up: the swing's blow is waiting for it to connect"), Brute->HasPendingBasicAttack()))
	{
		return false;
	}
	const float DueAt = Brute->PendingBasicAttackLandsAtSeconds;

	LandABlow(Striker, Brute);
	TestEqual(TEXT("a hit while its swing is under way starts no flinch"), Brute->FlinchesChosen, 0);
	TestEqual(TEXT("and says the swing is why"), static_cast<int32>(Brute->LastFlinchRefusal),
		static_cast<int32>(ECataclysmFlinchRefusal::Attacking));

	// RULING (a), AND ISSUE #783'S MOMENT: THE BLOW LANDS EXACTLY WHEN IT WOULD HAVE.
	TestEqual(TEXT("the swing's blow is still due when it was"), Brute->PendingBasicAttackLandsAtSeconds, DueAt,
		0.0001f);
	PushTheClockOn(World, (DueAt - WorldNowIn(World)) - 0.01);
	TestFalse(TEXT("a hundredth of a second before it is due, nothing lands"), Brute->LandPendingBasicAttack());
	TestEqual(TEXT("and its target is untouched"), HealthLeftOn(Target), Full);
	PushTheClockOn(World, 0.02);
	TestTrue(TEXT("a hundredth of a second after, it lands"), Brute->LandPendingBasicAttack());
	TestTrue(*FString::Printf(TEXT("and takes health (%.1f to %.1f)"), Full, HealthLeftOn(Target)),
		HealthLeftOn(Target) < Full);
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionSwingEndsFlinchTest,
	"Cataclysm.HitReaction.ASwingOrderedDuringAFlinchEndsItAndIsNotDelayed")
{
	using namespace CataclysmHitReactionTest;
	using Warden_t = ACataclysmAbyssalWardenCharacter;

	// THE WARDEN, BECAUSE IT IS THE ONE CREATURE WHOSE SWING WAITS for a one-shot still playing (issue #783's "The
	// Warden's blow waits for a swing that has not started yet"), so a flinch it did not cut short would delay both
	// the swing and its blow.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	Warden_t* Warden = Cast<Warden_t>(SpawnCreatureToStrike(World, Warden_t::StaticClass(), FVector::ZeroVector));
	ACataclysmEnemyCharacter* Target = SpawnSomethingToSwingAt(World, FVector(80.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -StrikerStandsCm, 0.0f));
	if (!TestNotNull(TEXT("a Warden"), Warden) || !TestNotNull(TEXT("its target"), Target)
		|| !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}
	Warden->SetAttackDamage(100.0f);

	LandABlow(Striker, Warden);
	if (!TestEqual(TEXT("set-up: the Warden flinches"), Warden->FlinchesChosen, 1))
	{
		return false;
	}

	// WITHOUT THE ART NO CLIP PLAYED, so a flinch half a second long is written in its place, as
	// `Cataclysm.EnemyHitTiming` writes the clip still playing.
	const bool bClipPlayed = Warden->IsFlinching();
	if (!bClipPlayed)
	{
		Warden->FlinchEndsAtSeconds = WorldNowIn(World) + 0.5f;
	}
	if (!TestTrue(TEXT("set-up: it is flinching"), Warden->IsFlinching()))
	{
		return false;
	}

	Warden->AttackTarget(Target);
	TestFalse(TEXT("the swing ends the flinch"), Warden->IsFlinching());
	TestEqual(TEXT("and its blow waits for the left swing's strike alone, not for the flinch"),
		Warden->PendingBasicAttackLandsAtSeconds - WorldNowIn(World), Warden_t::LeftSwingStrikeSeconds, 0.001f);

	if (!bClipPlayed)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("The Paragon Grux pack is absent, so no flinch clip played and no left swing was there to wait "
				 "behind one. The flinch ending and the blow's moment are checked; the left swing starting at once "
				 "over a flinch clip still playing is not."));
		return true;
	}

	TestEqual(TEXT("the left swing is playing, at once"), Warden->LastPlayedAnimation.Get(),
		Warden->LeftSwingAnimation.Get());
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionOneAtATimeTest,
	"Cataclysm.HitReaction.NoFlinchStartsWhileOneIsPlaying")
{
	using namespace CataclysmHitReactionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Imp =
		SpawnCreatureToStrike(World, ACataclysmImpCharacter::StaticClass(), FVector::ZeroVector);
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -StrikerStandsCm, 0.0f));
	if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}

	LandABlow(Striker, Imp);
	if (!TestEqual(TEXT("set-up: the first blow flinches it"), Imp->FlinchesChosen, 1))
	{
		return false;
	}

	// THE CLIP'S OWN LENGTH IS THE LIMIT. Without the art none played, so half a second is written in its place.
	const bool bClipPlayed = Imp->IsFlinching();
	if (!bClipPlayed)
	{
		Imp->FlinchEndsAtSeconds = WorldNowIn(World) + 0.5f;
	}
	const float EndsAt = Imp->FlinchEndsAtSeconds;

	LandABlow(Striker, Imp);
	TestEqual(TEXT("a second blow while the first flinch plays starts none"), Imp->FlinchesChosen, 1);
	TestEqual(TEXT("and says a flinch is playing"), static_cast<int32>(Imp->LastFlinchRefusal),
		static_cast<int32>(ECataclysmFlinchRefusal::AlreadyFlinching));

	PushTheClockOn(World, (EndsAt - WorldNowIn(World)) + 0.01);
	LandABlow(Striker, Imp);
	TestEqual(TEXT("once it has ended, the next blow flinches again"), Imp->FlinchesChosen, 2);

	if (!bClipPlayed)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("The Paragon Minions pack is absent, so no flinch clip played and its length was written in by "
				 "hand. That a flinch refuses while one plays, and not after, is checked; that the clip's real "
				 "length is what sets the limit is not."));
	}
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionTickTest,
	"Cataclysm.HitReaction.ADamageOverTimeTickStartsNoFlinch")
{
	using namespace CataclysmHitReactionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE STRIKER STANDS A HUNDRED METRES AWAY, beyond every creature's notice, because the burn's ticks are timers
	// and running them runs the Imp's brain too: it finds nobody to chase or swing at.
	ACataclysmEnemyCharacter* Imp =
		SpawnCreatureToStrike(World, ACataclysmImpCharacter::StaticClass(), FVector::ZeroVector);
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -10000.0f, 0.0f));
	if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}

	if (!TestTrue(TEXT("set-up: a burn is laid on the Imp"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Striker, Imp, /*DamagePerTick=*/50.0f,
				/*DurationSeconds=*/3.0f, UCataclysmSkillEffects::BurnTag(), /*bScalesWithInstigator=*/false)))
	{
		return false;
	}

	const float Full = HealthLeftOn(Imp);
	CataclysmTestWorld::RunClock(World, 2.5f);
	TestTrue(*FString::Printf(TEXT("set-up: the burn's ticks took health (%.1f to %.1f)"), Full, HealthLeftOn(Imp)),
		HealthLeftOn(Imp) < Full);
	TestEqual(TEXT("the burn's ticks start no flinch"), Imp->FlinchesChosen, 0);

	// THE CONTROL: a landed blow afterwards, with the clock moved past any clip and no timer run, does.
	PushTheClockOn(World, LongerThanAnyFlinchSeconds);
	const int32 Before = Imp->FlinchesChosen;
	LandABlow(Striker, Imp);
	TestEqual(TEXT("a landed blow afterwards does"), Imp->FlinchesChosen, Before + 1);
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionKillingBlowTest,
	"Cataclysm.HitReaction.ABlowThatKillsStartsNoFlinch")
{
	using namespace CataclysmHitReactionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Imp =
		SpawnCreatureToStrike(World, ACataclysmImpCharacter::StaticClass(), FVector::ZeroVector);
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -StrikerStandsCm, 0.0f));
	if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}

	// ONE HIT POINT, AND THE EVASION ZEROED AFTER IT, because `SetHealth` writes the designed evasion back.
	Imp->SetHealth(1.0f);
	LeaveNothingToMiss(Imp);

	LandABlow(Striker, Imp);
	if (!TestTrue(TEXT("set-up: the blow killed it"), UCataclysmSkillEffects::IsDead(Imp)))
	{
		return false;
	}
	TestEqual(TEXT("a blow that kills starts no flinch"), Imp->FlinchesChosen, 0);
	TestEqual(TEXT("and says death is why"), static_cast<int32>(Imp->LastFlinchRefusal),
		static_cast<int32>(ECataclysmFlinchRefusal::Dead));
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionHeldTest,
	"Cataclysm.HitReaction.AStunnedOrKnockedDownCreatureDoesNotFlinch")
{
	using namespace CataclysmHitReactionTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Stunned =
		SpawnCreatureToStrike(World, ACataclysmImpCharacter::StaticClass(), FVector::ZeroVector);
	ACataclysmEnemyCharacter* Floored =
		SpawnCreatureToStrike(World, ACataclysmImpCharacter::StaticClass(), FVector(FixturesApartCm, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -StrikerStandsCm, 0.0f));
	if (!TestNotNull(TEXT("an Imp to stun"), Stunned) || !TestNotNull(TEXT("an Imp to knock down"), Floored)
		|| !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}

	// DESIGNED, SO NO DAMAGE THRESHOLD APPLIES, the way `Cataclysm.EnemyHitTiming` stuns its Brute.
	if (!TestTrue(TEXT("set-up: one Imp is stunned"),
			UCataclysmSkillEffects::ApplyStun(Striker, Stunned, /*DurationSeconds=*/1.5f, /*DamageDealt=*/1000.0f,
				/*bStunIsDesigned=*/true))
		|| !TestTrue(TEXT("set-up: the other is knocked down"),
			UCataclysmSkillEffects::ApplyKnockdown(Striker, Floored, /*DurationSeconds=*/1.5f,
				/*DamageDealt=*/1000.0f, /*bKnockdownIsDesigned=*/true)))
	{
		return false;
	}

	LandABlow(Striker, Stunned);
	LandABlow(Striker, Floored);
	TestEqual(TEXT("a stunned Imp does not flinch"), Stunned->FlinchesChosen, 0);
	TestEqual(TEXT("because it cannot act"), static_cast<int32>(Stunned->LastFlinchRefusal),
		static_cast<int32>(ECataclysmFlinchRefusal::CannotAct));
	TestEqual(TEXT("nor does a knocked-down one"), Floored->FlinchesChosen, 0);
	TestEqual(TEXT("for the same reason"), static_cast<int32>(Floored->LastFlinchRefusal),
		static_cast<int32>(ECataclysmFlinchRefusal::CannotAct));
	return true;
}

CATACLYSM_TEST(FCataclysmHitReactionChargeTest,
	"Cataclysm.HitReaction.NoFlinchStartsMidCharge")
{
	using namespace CataclysmHitReactionTest;
	using Warden_t = ACataclysmAbyssalWardenCharacter;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Warden =
		SpawnCreatureToStrike(World, Warden_t::StaticClass(), FVector::ZeroVector);
	ACataclysmEnemyCharacter* Striker = SpawnFlinchStriker(World, FVector(0.0f, -StrikerStandsCm, 0.0f));
	if (!TestNotNull(TEXT("a Warden"), Warden) || !TestNotNull(TEXT("a striker"), Striker))
	{
		return false;
	}

	// STAMPEDE'S OWN FIGURES, down the +X lane, away from the striker.
	Warden->BeginCharge(Warden->GetActorLocation() + FVector(Warden_t::StampedeRangeCm, 0.0f, 0.0f),
		Warden_t::StampedeSpeedCmPerSecond, Warden_t::StampedeRadiusCm, Warden_t::StampedeDamagePercent);
	if (!TestTrue(TEXT("set-up: the Warden is charging"), Warden->IsCharging()))
	{
		return false;
	}

	LandABlow(Striker, Warden);
	TestEqual(TEXT("a hit mid-charge starts no flinch"), Warden->FlinchesChosen, 0);
	TestEqual(TEXT("and says the charge is why"), static_cast<int32>(Warden->LastFlinchRefusal),
		static_cast<int32>(ECataclysmFlinchRefusal::Charging));
	TestTrue(TEXT("and the charge runs on"), Warden->IsCharging());
	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
