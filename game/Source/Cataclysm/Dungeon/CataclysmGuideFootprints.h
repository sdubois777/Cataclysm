// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmGuideFootprints.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UWorld;

/**
 * The yellow footprints Demonic Guide's guide leaves on the floor behind it, fading as they age, so a player who has
 * lost sight of it can follow. Issue #2862, the owner's request of 2026-11-23: "I also think we should show like yellow
 * footsteps or something pointing to where he's gone. He's kinda easy to lose track of." Every figure is on
 * `UCataclysmDungeonModifierEffects`, beside the guide's others, with the reason for each.
 *
 * ONE ACTOR FOR THE GUIDE'S WHOLE TRAIL, HOLDING A FIXED POOL OF PRINTS, and never an actor a step. The pool is made
 * once, as the guide is raised, and the print laid longest ago is taken for the next one; a print that has faded is
 * hidden and waits for its turn. Made by `ACataclysmDungeonGameMode::PlaceTheGuide` and destroyed by `ForgetTheGuide`,
 * so a trail lasts exactly as long as its guide.
 *
 * STEPPED BY THE GAME MODE'S BEAT, NOT BY ITS OWN TICK, from `ACataclysmDungeonGameMode::StepDemonicGuide`: the beat
 * already reads the guide and whether it waits, and an automation test's world never ticks, so a public `Step` is
 * what lets a test walk a guide and watch its prints come and go, as `ACataclysmHarvestedSoul::Step` does.
 *
 * DRAWN WITH WHAT THE PROJECT ALREADY HAS, so no asset is added: the engine's cylinder, squashed to an oval, and the
 * telegraph markers' material, `/Game/Effects/M_TelegraphMarker`, which is unlit and translucent with a colour and an
 * opacity to set (`tools/generate_telegraph_material.py`). Unlit, so a print reads the same in a dark room as in a lit
 * one. A decal was not used because the project holds no deferred decal material. Without the material a print is
 * drawn in the engine's default and does not fade, and it still goes at the end of its life; the log says so once.
 *
 * NOTHING COLLIDES WITH A PRINT AND NOTHING FINDS ONE: no collision, no ability system, no shadow.
 */
UCLASS()
class CATACLYSM_API ACataclysmGuideFootprints : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmGuideFootprints();

	/** A trail with no prints yet, in `World`. Null when there is no world. */
	static ACataclysmGuideFootprints* Begin(UWorld* World, const FVector& Where);

	/**
	 * One beat of the guide's walk: the prints already down age by `Seconds` and fade, unless the guide waits, and one
	 * print is laid every `DemonicGuideFootprintSpacingCm` along the line from the last print to `FeetAt`, left and
	 * right in turn. The first step lays one print under the guide. A move longer than `DemonicGuideFootprintJumpCm`
	 * starts the trail again at `FeetAt`. PUBLIC SO A TEST CAN DRIVE IT.
	 *
	 * @param FeetAt       where the guide's feet are now: the bottom of its capsule
	 * @param Facing       the way the guide faces, which points a print laid where it stands
	 * @param Seconds      the time the beat covered
	 * @param bGuideWaits  whether the guide is waiting for the player; prints do not age while it is
	 * @return how many prints this step laid
	 */
	int32 Step(const FVector& FeetAt, const FRotator& Facing, float Seconds, bool bGuideWaits);

	/** How many prints are on the floor now. */
	int32 PrintsShown() const;

	/** How many of the pool's components are drawn now, which `PrintsShown` must equal. For tests. */
	int32 PrintComponentsVisible() const;

	/** How many prints the pool holds, `DemonicGuideFootprintsAtMost`. */
	int32 PoolSize() const { return Prints.Num(); }

	/** How many prints this trail has laid, ever. */
	int32 PrintsLaid() const { return Laid; }

	/**
	 * Every print on the floor now, the oldest first: where it lies and how opaque it is drawn. For tests and the log.
	 */
	void ShownPrints(TArray<FVector>& OutWhere, TArray<float>& OutOpacity) const;

	/** How opaque a print `AgeSeconds` old is drawn: `DemonicGuideFootprintOpacity` down to nought, evenly. */
	static float OpacityAtAge(float AgeSeconds);

	/** How thick a print is, and how far above the guide's feet it lies, in centimetres: flat, and clear of the floor. */
	static constexpr float PrintThicknessCm = 1.0f;
	static constexpr float PrintLiftCm = 2.0f;

private:
	/** Puts the pool's next print at `Centre`, offset to the side whose turn it is, pointing along `Along`. */
	void LayAt(const FVector& Centre, const FVector& Along);

	/** Draws the print in `Slot` at the opacity its age gives, making its material instance the first time. */
	void Paint(int32 Slot);

	/** Hides the print in `Slot` and marks it free. */
	void Lift(int32 Slot);

	/** The prints, the pool. Made in the constructor and never added to. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Prints;

	/** Each print's material instance, made the first time it is drawn; null while it has not been or with no material. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Paints;

	/** The telegraph markers' material, found by path in the constructor; null when it is not there. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PrintMaterial;

	/** Each print's age in seconds, or below nought while it is not on the floor. */
	TArray<float> Ages;

	/** The slot the next print takes: the one laid longest ago. */
	int32 NextSlot = 0;

	/** Whether a print has been laid, and where on the guide's line the last one was. */
	bool bStarted = false;
	FVector LastPrintAt = FVector::ZeroVector;

	/** Whether the next print goes on the guide's left. */
	bool bNextOnTheLeft = true;

	/** How many prints have been laid. */
	int32 Laid = 0;

	/** Whether the missing material has been said in the log yet. */
	bool bSaidNoMaterial = false;
};
