// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmCreatureAnimInstance.h"
#include "Cataclysm.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSyncScope.h"
#include "Animation/AnimTypes.h"
#include "Components/SkeletalMeshComponent.h"

const FName UCataclysmCreatureAnimInstance::SlotName(TEXT("DefaultSlot"));
int32 UCataclysmCreatureAnimInstance::AdditiveClipsRefused = 0;

/**
 * The worker-thread half. It does what an animation graph of "sequence player -> Slot -> output" would, with no graph:
 * `UpdateAnimationNode` keeps the slot registered and its weight current and ticks the loop; `Evaluate` evaluates the
 * loop and, while a montage plays in the slot, passes it to `SlotEvaluatePose` as the source pose.
 */
struct FCataclysmCreatureAnimInstanceProxy : public FAnimInstanceProxy
{
	explicit FCataclysmCreatureAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

		// THE GAME THREAD'S REQUEST, copied before the worker thread reads it. A new request restarts the loop's time.
		const UCataclysmCreatureAnimInstance* Instance = CastChecked<UCataclysmCreatureAnimInstance>(InAnimInstance);
		Loop = Instance->LoopClip;
		LoopRate = Instance->LoopRate;
		bLoopLooping = Instance->bLoopLooping;
		if (Instance->LoopRequests != LoopRequestsSeen)
		{
			LoopRequestsSeen = Instance->LoopRequests;
			LoopTime = 0.0f;
			DeltaTimeRecord = FDeltaTimeRecord();
			MarkerTickRecord.Reset();
		}
	}

	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override
	{
		// THE SLOT, AS `FAnimNode_Slot` KEEPS IT: registered again whenever the instance's slot counter moves, and its
		// weight read and stored each update, which is what lets a montage played into it blend in and out.
		if (!SlotRegistration.IsSynchronized_Counter(GetSlotNodeInitializationCounter()))
		{
			SlotRegistration.SynchronizeWith(GetSlotNodeInitializationCounter());
			RegisterSlotNodeWithAnimInstance(UCataclysmCreatureAnimInstance::SlotName);
		}
		GetSlotWeight(UCataclysmCreatureAnimInstance::SlotName, SlotNodeWeight, SourceWeight, TotalNodeWeight);
		UpdateSlotNodeWeight(UCataclysmCreatureAnimInstance::SlotName, SlotNodeWeight, InContext.GetFinalBlendWeight());

		// THE LOOP, TICKED AS THE ENGINE'S SINGLE-NODE PLAYER TICKS A SEQUENCE: through a tick record, which advances
		// its time, wraps or holds it, and fires its notifies.
		if (Loop)
		{
			UE::Anim::FAnimSyncGroupScope& SyncScope = InContext.GetMessageChecked<UE::Anim::FAnimSyncGroupScope>();
			FAnimTickRecord TickRecord(Loop, bLoopLooping, LoopRate, /*bInIsEvaluator=*/false, /*InFinalBlendWeight=*/1.0f,
									   /*inout*/ LoopTime, MarkerTickRecord);
			TickRecord.DeltaTimeRecord = &DeltaTimeRecord;
			SyncScope.AddTickRecord(TickRecord);
		}
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		// NO MONTAGE IN THE SLOT: the loop alone, as the slot node passes its source through.
		if (SlotNodeWeight <= ZERO_ANIMWEIGHT_THRESH)
		{
			EvaluateLoop(Output);
			return true;
		}

		FPoseContext SourceContext(Output);
		if (SourceWeight > ZERO_ANIMWEIGHT_THRESH)
		{
			EvaluateLoop(SourceContext);
		}
		else
		{
			SourceContext.ResetToRefPose();
		}
		const FAnimationPoseData SourcePoseData(SourceContext);
		FAnimationPoseData OutputPoseData(Output);
		SlotEvaluatePose(UCataclysmCreatureAnimInstance::SlotName, SourcePoseData, SourceWeight, OutputPoseData,
						 SlotNodeWeight, TotalNodeWeight);
		return true;
	}

	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override
	{
		FAnimInstanceProxy::PostUpdate(InAnimInstance);
		CastChecked<UCataclysmCreatureAnimInstance>(InAnimInstance)->LoopTimeSeconds = LoopTime;
	}

private:
	/** The loop's pose; the reference pose with none, as a creature with no clip stands. Never additive (refused). */
	void EvaluateLoop(FPoseContext& Output) const
	{
		if (!Loop)
		{
			Output.ResetToRefPose();
			return;
		}
		FAnimationPoseData PoseData(Output);
		const FAnimExtractContext Extraction(static_cast<double>(LoopTime), Loop->bEnableRootMotion, DeltaTimeRecord,
											 bLoopLooping);
		Loop->GetAnimationPose(PoseData, Extraction);
	}

	UAnimSequence* Loop = nullptr;
	float LoopRate = 1.0f;
	bool bLoopLooping = true;
	int32 LoopRequestsSeen = 0;
	float LoopTime = 0.0f;
	FDeltaTimeRecord DeltaTimeRecord;
	FMarkerTickRecord MarkerTickRecord;

	FGraphTraversalCounter SlotRegistration;
	float SlotNodeWeight = 0.0f;
	float SourceWeight = 1.0f;
	float TotalNodeWeight = 0.0f;
};

