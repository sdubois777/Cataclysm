// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmImpCharacter.h"
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
 * Tests for the Imp, the fourth of the seven Demonic vertical slice creatures
 * and the first that is designed to arrive in numbers.
 *
 * WHAT THESE GUARD, and each is something the creature can fail at silently:
 *
 * 1. BEING BUILT FOR A PACK RATHER THAN FOR ONE FIGHT. Its designed body radius
 *    of 0.30 metres is the only measured one in the roster and the whole of its
 *    reach is derived from it: seven bodies fit in a ring 0.72 m from the player
 *    and thirteen more at 1.32 m, so twenty can swing at once and a twenty-first
 *    waits. A capsule that quietly returned to the 0.48 m default every other
 *    creature uses would break that arithmetic and nothing on screen would say
 *    so. `ItCarriesItsDesignedProfile` checks the live capsule.
 *
 * 2. TEN OF THEM NOT ALL SWINGING THE SAME CLIP. The pack ships five claw
 *    swipes that fit the attack interval and the creature draws one per swing.
 *    One clip is right for a creature you meet alone; ten of them in step is the
 *    one place in this project where a single clip is visibly wrong.
 *    `ItDrawsBetweenSeveralClawSwipes` spawns a pack and requires the draws to
 *    differ.
 *
 * 3. OFFERING THE BRAIN NOTHING TO CHOOSE. `EnemyAbilities` is deliberately not
 *    overridden, so this creature has no ability at all, which is what the
 *    design says of it in as many words. A test is what tells an ability added
 *    by accident from one added on purpose.
 *
 * 4. WEARING ITS ART AT THE SIZE IT WAS AUTHORED. That is a decision rather than
 *    a default -- see `ACataclysmImpCharacter::ImpMeshScale` -- and it is tied
 *    to the play rate its walk needs, which a scale change would push past the
 *    ceiling.
 *
 * WHAT THESE DELIBERATELY DO NOT CHECK. Whether the pack queues into rings. The
 * design says the ring is what crowd avoidance produces, this project has none
 * at all, and an automation test world holds no navigation mesh to path across
 * anyway. Issue #761.
 */

namespace CataclysmImpTest
{
	using Imp_t = ACataclysmImpCharacter;

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

	static ACataclysmImpCharacter* SpawnImp(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmImpCharacter>(
			ACataclysmImpCharacter::StaticClass(), Where, FRotator::ZeroRotator,
			Params);
	}

	/** The player's capsule, which the creature's reach is derived from.
	 *  `CapsuleRadius` in `CataclysmPlayerCharacter.cpp` is file-local, so it is
	 *  copied here and `tools/tests/test_imp_matches_the_model.py` reads the
	 *  real one out of that file and holds the two together. */
	static constexpr float PlayerCapsuleRadiusCm = 42.0f;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmImpCarriesItsDesignedProfile,
	"Cataclysm.Imp.ItCarriesItsDesignedProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpCarriesItsDesignedProfile::RunTest(const FString&)
{
	using namespace CataclysmImpTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector::ZeroVector);
	if (!Imp)
	{
		AddError(TEXT("could not spawn an Imp"));
		return false;
	}

	TestEqual(TEXT("it reaches its designed distance"),
		Imp->AttackReachCm(), Imp_t::DesignedMeleeReachCm);
	TestEqual(TEXT("it swipes on its designed interval"),
		Imp->SecondsBetweenAttacks(), Imp_t::DesignedAttackIntervalSeconds);
	TestEqual(TEXT("it wanders its designed distance"),
		Imp->RoamRadiusCm(), Imp_t::ImpRoamRadiusCm);
	TestEqual(TEXT("it notices at its designed distance"),
		Imp->SightRadiusCm(), Imp_t::ImpNoticeRadiusCm);

	// AND EVERY ONE OF THOSE DIFFERS FROM THE BASE ENEMY'S, which is what makes
	// the four above mean anything.
	TestNotEqual(TEXT("its reach is not the base enemy's"),
		Imp->AttackReachCm(), 200.0f);
	TestNotEqual(TEXT("its interval is not the base enemy's"),
		Imp->SecondsBetweenAttacks(), 1.5f);
	TestTrue(TEXT("it roams, where the base enemy does not"),
		Imp->RoamRadiusCm() > 0.0f);

	const UCapsuleComponent* Capsule = Imp->GetCapsuleComponent();
	TestEqual(TEXT("its capsule is its designed radius"),
		Capsule->GetScaledCapsuleRadius(), Imp_t::ImpCapsuleRadius);
	TestEqual(TEXT("its capsule is its designed half-height"),
		Capsule->GetScaledCapsuleHalfHeight(), Imp_t::ImpCapsuleHalfHeight);

