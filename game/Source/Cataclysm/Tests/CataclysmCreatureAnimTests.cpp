// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AnimationRuntime.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmCreatureAnimInstance.h"
#include "Character/CataclysmImpCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Additive clips played over a pose, not on the reference pose. Issue #2976.
 *
 * WHAT WAS WRONG. Every Paragon pack's hit reactions are ADDITIVE clips, and creatures with no animation Blueprint
 * played them with `PlayAnimation`, in single-node mode, which resets the mesh to its reference pose and adds the clip
 * to that (UE 5.8 AnimSingleNodeInstanceProxy.cpp lines 331-350). `UCataclysmCreatureAnimInstance` plays them into a
 * slot over the creature's loop instead, and its static functions refuse an additive clip in single-node mode.
 *
 * THE ART. Every test that plays a clip needs the Paragon Minions pack, which is gitignored and exists only in the main
 * checkout. Without it each reports its skipped half and checks only what needs no clip. A window that is to prove
 * these copies the pack into its worktree first (never links it; see the decisions log).
 *
 * NOT COMPILED AND NOT RUN BY THE SESSION THAT WROTE IT.
 */
namespace CataclysmCreatureAnimTest
{
	ACataclysmImpCharacter* SpawnDressedImp(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmImpCharacter* Imp =
			World->SpawnActor<ACataclysmImpCharacter>(ACataclysmImpCharacter::StaticClass(), Where, FRotator::ZeroRotator,
													  Params);
		if (Imp)
		{
			Imp->ResolveBody(/*bIncludeAnimation=*/true);
		}
		return Imp;
	}

	/** A bare skeletal mesh in single-node mode, wearing `Body`: the way every creature played its clips before. */
	USkeletalMeshComponent* MakeSingleNodeMesh(UWorld* World, USkeletalMesh* Body)
	{
		AActor* Holder = World->SpawnActor<AActor>();
		if (!Holder)
		{
			return nullptr;
		}
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Holder);
		Holder->SetRootComponent(Mesh);
		Mesh->RegisterComponent();
		Mesh->SetSkeletalMesh(Body);
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		return Mesh;
	}

	/** The animation updated and the pose evaluated at once: no tick function, so no parallel evaluation. */
	void Advance(USkeletalMeshComponent* Mesh, const float Seconds)
	{
		constexpr float Step = 0.05f;
		for (float Done = 0.0f; Done < Seconds - KINDA_SMALL_NUMBER; Done += Step)
		{
			Mesh->TickAnimation(Step, /*bNeedsValidRootMotion=*/false);
			Mesh->RefreshBoneTransforms(nullptr);
		}
	}

	/** Every bone's location in component space. */
	TArray<FVector> BoneLocations(const USkeletalMeshComponent* Mesh)
	{
		TArray<FVector> Out;
		for (const FTransform& Bone : Mesh->GetComponentSpaceTransforms())
		{
			Out.Add(Bone.GetLocation());
		}
		return Out;
	}

	/** The reference pose's bone locations in component space, worked out from the mesh's own skeleton. */
	TArray<FVector> ReferenceLocations(const USkeletalMesh* Body)
	{
		const FReferenceSkeleton& Skeleton = Body->GetRefSkeleton();
		TArray<FTransform> ComponentSpace;
		FAnimationRuntime::FillUpComponentSpaceTransforms(Skeleton, Skeleton.GetRefBonePose(), ComponentSpace);
		TArray<FVector> Out;
		for (const FTransform& Bone : ComponentSpace)
		{
			Out.Add(Bone.GetLocation());
		}
		return Out;
	}

	/** The mean distance between two poses' bones, in cm; -1 when they do not have the same bones. */
	float MeanDistance(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		if (A.Num() == 0 || A.Num() != B.Num())
		{
			return -1.0f;
		}
		double Sum = 0.0;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			Sum += FVector::Dist(A[Index], B[Index]);
		}
		return static_cast<float>(Sum / A.Num());
	}
}

