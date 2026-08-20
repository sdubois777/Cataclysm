// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyDeath.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * What dying looks like. Issue #522.
 *
 * WHAT IS COVERED. Which clip is drawn out of how many, how long the body is
 * kept for a clip of a given length, that a creature with art plays one of its
 * own death clips and outlives the killing blow, and that a creature without art
 * still goes on the next tick as it always did.
 *
 * WHAT CANNOT BE. That the clip is visible, or that it looks like dying. The
 * automation command in tools/unreal_build.py passes -nullrhi, so nothing
 * reaches a screen. What is checked instead is that the mesh was handed the
 * clip and that nothing took it back afterwards -- which is the failure that
 * would otherwise be invisible, because a corpse has no velocity and so reads as
 * standing to the code that chooses a locomotion loop.
 *
 * THE ART IS NOT IN THE REPOSITORY. The Paragon packs are gitignored, so the
 * two tests that need a dressed creature report a skip through
 * CataclysmTestSkip::ReportSkippedHalf rather than passing quietly on a
 * placeholder cylinder.
 */
namespace CataclysmEnemyDeathTest
{
	using FDeath = UCataclysmEnemyDeath;

	/** Enough draws that a clip never drawn is a real result rather than luck. */
	constexpr int32 Draws = 2000;

	static ACataclysmEnemyCharacter* SpawnUndressed(UWorld* World)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
														FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(100.0f);
		}
		return Spawned;
	}
}

// ---------------------------------------------------------------------------
// Which clip, out of how many
// ---------------------------------------------------------------------------

