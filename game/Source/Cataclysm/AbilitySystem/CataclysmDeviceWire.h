// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmDeviceWire.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;

/**
 * One of Cat's Cradle's wires, seen: a thin line between two of the player's devices.
 *
 * WHAT ASKS FOR IT. The Saboteur's Second Oath, option 3, Cat's Cradle (the War vow options' fifth slice, second half,
 * second part, V5b2, 2026-11-25): "Each of your traps and gadgets is joined by a wire to every other one within 10
 * metres." The coordinating session ruled C2 that the wires are seen: a thin line drawn between each pair, refreshed at
 * the player's quarter-second step. The owner's standing rule is that every system needs a basic interface.
 *
 * IT DOES NOTHING BUT SHOW ITSELF. Which enemies cross a wire is measured by
 * `UCataclysmAbilitySystemComponent::StepCatsCradle` against the devices' own places, never against this actor, so a
 * wire that failed to spawn changes nothing in play. No collision, no tick, no shadow.
 *
 * ONE ACTOR FOR EACH WIRE, held and re-stretched by the player's ability system component at its step, and taken down
 * there when its pair is no longer within reach, when a device is gone, or when the player no longer holds the option.
 * The engine's cube, squashed to a bar, in the telegraph markers' unlit material: no asset is added to the project.
 */
UCLASS()
class CATACLYSM_API ACataclysmDeviceWire : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmDeviceWire();

	/** How thick a wire is drawn, in centimetres: thin, and still seen from the game's camera. A judgement. */
	static constexpr float ThicknessCm = 3.0f;

	/** The wire's colour, sRGB hex: a pale steel, apart from the telegraph's red and the guide's footprints. */
	static const TCHAR* const ColourHex;

	/** A wire stretched from `From` to `To`, owned by `Owner`, or null without a world. */
	static ACataclysmDeviceWire* Stretch(UWorld* World, AActor* Owner, const FVector& From, const FVector& To);

	/** Re-stretch this wire from `From` to `To`: its place, its turn and its length. */
	void Span(const FVector& From, const FVector& To);

	/** How long the wire is drawn, in centimetres, along the floor and up. */
	float GetLengthCm() const { return LengthCm; }

	/** The bar that is drawn. For tests. */
	UStaticMeshComponent* GetLine() const { return Line; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Device Wire")
	TObjectPtr<UStaticMeshComponent> Line;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> WireMaterial;

	float LengthCm = 0.0f;
};