/**
 * The guard. In single-node mode an additive clip is refused, as a loop or a one-shot; on the instance it plays into the
 * slot. CONTROL: the Imp's hit reaction is additive and its idle is not. Without the art, only what needs no clip:
 * nothing plays on a null mesh or a null clip, and the instance is not put on a mesh with no skeletal mesh asset.
 * Prints "ADDITIVE GUARD".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreatureAnimGuardTest,
	"Cataclysm.CreatureAnim.AnAdditiveClipIsRefusedInSingleNodeAndPlaysIntoTheSlotOnTheInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreatureAnimGuardTest::RunTest(const FString&)
{
	using namespace CataclysmCreatureAnimTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// WHAT NEEDS NO CLIP.
	USkeletalMeshComponent* Bare = MakeSingleNodeMesh(World, nullptr);
	TestNotNull(TEXT("set-up: a bare mesh"), Bare);
	TestNull(TEXT("the instance is not put on a mesh with no skeletal mesh asset"),
			 UCataclysmCreatureAnimInstance::UseOn(Bare));
	TestFalse(TEXT("nothing loops on a null mesh"), UCataclysmCreatureAnimInstance::PlayLoop(nullptr, nullptr, true));
	TestEqual(TEXT("nothing plays once on a null clip"), UCataclysmCreatureAnimInstance::PlayOneShot(Bare, nullptr), 0.0f);

	ACataclysmImpCharacter* Imp = SpawnDressedImp(World, FVector::ZeroVector);
	USkeletalMesh* Body = Imp && Imp->GetMesh() ? Imp->GetMesh()->GetSkeletalMeshAsset() : nullptr;
	UAnimSequence* HitReact =
		Imp && Imp->HitReactionAnimations.IsValidIndex(0) ? Imp->HitReactionAnimations[0].Get() : nullptr;
	if (!Body || !HitReact || !Imp->IdleAnimation)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is not present, so there is no additive clip: whether one is refused in "
				 "single-node mode and played into the slot on the instance was NOT checked."));
		return true;
	}

	USkeletalMeshComponent* SingleNode = MakeSingleNodeMesh(World, Body);
	if (!TestNotNull(TEXT("set-up: a single-node mesh wearing the Imp's body"), SingleNode))
	{
		return false;
	}

	const int32 RefusedBefore = UCataclysmCreatureAnimInstance::AdditiveClipsRefused;
	const float SingleNodeOneShot = UCataclysmCreatureAnimInstance::PlayOneShot(SingleNode, HitReact);
	const bool bSingleNodeLoop = UCataclysmCreatureAnimInstance::PlayLoop(SingleNode, HitReact, /*bLooping=*/true);
	const int32 Refused = UCataclysmCreatureAnimInstance::AdditiveClipsRefused - RefusedBefore;

	UCataclysmCreatureAnimInstance* Instance = UCataclysmCreatureAnimInstance::On(Imp->GetMesh());
	const float InstanceOneShot = Instance ? UCataclysmCreatureAnimInstance::PlayOneShot(Imp->GetMesh(), HitReact) : 0.0f;
	const bool bInSlot = Instance && Instance->IsPlayingSlotAnimation(HitReact, UCataclysmCreatureAnimInstance::SlotName);

	AddInfo(FString::Printf(TEXT("ADDITIVE GUARD: %s additive %s, idle additive %s; single-node one-shot %.3f s, loop %s, "
								 "refused %d; the Imp on the instance %s, one-shot %.3f s, in the slot %s"),
							*HitReact->GetName(), HitReact->IsValidAdditive() ? TEXT("yes") : TEXT("NO"),
							Imp->IdleAnimation->IsValidAdditive() ? TEXT("YES") : TEXT("no"), SingleNodeOneShot,
							bSingleNodeLoop ? TEXT("PLAYED") : TEXT("refused"), Refused,
							Instance ? TEXT("yes") : TEXT("NO"), InstanceOneShot, bInSlot ? TEXT("yes") : TEXT("NO")));

	TestTrue(TEXT("CONTROL: the Imp's hit reaction is an additive clip"), HitReact->IsValidAdditive());
	TestFalse(TEXT("CONTROL: its idle is not"), Imp->IdleAnimation->IsValidAdditive());
	TestEqual(TEXT("in single-node mode the additive clip is refused as a one-shot"), SingleNodeOneShot, 0.0f);
	TestFalse(TEXT("and as a loop"), bSingleNodeLoop);
	TestEqual(TEXT("and both refusals are counted"), Refused, 2);
	TestNotNull(TEXT("the Imp plays on the instance"), Instance);
	TestTrue(TEXT("where the additive clip plays, for its length"), InstanceOneShot > 0.0f);
	TestTrue(TEXT("into the slot"), bInSlot);
	return true;
}