FAnimInstanceProxy* UCataclysmCreatureAnimInstance::CreateAnimInstanceProxy()
{
	return new FCataclysmCreatureAnimInstanceProxy(this);
}

UCataclysmCreatureAnimInstance* UCataclysmCreatureAnimInstance::UseOn(USkeletalMeshComponent* Mesh)
{
	if (!Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		return nullptr;
	}
	if (UCataclysmCreatureAnimInstance* Already = On(Mesh))
	{
		return Already;
	}
	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(StaticClass());
	return On(Mesh);
}

UCataclysmCreatureAnimInstance* UCataclysmCreatureAnimInstance::On(const USkeletalMeshComponent* Mesh)
{
	return Mesh && Mesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint
		? Cast<UCataclysmCreatureAnimInstance>(Mesh->GetAnimInstance())
		: nullptr;
}

bool UCataclysmCreatureAnimInstance::RefuseAdditiveOnSingleNode(const USkeletalMeshComponent* Mesh,
																const UAnimSequenceBase* Clip)
{
	if (!Clip || !Clip->IsValidAdditive())
	{
		return false;
	}
	++AdditiveClipsRefused;

	// ONCE PER CLIP NAME, so a creature flinching all fight long logs one line and not hundreds.
	static TSet<FName> Said;
	bool bAlready = false;
	Said.Add(Clip->GetFName(), &bAlready);
	if (!bAlready)
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("%s is an additive clip and %s plays in single-node mode, which would show it on the reference pose; "
					"it is not played. Put the mesh on UCataclysmCreatureAnimInstance to play it over a pose."),
			   *Clip->GetName(), Mesh && Mesh->GetOwner() ? *Mesh->GetOwner()->GetName() : TEXT("a mesh"));
	}
	return true;
}

bool UCataclysmCreatureAnimInstance::PlayLoop(USkeletalMeshComponent* Mesh, UAnimSequence* Clip, bool bLooping,
											  float PlayRate)
{
	if (!Mesh || !Clip)
	{
		return false;
	}
	if (Clip->IsValidAdditive())
	{
		RefuseAdditiveOnSingleNode(Mesh, Clip); // Counted and logged on either path: a loop needs a pose of its own.
		return false;
	}
	if (UCataclysmCreatureAnimInstance* Instance = On(Mesh))
	{
		Instance->LoopClip = Clip;
		Instance->LoopRate = PlayRate;
		Instance->bLoopLooping = bLooping;
		++Instance->LoopRequests;
		return true;
	}

	// THE ONE SINGLE-NODE CALL FOR LOOPS. `tools/tests/test_creature_clips_go_through_one_helper.py` holds it here.
	Mesh->PlayAnimation(Clip, bLooping);
	Mesh->SetPlayRate(PlayRate);
	return true;
}

float UCataclysmCreatureAnimInstance::PlayOneShot(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Clip, float PlayRate,
												  float BlendInSeconds, float BlendOutSeconds)
{
	if (!Mesh || !Clip || PlayRate <= 0.0f)
	{
		return 0.0f;
	}
	const float Length = Clip->GetPlayLength();
	if (Length <= 0.0f)
	{
		return 0.0f;
	}

	if (UCataclysmCreatureAnimInstance* Instance = On(Mesh))
	{
		// INTO THE SLOT, as the Brute plays its swings and flinches. A montage that cannot be made plays nothing.
		const UAnimMontage* Montage = Instance->PlaySlotAnimationAsDynamicMontage(
			Clip, SlotName, BlendInSeconds, BlendOutSeconds, PlayRate, /*LoopCount=*/1);
		return Montage ? Length / PlayRate : 0.0f;
	}

	if (RefuseAdditiveOnSingleNode(Mesh, Clip))
	{
		return 0.0f;
	}

	// THE ONE SINGLE-NODE CALL FOR ONE-SHOTS. It plays once and holds its last frame, as it always has.
	Mesh->PlayAnimation(Clip, /*bLooping=*/false);
	Mesh->SetPlayRate(PlayRate);
	return Length / PlayRate;
}

void UCataclysmCreatureAnimInstance::StopOneShots(USkeletalMeshComponent* Mesh, float BlendOutSeconds)
{
	if (UCataclysmCreatureAnimInstance* Instance = On(Mesh))
	{
		Instance->StopSlotAnimation(BlendOutSeconds, SlotName);
	}
}

void UCataclysmCreatureAnimInstance::SetLoopPlayRate(USkeletalMeshComponent* Mesh, float PlayRate)
{
	if (UCataclysmCreatureAnimInstance* Instance = On(Mesh))
	{
		Instance->LoopRate = PlayRate;
		return;
	}
	if (Mesh)
	{
		Mesh->SetPlayRate(PlayRate);
	}
}

float UCataclysmCreatureAnimInstance::LoopPlayRate(const USkeletalMeshComponent* Mesh)
{
	if (const UCataclysmCreatureAnimInstance* Instance = On(Mesh))
	{
		return Instance->LoopRate;
	}
	return Mesh && Mesh->GetSingleNodeInstance() ? Mesh->GetPlayRate() : 0.0f;
}
