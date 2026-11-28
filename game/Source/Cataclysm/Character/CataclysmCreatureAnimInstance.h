// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"

#include "CataclysmCreatureAnimInstance.generated.h"

class UAnimSequence;
class UAnimSequenceBase;
class USkeletalMeshComponent;

/**
 * A creature's animation with no animation Blueprint: one clip looping (or played once and held) under a slot, and
 * one-shot clips played into that slot on top of it. Issue #2976.
 *
 * WHY. Creatures with no animation Blueprint played every clip with `USkeletalMeshComponent::PlayAnimation`, which
 * puts the mesh in single-node mode. The hit reactions of every Paragon pack, the Corrupted Sentinel's planted shots
 * and the Hellhound's idle are ADDITIVE clips: each stores a difference from a base pose. Single-node play of an
 * additive clip resets the mesh to its REFERENCE POSE and adds the clip to that (UE 5.8 AnimSingleNodeInstanceProxy.cpp
 * lines 331-350); the branch that would use a base pose exists only in an editor preview
 * (`bCanProcessAdditiveAnimations`, compiled out of non-editor builds). The engine warns on every such call that it
 * "will not function correctly in cooked builds".
 *
 * HOW THE PACKS INTEND THEM. Their own animation Blueprints are state machines of sequence players with Slot and
 * ApplyAdditive nodes, and name no hit reaction: gameplay plays the hit reaction into a slot over the locomotion pose.
 * The Brute does exactly that through ABP_Brute's DefaultSlot. This class does the same with no Blueprint asset: its
 * proxy evaluates the looping clip as the slot's source pose and hands it to the engine's own `SlotEvaluatePose`, which
 * adds an additive montage on top of the source and blends a full-pose one over it (AnimInstanceProxy.cpp about line
 * 1951). The slot is registered and its weight kept as `FAnimNode_Slot` does (AnimNode_Slot.cpp).
 *
 * USE THE STATIC FUNCTIONS, which every clip a creature plays goes through. They play on this instance when the mesh has
 * one, and otherwise in single-node mode, where an additive clip is REFUSED and logged rather than shown on the
 * reference pose. `tools/tests/test_creature_clips_go_through_one_helper.py` fails on a `->PlayAnimation(` anywhere else.
 */
UCLASS(Transient, NotBlueprintable)
class CATACLYSM_API UCataclysmCreatureAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** The slot one-shots play into: the engine's default, the name ABP_Brute's slot has too. */
	static const FName SlotName;

	/**
	 * Puts this instance on `Mesh`, replacing single-node mode. Nothing without a skeletal mesh asset, which is a
	 * creature without its art: the engine makes no anim instance for a mesh with no skeleton. Returns the instance,
	 * or null.
	 */
	static UCataclysmCreatureAnimInstance* UseOn(USkeletalMeshComponent* Mesh);

	/** This instance on `Mesh`, or null when the mesh plays in single-node mode or has no instance. */
	static UCataclysmCreatureAnimInstance* On(const USkeletalMeshComponent* Mesh);

	/**
	 * Plays `Clip` as the pose under the slot, from its start: looping, or once and then holding its last frame (a death
	 * clip). On a mesh without this instance, single-node `PlayAnimation`. FALSE, AND NOTHING PLAYED, for a null clip,
	 * a null mesh, and an ADDITIVE clip on either path: an additive clip has no pose of its own to stand on.
	 */
	static bool PlayLoop(USkeletalMeshComponent* Mesh, UAnimSequence* Clip, bool bLooping, float PlayRate = 1.0f);

	/**
	 * Plays `Clip` once into the slot, over the loop: an additive clip is added to it, a full-pose clip blended over
	 * it. On a mesh without this instance, single-node `PlayAnimation` (full-pose clips only; an additive one is
	 * refused). Returns how long it plays at `PlayRate`, or 0 when nothing was played.
	 */
	static float PlayOneShot(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Clip, float PlayRate = 1.0f,
							 float BlendInSeconds = DefaultBlendSeconds, float BlendOutSeconds = DefaultBlendSeconds);

	/** Stops every one-shot in the slot, blending out over `BlendOutSeconds`; nothing on a mesh without this instance. */
	static void StopOneShots(USkeletalMeshComponent* Mesh, float BlendOutSeconds = DefaultBlendSeconds);

	/** Changes the loop's rate without restarting it; on a mesh without this instance, `SetPlayRate`. */
	static void SetLoopPlayRate(USkeletalMeshComponent* Mesh, float PlayRate);

	/** The loop's rate, or on a mesh without this instance the single-node rate; 0 with neither. */
	static float LoopPlayRate(const USkeletalMeshComponent* Mesh);

	/**
	 * How long a one-shot blends in and out over the loop. The Brute's figure (`AttackBlendInSeconds`), a judgement of
	 * the session that built it; single-node play had none and cut between clips.
	 */
	static constexpr float DefaultBlendSeconds = 0.15f;

	/** The clip under the slot, its rate, whether it loops, and its time as of the last animation update. */
	UAnimSequence* GetLoopClip() const { return LoopClip; }
	float GetLoopRate() const { return LoopRate; }
	bool IsLoopLooping() const { return bLoopLooping; }
	float GetLoopTimeSeconds() const { return LoopTimeSeconds; }

	/** Counted for tests: additive clips refused by the single-node path since the process began. */
	static int32 AdditiveClipsRefused;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	friend struct FCataclysmCreatureAnimInstanceProxy;

	/** The clip under the slot. A property, so the clip is held while it plays. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LoopClip;

	float LoopRate = 1.0f;
	bool bLoopLooping = true;

	/** Counts `PlayLoop` calls, so the proxy restarts the loop's time on each, the same clip included. */
	int32 LoopRequests = 0;

	/** Copied back from the proxy after each update, for tests and the log. */
	float LoopTimeSeconds = 0.0f;

	/** The single-node path's refusal, in one place: logs once per clip name. */
	static bool RefuseAdditiveOnSingleNode(const USkeletalMeshComponent* Mesh, const UAnimSequenceBase* Clip);
};