	// **THE ONE THAT WOULD BREAK THE WHOLE CREATURE SILENTLY.** This is the only
	// measured body radius in the roster and the reach is derived from it. Six
	// other creatures take the 0.48 m dataclass default, so 48 is exactly the
	// figure this would drift back to. Issue #366.
	TestNotEqual(TEXT("and it is not the 0.48 m default six other creatures "
					  "silently use"),
		Capsule->GetScaledCapsuleRadius(), 48.0f);

	// AND THE REACH REALLY IS THE SECOND RANK OF A CROWD, recomputed from the
	// two bodies rather than quoted. The player's capsule, then one Imp's body,
	// is the first rank; another Imp's body beyond that is the second.
	const float FirstRank = PlayerCapsuleRadiusCm + Imp_t::ImpCapsuleRadius;
	const float SecondRank = FirstRank + 2.0f * Imp_t::ImpCapsuleRadius;
	TestEqual(TEXT("its reach is exactly where the second rank of a crowd "
				   "stands, which is what lets more than seven swing at once"),
		Imp->AttackReachCm(), SecondRank);

	const UCharacterMovementComponent* Movement = Imp->GetCharacterMovement();
	TestEqual(TEXT("it walks at its designed speed"),
		Movement->MaxWalkSpeed, Imp_t::DesignedWalkSpeedCmPerSecond);
	// CAST BECAUSE FRotator's COMPONENTS ARE DOUBLES IN UNREAL 5. Comparing one
	// against a float constant is an ambiguous overload, error C2666.
	TestEqual(TEXT("it turns at its designed rate"),
		Movement->RotationRate.Yaw,
		static_cast<double>(Imp_t::DesignedTurnRateDegreesPerSecond));

	// `ACataclysmEnemyCharacter` never sets MaxWalkSpeed, so a creature that
	// forgot to would move at Unreal's default 600 -- which for this creature
	// would be a silent slowing back inside the speed a player can walk away
	// from, and walking away is what the design says never works.
	TestTrue(TEXT("and it is faster than Unreal's default walk speed, which is "
				  "what a forgotten MaxWalkSpeed would leave it at"),
		Movement->MaxWalkSpeed > 600.0f);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmImpOffersTheBrainNothing,
	"Cataclysm.Imp.ItOffersTheBrainNothingToChoose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpOffersTheBrainNothing::RunTest(const FString&)
{
	using namespace CataclysmImpTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector::ZeroVector);
	if (!Imp)
	{
		AddError(TEXT("could not spawn an Imp"));
		return false;
	}

	// **THE ONLY CREATURE IN THE SLICE THAT OFFERS NONE**, and the design says
	// so outright: "The Imp has one attack and nothing else, and that is the
	// design rather than an omission." Its one attack is Rend, which like every
	// other basic attack in this project is MeleeReachCm plus
	// AttackIntervalSeconds rather than an entry in this array.
	//
	// THE DESIGN REFUSES A SECOND ABILITY FOR TWO STATED REASONS. Whatever an
	// Imp does is multiplied by the pack, so one extra ability between ten of
	// them is ten of it at once; and a second ability could not be quick, because
	// the smallest useful marker needs a cycle of 1.4 seconds and this creature's
	// whole attack interval is 0.9.
	TestEqual(TEXT("it offers the brain nothing to choose between"),
		Imp->EnemyAbilities().Num(), 0);

	// AND IT CANNOT TELEGRAPH, which is not a choice made for this creature.
	// The telegraph rule caps a marker at 3.5 x (interval / 2 - 0.4) metres, and
	// at 0.9 seconds that is 0.2 -- smaller than the creature standing in it.
	const float LargestMarkerMetres =
		3.5f * (Imp_t::DesignedAttackIntervalSeconds / 2.0f - 0.4f);
	constexpr float SmallestUsefulMarkerMetres = 1.0f;

