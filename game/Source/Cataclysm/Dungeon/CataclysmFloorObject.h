// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmFloorObject.generated.h"

/**
 * One thing a floor object offers when it is clicked: a key the rule that placed it answers, the words on its button,
 * and whether it can be chosen now.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmFloorObjectChoice
{
	GENERATED_BODY()

	/** What the rule that placed the object is told was chosen. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FName Key;

	/** The words on the choice's button. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FString Label;

	/** Whether it can be chosen now; a choice that cannot is shown and refused. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	bool bAvailable = true;
};

/**
 * A thing on a dungeon floor that a rule placed and the player can click to be offered a choice: a totem, an altar,
 * a chest. Issues #1820 and #41, the interaction and choice screen.
 *
 * CLICKED THE WAY A DROP IS, through the name tag the HUD draws over it, so no new key is needed: a click in reach
 * opens its choice panel, and a click from further off walks the player there first. The design document says so
 * beside the section on taking drops.
 *
 * NOT A CREATURE. It has no health, is never targeted and is never a floor's creature. It holds only what the panel
 * shows; what a choice does is the dungeon game mode's, told through
 * `ACataclysmDungeonGameMode::ChooseAtFloorObject` which rule placed the object (`RuleKey`).
 *
 * NOT SAVED. A floor restored from a save re-runs its rules, which place their objects again.
 */
UCLASS()
class CATACLYSM_API ACataclysmFloorObject : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmFloorObject();

	/** The row key of the dungeon rule that placed it, which is the rule a choice is sent to. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FName RuleKey;

	/** The name on its tag and at the top of its panel. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FString DisplayName;

	/** The line under the name on its panel, saying what the choice is about. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FString Prompt;

	/** What it offers, in the order the panel shows them. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	TArray<FCataclysmFloorObjectChoice> Choices;

	/** The colour of its name tag. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FLinearColor NameColour = FLinearColor(0.85f, 0.75f, 1.0f, 1.0f);

	/** How far above the object its name is drawn, as a drop's is. */
	static constexpr float NameHeightCm = 120.0f;

	/** The choice of this key, or null. */
	const FCataclysmFloorObjectChoice* ChoiceOf(FName ChoiceKey) const;

	/**
	 * Every floor object within `Range` of `Standing`, nearest first: the objects whose names the HUD draws. Asked
	 * every frame rather than kept, for the reason `UCataclysmDropPickup::DropsToName` gives.
	 */
	static void ObjectsToName(const UWorld* World, const FVector& Standing, float Range,
							  TArray<ACataclysmFloorObject*>& Out);
};