/**
 * A creature with two death clips uses both of them, and one with none is not
 * an error.
 *
 * THE INCLUSIVE BOUND IS THE MISTAKE WORTH GUARDING. FRandomStream::RandRange
 * includes both ends, so the count has to be passed with one subtracted. Written
 * without it, the Abyssal Warden would index past its array and be refused every
 * time -- which reads as a creature that simply has no death animation, not as
 * an off-by-one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathClipIsDrawn,
	"Cataclysm.EnemyDeath.ACreatureWithTwoDeathClipsUsesBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDeathClipIsDrawn::RunTest(const FString&)
{
	using namespace CataclysmEnemyDeathTest;

	FRandomStream Stream(1234);

	// NO CLIPS IS THE ORDINARY CASE. Five of the seven vertical slice creatures
	// have no art at all.
	TestEqual(TEXT("a creature with no death clips plays nothing"),
		FDeath::ClipToPlay(0, Stream), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and neither does a negative count"),
		FDeath::ClipToPlay(-3, Stream), static_cast<int32>(INDEX_NONE));

	// ONE CLIP IS ALWAYS THAT CLIP, however many times it is asked.
	for (int32 Attempt = 0; Attempt < 50; ++Attempt)
	{
		if (FDeath::ClipToPlay(1, Stream) != 0)
		{
			AddError(TEXT("a creature with one death clip drew something else"));
			break;
		}
	}

	// TWO CLIPS ARE BOTH USED, which is the Abyssal Warden. Counted rather than
	// sampled once, because a single draw cannot tell a fair choice from a
	// constant.
	int32 Counted[2] = { 0, 0 };
	for (int32 Draw = 0; Draw < Draws; ++Draw)
	{
		const int32 Index = FDeath::ClipToPlay(2, Stream);
		if (Index < 0 || Index > 1)
		{
			AddError(FString::Printf(
				TEXT("a two-clip draw answered %d, which is outside the array"),
				Index));
			return false;
		}
		++Counted[Index];
	}

	TestTrue(TEXT("the first death clip is used"), Counted[0] > 0);
	TestTrue(TEXT("and so is the second"), Counted[1] > 0);

	// AND NEITHER IS RARE. A wrong bound would not necessarily exclude a clip
	// outright; it could make one vanishingly unlikely.
	TestTrue(TEXT("neither clip is drawn less than a fifth of the time"),
		Counted[0] > Draws / 5 && Counted[1] > Draws / 5);

	return true;
}

// ---------------------------------------------------------------------------
// How long the body is kept
// ---------------------------------------------------------------------------

/**
 * The body is kept for exactly the clip, and a creature with no clip is not.
 *
 * THE ZERO CASE IS THE ONE THAT MATTERS. It is what every creature without art
 * reaches, and it has to mean "on the next tick, as before" rather than "wait
 * for no time and then something else".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCorpseIsKeptForItsClip,
	"Cataclysm.EnemyDeath.TheBodyIsKeptForExactlyTheLengthOfItsClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCorpseIsKeptForItsClip::RunTest(const FString&)
{
	using namespace CataclysmEnemyDeathTest;

	// THE TWO CLIPS THAT EXIST, measured in the editor on 2026-08-19.
	TestEqual(TEXT("the Abyssal Warden's body is kept for its 1.6667s clip"),
		FDeath::CorpseSecondsFor(1.6667f), 1.6667f);
	TestEqual(TEXT("and the Brute's for its 0.7667s one"),
		FDeath::CorpseSecondsFor(0.7667f), 0.7667f);

	// NOTHING TO PLAY MEANS NOTHING TO WAIT FOR.
	TestEqual(TEXT("a clip of no length keeps the body for no time"),
		FDeath::CorpseSecondsFor(0.0f), 0.0f);
	TestEqual(TEXT("and neither does a negative length"),
		FDeath::CorpseSecondsFor(-2.0f), 0.0f);

	// THE CEILING, which nothing today comes near.
	TestTrue(TEXT("the ceiling is longer than either clip that exists"),
		FDeath::LongestCorpseSeconds > 1.6667f);
	TestEqual(TEXT("a clip longer than the ceiling is cut to it"),
		FDeath::CorpseSecondsFor(FDeath::LongestCorpseSeconds + 26.0f),
		FDeath::LongestCorpseSeconds);
	TestEqual(TEXT("and one exactly at the ceiling is kept whole"),
		FDeath::CorpseSecondsFor(FDeath::LongestCorpseSeconds),
		FDeath::LongestCorpseSeconds);

	return true;
}

// ---------------------------------------------------------------------------
// What a creature actually does when it dies
// ---------------------------------------------------------------------------

/**
 * A creature with no art dies the way every creature did before issue #522.
 *
 * STATED RATHER THAN LEFT AS AN ACCIDENT. Five of the seven vertical slice
 * creatures have no art, and the sandbox's training dummies are the base class
 * itself. Going on the next tick is the right answer for a placeholder cylinder,
 * and it should be a decision somebody can read rather than a branch nobody
 * noticed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmUndressedEnemyGoesAtOnce,
	"Cataclysm.EnemyDeath.ACreatureWithNoArtIsRemovedOnTheNextTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmUndressedEnemyGoesAtOnce::RunTest(const FString&)
{
	using namespace CataclysmEnemyDeathTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy = SpawnUndressed(World);
	if (!TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	TestEqual(TEXT("it has no death clips before it dies"),
		Enemy->DeathAnimations.Num(), 0);

	Enemy->HandleDeath();

	TestNull(TEXT("it died with no clip"), Enemy->DiedWith.Get());
	TestEqual(TEXT("so its body is kept for no time at all"),
		Enemy->CorpseSeconds, 0.0f);

	// AND ITS PER-FRAME WORK STOPPED, whether or not it had a clip. A corpse
	// running its brain for a second would be worse than one running it for a
	// frame.
	TestFalse(TEXT("and it stopped ticking"), Enemy->IsActorTickEnabled());

	return true;
}

/**
 * A creature with art plays one of its own death clips and outlives the blow.
 *
 * THE SECOND HALF IS THE PART THAT WOULD OTHERWISE BREAK SILENTLY. Playing the
 * clip is easy; keeping it is not. The Abyssal Warden's UpdateLoopingAnimation
 * runs every frame and puts an idle loop on whenever the creature is not moving,
 * and a corpse is not moving, so without the per-frame work being stopped the
 * death pose would be replaced within a frame or two and the creature would
 * appear to stand up before vanishing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDressedEnemyPlaysItsDeath,
	"Cataclysm.EnemyDeath.ADressedCreaturePlaysOneOfItsOwnDeathClips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDressedEnemyPlaysItsDeath::RunTest(const FString&)
{
	using namespace CataclysmEnemyDeathTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmAbyssalWardenCharacter* Warden =
		World->SpawnActor<ACataclysmAbyssalWardenCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("an Abyssal Warden"), Warden))
	{
		return false;
	}
	Warden->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Warden->SetHealth(100.0f);

	// EVERY ENTRY IS NULL WITHOUT THE PACK, and the array is still two long,
	// because a clip that failed to load is kept rather than dropped.
	const bool bDressed = Warden->DeathAnimations.Num() > 0
		&& Warden->DeathAnimations[0] != nullptr;
	if (!bDressed)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Grux pack is not present, so the Abyssal Warden "
				 "has no death clip to play. Which clip is drawn and how long "
				 "the body is kept are still checked by the two tests above."));
		return true;
	}

	TestEqual(TEXT("it carries both of its death clips"),
		Warden->DeathAnimations.Num(), 2);

	Warden->HandleDeath();

	UAnimSequence* Played = Warden->DiedWith.Get();
	if (!TestNotNull(TEXT("it died with a clip"), Played))
	{
		return false;
	}

	TestTrue(TEXT("and the clip is one of its own"),
		Warden->DeathAnimations.Contains(Played));

	// THE BODY OUTLIVES THE KILLING BLOW BY EXACTLY THAT CLIP.
	TestEqual(TEXT("its body is kept for the clip's own length"),
		Warden->CorpseSeconds,
		FDeath::CorpseSecondsFor(Played->GetPlayLength()));
	TestTrue(TEXT("which is longer than nothing"), Warden->CorpseSeconds > 0.0f);

	// THE MESH IS HOLDING THE DEATH CLIP AND NOT A LOCOMOTION LOOP.
	//
	// ASKED OF THE SINGLE-NODE INSTANCE AND NOT OF AnimationData, which is a
	// distinction that cost a build. USkeletalMeshComponent::SetAnimation
	// hands the asset straight to the single-node instance and never writes
	// AnimationData, so reading that member reports whatever was last set in
	// the editor -- which for a creature dressed at runtime is nothing at all.
	USkeletalMeshComponent* MeshComponent = Warden->GetMesh();
	if (!TestNotNull(TEXT("it has a mesh"), MeshComponent))
	{
		return false;
	}

	TestEqual(TEXT("the mesh is off its animation graph"),
		MeshComponent->GetAnimationMode(),
		EAnimationMode::AnimationSingleNode);

	UAnimSingleNodeInstance* Node = MeshComponent->GetSingleNodeInstance();
	if (!TestNotNull(TEXT("and is playing a single clip"), Node))
	{
		return false;
	}

	TestEqual(TEXT("which is the death clip"),
		Node->GetAnimationAsset(), static_cast<UAnimationAsset*>(Played));
	TestFalse(TEXT("played once rather than looped"), Node->IsLooping());

	// AND NOTHING WILL TAKE IT BACK, because the creature's per-frame work is
	// over. Calling Tick directly is what a running world would do; without the
	// switch above it would put the idle loop on.
	TestFalse(TEXT("the creature stopped ticking"),
		Warden->IsActorTickEnabled());

	Warden->Tick(0.016f);
	TestEqual(TEXT("so a tick does not replace the death clip"),
		MeshComponent->GetSingleNodeInstance()
			? MeshComponent->GetSingleNodeInstance()->GetAnimationAsset()
			: nullptr,
		static_cast<UAnimationAsset*>(Played));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
