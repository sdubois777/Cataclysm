// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "UObject/Object.h"
#include "CataclysmFloorModifierPanelLayout.generated.h"

class UDataTable;

/**
 * One dungeon modifier as the floor panel shows it.
 *
 * DATA AND NOT A WIDGET, for the reason `UCataclysmCityScreenLayout` gives: the
 * automation test command passes `-nullrhi`, and a widget built in a headless
 * test has no children to read, so what the panel says is decided here where a
 * test can see it.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmFloorModifierLine
{
	GENERATED_BODY()

	/** The row key, e.g. `Famine_Starvation`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FName RowKey;

	/**
	 * What the design calls it, e.g. "Starvation".
	 *
	 * THE KEY ITSELF FOR A KEY THAT IS NOT A ROW, so a mistyped or stale key is
	 * still shown as what it is rather than as a blank line.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FString Name;

	/** The row's own description, word for word. Empty for a key that is not a row. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FString Description;

	/** How much of what the row says happens. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	ECataclysmModifierBuilt Built = ECataclysmModifierBuilt::NotBuilt;

	/** Whether the key names a row of the dungeon modifier table. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	bool bIsARow = false;
};

/**
 * What the floor panel says about the modifiers in force on a floor.
 *
 * WHY THE PANEL EXISTS. Before issue #41's first modifier slice, the only place a
 * dungeon's modifiers appeared anywhere in the game was a count and a danger
 * total printed by `Cataclysm.ShowEmpire`. No screen named them, so a player
 * could not tell which modifiers a floor carried, and 116 of the 117 did nothing
 * a player could see.
 *
 * IT SAYS WHICH ONES DO NOTHING. The project owner's answer to question 2 of the
 * modifier plan, on 2026-09-11 and relayed by the coordinating session, was that
 * every row stays in the draw and the unbuilt ones are marked on this panel.
 * `NameLineFor` is where the mark is.
 */
UCLASS()
class CATACLYSM_API UCataclysmFloorModifierPanelLayout : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * One line per modifier, in the order the floor carries them.
	 *
	 * @param RowKeys              `FCataclysmFloorBrief::Modifiers`
	 * @param DungeonModifierTable where names and descriptions come from. Null
	 *                             gives lines holding only the keys
	 */
	static TArray<FCataclysmFloorModifierLine> LinesFor(
		const TArray<FName>& RowKeys, const UDataTable* DungeonModifierTable);

	/** The panel's heading, e.g. "Floor 5: 2 dungeon modifiers". */
	static FString HeadingFor(int32 FloorNumber, int32 ModifierCount);

	/**
	 * A modifier's name with what is built of it, e.g. "Edict of Silence (not
	 * built yet)".
	 *
	 * NOTHING AFTER A BUILT ONE'S NAME, so the mark stands out on the ones that
	 * need it rather than being on every line.
	 */
	static FString NameLineFor(const FCataclysmFloorModifierLine& Line);
};