/**
 * The proof. A hit reaction played on the instance is added to the Imp's idle; played in single-node mode, the way it
 * was, it is added to the reference pose. Measured as each pose's mean bone distance, in component space, from the idle
 * at the same moment and from the reference pose.
 *
 * CONTROL: the single-node flinch is nearer the reference pose than the idle, which is the fault the engine's warning
 * describes. THE FIX: the instance's flinch is nearer the idle than the reference pose, and is not the idle itself (the
 * clip was added). Without the art, nothing to measure. Prints "FLINCH POSE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreatureAnimPoseTest,
	"Cataclysm.CreatureAnim.AFlinchIsAddedToTheLoopAndNotToTheReferencePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreatureAnimPoseTest::RunTest(const FString&)
{
	using namespace CataclysmCreatureAnimTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// TWO IMPS, ONE ONLY IDLING AND ONE FLINCHING OVER THE SAME IDLE, so the two poses are of the same moment.
	ACataclysmImpCharacter* Idling = SpawnDressedImp(World, FVector::ZeroVector);
	ACataclysmImpCharacter* Flinching = SpawnDressedImp(World, FVector(500.0f, 0.0f, 0.0f));
	USkeletalMesh* Body = Idling && Idling->GetMesh() ? Idling->GetMesh()->GetSkeletalMeshAsset() : nullptr;
	UAnimSequence* HitReact = Flinching && Flinching->HitReactionAnimations.IsValidIndex(0)
		? Flinching->HitReactionAnimations[0].Get()
		: nullptr;
	if (!Body || !HitReact || !Idling->IdleAnimation)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the Paragon Minions pack is not present, so there is no pose to measure: whether a flinch is added to "
				 "the idle rather than to the reference pose was NOT checked."));
		return true;
	}
	USkeletalMeshComponent* IdleMesh = Idling->GetMesh();
	USkeletalMeshComponent* FlinchMesh = Flinching->GetMesh();
	USkeletalMeshComponent* SingleNode = MakeSingleNodeMesh(World, Body);
	if (!TestNotNull(TEXT("set-up: the Imp plays on the instance"), UCataclysmCreatureAnimInstance::On(IdleMesh))
		|| !TestNotNull(TEXT("set-up: and so does the second"), UCataclysmCreatureAnimInstance::On(FlinchMesh))
		|| !TestNotNull(TEXT("set-up: a single-node mesh wearing the Imp's body"), SingleNode))
	{
		return false;
	}

	// BOTH IMPS IDLE; THE SECOND FLINCHES WITH NO BLEND IN, so the clip is fully on when the poses are read.
	UCataclysmCreatureAnimInstance::PlayLoop(IdleMesh, Idling->IdleAnimation, /*bLooping=*/true);
	UCataclysmCreatureAnimInstance::PlayLoop(FlinchMesh, Flinching->IdleAnimation, /*bLooping=*/true);
	Advance(IdleMesh, 0.1f);
	Advance(FlinchMesh, 0.1f);
	const float Played = UCataclysmCreatureAnimInstance::PlayOneShot(FlinchMesh, HitReact, /*PlayRate=*/1.0f,
																	/*BlendInSeconds=*/0.0f);

	// THE WAY IT WAS: the same clip in single-node mode, called directly, as the creatures did. This test file is the
	// one place outside the helper where that call is allowed, because it is the control.
	SingleNode->PlayAnimation(HitReact, /*bLooping=*/false);

	// A THIRD OF THE CLIP IN, where a hit reaction is near its largest.
	const float Into = FMath::Clamp(HitReact->GetPlayLength() / 3.0f, 0.05f, 0.5f);
	Advance(IdleMesh, Into);
	Advance(FlinchMesh, Into);
	Advance(SingleNode, Into);

	const TArray<FVector> Idle = BoneLocations(IdleMesh);
	const TArray<FVector> Flinch = BoneLocations(FlinchMesh);
	const TArray<FVector> OnReference = BoneLocations(SingleNode);
	const TArray<FVector> Reference = ReferenceLocations(Body);

	const float IdleFromReference = MeanDistance(Idle, Reference);
	const float OldFromReference = MeanDistance(OnReference, Reference);
	const float OldFromIdle = MeanDistance(OnReference, Idle);
	const float NewFromReference = MeanDistance(Flinch, Reference);
	const float NewFromIdle = MeanDistance(Flinch, Idle);

	AddInfo(FString::Printf(TEXT("FLINCH POSE: %s, %d bones, read %.2f s into the clip (it plays %.3f s on the instance); "
								 "mean bone distance, cm: the idle from the reference pose %.2f; SINGLE-NODE flinch from the "
								 "reference pose %.2f, from the idle %.2f; INSTANCE flinch from the reference pose %.2f, "
								 "from the idle %.2f"),
							*HitReact->GetName(), Idle.Num(), Into, Played, IdleFromReference, OldFromReference,
							OldFromIdle, NewFromReference, NewFromIdle));

	if (!TestTrue(TEXT("set-up: every pose has the same bones"),
				  IdleFromReference >= 0.0f && OldFromReference >= 0.0f && NewFromIdle >= 0.0f)
		|| !TestTrue(TEXT("set-up: the flinch played on the instance"), Played > 0.0f)
		|| !TestTrue(TEXT("set-up: the idle is not the reference pose, so the two can be told apart"),
					 IdleFromReference > 1.0f))
	{
		return false;
	}
	TestTrue(TEXT("CONTROL: in single-node mode the flinch is nearer the reference pose than the idle"),
			 OldFromReference < OldFromIdle);
	TestTrue(TEXT("on the instance the flinch is nearer the idle than the reference pose"), NewFromIdle < NewFromReference);
	TestTrue(TEXT("and it is not the idle itself: the clip was added"), NewFromIdle > 0.1f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