	TestTrue(FString::Printf(
			TEXT("its attack interval allows a marker of only %.2f m, under the "
				 "%.1f m the design calls the smallest useful one"),
			LargestMarkerMetres, SmallestUsefulMarkerMetres),
		LargestMarkerMetres < SmallestUsefulMarkerMetres);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmImpWearsItsMeshAndHidesThePlaceholder,
	"Cataclysm.Imp.ItWearsItsMeshAndHidesThePlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpWearsItsMeshAndHidesThePlaceholder::RunTest(const FString&)
{
	using namespace CataclysmImpTest;

	// A WORLD THAT HAS NOT BEGUN PLAY. This test is about what ResolveBody does
	// and proves it by watching the placeholder go from visible to hidden. The
	// creature's own BeginPlay calls ResolveBody, so in a world that has begun
	// play there is no transition left to watch.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasNotBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector::ZeroVector);
	if (!Imp)
	{
		AddError(TEXT("could not spawn an Imp"));
		return false;
	}

	if (!Imp->PlaceholderBody)
	{
		AddError(TEXT("the enemy base no longer creates a PlaceholderBody, so "
					  "there is nothing to hide and this check is meaningless"));
		return false;
	}

	TestTrue(TEXT("the placeholder cylinder starts visible"),
		Imp->PlaceholderBody->IsVisible());

	const bool bDressed = Imp->ResolveBody(/*bIncludeAnimation=*/true);

	// THE RELATIONSHIP RATHER THAN AN ABSOLUTE, so this runs the same on a
	// machine with the Paragon Minions pack and on one without.
	TestEqual(
		TEXT("the placeholder is hidden exactly when the real mesh resolved"),
		Imp->PlaceholderBody->IsVisible(), !bDressed);

	if (!bDressed)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is not present, so what was checked "
				 "is that the placeholder is KEPT rather than that it is "
				 "hidden, and neither the mesh nor any of its clips was loaded. "
				 "Both directions matter; only one ran here."));
		return true;
	}

	USkeletalMeshComponent* MeshComponent = Imp->GetMesh();
	if (!MeshComponent)
	{
		AddError(TEXT("the creature has no skeletal mesh component"));
		return false;
	}

	TestNotNull(TEXT("it is wearing a skeletal mesh"),
		MeshComponent->GetSkeletalMeshAsset());

	// **AT THE SIZE IT WAS AUTHORED, WHICH IS A DECISION.** The mesh's shoulders
	// measure 63.5 cm apart against the 60 cm this creature's designed body
	// radius gives it, so the art already is the width the design asks for.
	// Scaling it down would also raise the play rate its walk needs, because a
	// smaller mesh takes a shorter stride. Issue #760.
	TestEqual(TEXT("the mesh is worn at its designed scale"),
		MeshComponent->GetRelativeScale3D().X,
		static_cast<double>(Imp_t::ImpMeshScale));

	// FEET ON THE CAPSULE'S BOTTOM, AND THE DROP FOLLOWS THE SCALE. A mesh at
	// half size stands half as tall, so dropping it by the full half-height
	// would bury it.
	TestEqual(TEXT("the mesh is dropped so its feet are on the capsule bottom"),
		MeshComponent->GetRelativeLocation().Z,
		static_cast<double>(-Imp_t::ImpCapsuleHalfHeight * Imp_t::ImpMeshScale));

	TestEqual(TEXT("and yawed the engine's -90 degrees for a character mesh"),
		MeshComponent->GetRelativeRotation().Yaw, -90.0);

	TestNotNull(TEXT("its standing clip loaded"), Imp->IdleAnimation.Get());
	TestNotNull(TEXT("its walking clip loaded"), Imp->JogAnimation.Get());

	// FIVE CLAW SWIPES AND FIVE DEATHS, and the counts matter rather than only
	// that something loaded: the draw is over however many entries there are.
	TestEqual(TEXT("it loaded every claw swipe it draws between"),
		Imp->RendAnimations.Num(), Imp_t::RendAnimationCount);
	TestEqual(TEXT("it loaded every way it has of falling over"),
		Imp->DeathAnimations.Num(), Imp_t::DeathAnimationCount);

	int32 SwipesFound = 0;
	for (const TObjectPtr<UAnimSequence>& Clip : Imp->RendAnimations)
	{
		SwipesFound += Clip ? 1 : 0;

		// AND EVERY ONE OF THEM FITS INSIDE THE INTERVAL. Nothing rate-scales a
		// swipe up beyond its authored speed, so a clip longer than the interval
		// would still be playing when the next swipe began.
		if (Clip)
		{
			TestTrue(FString::Printf(
					TEXT("%s is %.4f s, inside the %.2f s interval"),
					*Clip->GetName(), Clip->GetPlayLength(),
					Imp_t::DesignedAttackIntervalSeconds),
				Clip->GetPlayLength() < Imp_t::DesignedAttackIntervalSeconds);
		}
	}
	TestEqual(TEXT("every claw swipe really resolved"),
		SwipesFound, Imp_t::RendAnimationCount);

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmImpDrawsBetweenSeveralClawSwipes,
	"Cataclysm.Imp.ItDrawsBetweenSeveralClawSwipes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpDrawsBetweenSeveralClawSwipes::RunTest(const FString&)
{
	using namespace CataclysmImpTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	// **A PACK, BECAUSE THAT IS THE CASE THIS EXISTS FOR.** The draw is seeded
	// from the creature's own identity, so one Imp swinging twice in the same
	// instant draws the same clip both times -- correctly, since the seed has
	// not changed. What must not happen is ten Imps swinging together all
	// drawing the same one, and that needs ten Imps to check.
	constexpr int32 PackSize = 12;

	TSet<const UAnimSequence*> Drawn;
	TSet<const UAnimSequence*> Available;
	int32 Swung = 0;
	bool bDressed = false;

	for (int32 Index = 0; Index < PackSize; ++Index)
	{
		// SPREAD OUT SO THEY DO NOT PUSH EACH OTHER, which changes nothing about
		// the draw and keeps the world tidy.
		ACataclysmImpCharacter* Imp =
			SpawnImp(World, FVector(Index * 500.0f, 0.0f, 0.0f));
		if (!Imp)
		{
			continue;
		}

		bDressed = Imp->ResolveBody(/*bIncludeAnimation=*/true) || bDressed;
		if (Imp->RendAnimations.IsEmpty())
		{
			continue;
		}

		for (const TObjectPtr<UAnimSequence>& Clip : Imp->RendAnimations)
		{
			if (Clip)
			{
				Available.Add(Clip.Get());
			}
		}

		Imp->AttackTarget(nullptr);
		if (const UAnimSequence* Played = Imp->LastPlayedAnimation.Get())
		{
			Drawn.Add(Played);
			++Swung;
		}
	}

	if (!bDressed || Swung == 0)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is not present, so there are no claw "
				 "swipes to draw between. Whether a pack draws more than one "
				 "was NOT checked on this machine."));
		return true;
	}

	TestEqual(TEXT("every Imp in the pack swung"), Swung, PackSize);

	// MORE THAN ONE, WHICH IS THE WHOLE CLAIM. Twelve draws over five clips
	// landing on one clip has a chance of about 2 in a hundred million, so this
	// failing means the draws are correlated rather than unlucky -- which is
	// exactly what seeding every creature from a stream that does not vary would
	// produce.
	TestTrue(FString::Printf(
			TEXT("%d Imps drew %d different claw swipes out of %d"),
			Swung, Drawn.Num(), Imp_t::RendAnimationCount),
		Drawn.Num() > 1);

	// AND EVERY CLIP DRAWN IS ONE OF THE CREATURE'S OWN CLAW SWIPES, which is
	// what would catch a draw running off the end of the array. Checked against
	// the clips the pack really loaded rather than against a list written here,
	// so the two cannot disagree.
	TestEqual(FString::Printf(
			TEXT("the pack loaded %d distinct claw swipes"), Available.Num()),
		Available.Num(), Imp_t::RendAnimationCount);

	for (const UAnimSequence* Played : Drawn)
	{
		TestTrue(FString::Printf(
				TEXT("the clip drawn, %s, is one of the creature's claw swipes"),
				Played ? *Played->GetName() : TEXT("null")),
			Played != nullptr && Available.Contains(Played));
	}

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmImpReturnsToARestingPoseAfterASwipe,
	"Cataclysm.Imp.ItReturnsToARestingPoseAfterASwipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpReturnsToARestingPoseAfterASwipe::RunTest(const FString&)
{
	using namespace CataclysmImpTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { TearDown(World); };

	ACataclysmImpCharacter* Imp = SpawnImp(World, FVector::ZeroVector);
	if (!Imp)
	{
		AddError(TEXT("could not spawn an Imp"));
		return false;
	}

	const bool bDressed = Imp->ResolveBody(/*bIncludeAnimation=*/true);
	if (!bDressed || !Imp->IdleAnimation || Imp->RendAnimations.IsEmpty())
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is not present, so there are no clips "
				 "to move between. Whether a swipe gives the mesh back was NOT "
				 "checked on this machine."));
		return true;
	}

	// STANDING TO BEGIN WITH.
	Imp->UpdateLoopingAnimation();
	TestEqual(TEXT("it stands in its idle before anything happens"),
		Imp->CurrentLoopingAnimation.Get(), Imp->IdleAnimation.Get());

	// A SWIPE TAKES THE MESH AND RECORDS WHEN IT WILL GIVE IT BACK. Without that
	// the creature holds the last frame of the swipe until the next one.
	Imp->AttackTarget(nullptr);

	TestTrue(TEXT("the clip it played is one of its claw swipes"),
		Imp->RendAnimations.Contains(Imp->LastPlayedAnimation));
	TestTrue(TEXT("a swipe records when it will finish"),
		Imp->OneShotEndsAtSeconds > World->GetTimeSeconds());
	TestNull(TEXT("and nothing is looping while it plays"),
		Imp->CurrentLoopingAnimation.Get());

	// WHILE IT IS STILL PLAYING, NOTHING TAKES THE MESH BACK.
	Imp->UpdateLoopingAnimation();
	TestNull(TEXT("the swipe is left alone until it ends"),
		Imp->CurrentLoopingAnimation.Get());

	// AND ONCE IT HAS ENDED, THE RESTING POSE COMES BACK. The end time is moved
	// into the past rather than the world being ticked forward, which keeps this
	// instant and exercises the same branch.
	constexpr int32 MostClipsASwipeMayHave = 8;
	for (int32 Step = 0; Step < MostClipsASwipeMayHave; ++Step)
	{
		if (Imp->CurrentLoopingAnimation)
		{
			break;
		}
		Imp->OneShotEndsAtSeconds = 0.0f;
		Imp->UpdateLoopingAnimation();
	}

	TestEqual(TEXT("it returns to its idle once the swipe has finished"),
		Imp->CurrentLoopingAnimation.Get(), Imp->IdleAnimation.Get());

	return true;
}

// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmImpWalksRatherThanSlides,
	"Cataclysm.Imp.ItWalksRatherThanSlides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmImpWalksRatherThanSlides::RunTest(const FString&)
{
	using namespace CataclysmImpTest;

	// THE ARITHMETIC THAT STOPS THE FOOT SLIDING, checked without a world.
	//
	// A planted foot travels backwards at the clip's authored speed while the
	// body travels forwards at the designed speed. Playing the clip at the ratio
	// of the two makes them cancel.
	//
	// **AND THE MESH'S SCALE IS PART OF THAT RATIO**, which is the part that is
	// easy to get backwards. A mesh at half size takes half the stride, so its
	// planted foot travels at half the authored speed and the clip needs TWICE
	// the rate to keep up with the same body. The factor is one today.
	const float AuthoredAtThisSize =
		Imp_t::AuthoredJogSpeedCmPerSecond * Imp_t::ImpMeshScale;
	const float Expected =
		Imp_t::DesignedWalkSpeedCmPerSecond / AuthoredAtThisSize;

	TestEqual(TEXT("the walk plays at the ratio of designed speed to the speed "
				   "the clip carries the mesh at its worn size"),
		Imp_t::JogPlayRate(), Expected);

	// AND THE PRODUCT IS THE SPEED IT ACTUALLY MOVES AT, which is the thing that
	// matters and is not the same statement.
	TestEqual(TEXT("so the foot travels backwards at the body's own speed"),
		Imp_t::JogPlayRate() * AuthoredAtThisSize,
		Imp_t::DesignedWalkSpeedCmPerSecond);

	// THE RATE IS INSIDE THE CLAMP, so it is not being silently corrected.
	TestTrue(FString::Printf(
			TEXT("the rate is %.4f, inside the %.1f ceiling by %.4f"),
			Imp_t::JogPlayRate(), Imp_t::MaximumPlayRate,
			Imp_t::MaximumPlayRate - Imp_t::JogPlayRate()),
		Imp_t::JogPlayRate() > Imp_t::MinimumPlayRate
		&& Imp_t::JogPlayRate() < Imp_t::MaximumPlayRate);

	// AND THE COMBAT WALK REALLY IS TOO SLOW TO USE, which is why this creature
	// wears a clip named for not being in combat. Stated out loud because it
	// looks like a mistake and is not one. `Combat_JogFwd` measures 241.1 cm/s.
	constexpr float CombatWalkAuthoredCmPerSecond = 241.1f;
	const float CombatWalkRate = Imp_t::DesignedWalkSpeedCmPerSecond
		/ (CombatWalkAuthoredCmPerSecond * Imp_t::ImpMeshScale);

	TestTrue(FString::Printf(
			TEXT("the pack's combat walk would need a rate of %.3f, above the "
				 "%.1f ceiling"),
			CombatWalkRate, Imp_t::MaximumPlayRate),
		CombatWalkRate > Imp_t::MaximumPlayRate);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
