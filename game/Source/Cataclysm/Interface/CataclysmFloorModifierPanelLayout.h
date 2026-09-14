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

	/**
	 * What this row is counting right now, e.g. "3 of 20", or empty for a row
	 * that counts nothing.
	 *
	 * WHY A ROW NEEDS THIS AT ALL. Two dungeon rules keep a count that decides
	 * what happens to the player -- `Famine_Wasting_Sickness` and
	 * `Demonic_Brand_of_the_Aggressor` -- and until this field neither count was
	 * shown anywhere. Measured 2026-09-14: outside
	 * `game/Source/Cataclysm/Dungeon/`, nothing in `game/Source` read either one.
	 *
	 * THE SECOND OF THOSE IS WHY IT COULD NOT WAIT. Wasting Sickness's stacks
	 * lower maximum health and mana, so a player sees the bar shrink even without
	 * a number. Brand of the Aggressor changes nothing at all for nineteen blows
	 * and then takes a fifth of maximum health, so with no count there is nothing
	 * to read and nothing to react to.
	 *
	 * A STRING RATHER THAN A NUMBER, because what is worth showing differs by
	 * row and the rule that keeps the count is the only thing that knows. The
	 * layout prints what it is handed and decides nothing.
	 *
	 * EMPTY FOR EVERY OTHER ROW, which is most of them, and `NameLineFor` adds
	 * nothing when it is empty -- so a row that counts nothing reads exactly as
	 * it did before this field existed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Dungeon")
	FString LiveCount;
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
	 * @param LiveCounts           what each row is counting now, by row key.
	 *                             Defaulted empty, so every caller that has no
	 *                             counts to give is unchanged and every line
	 *                             reads as it did before this parameter existed
	 */
	static TArray<FCataclysmFloorModifierLine> LinesFor(
		const TArray<FName>& RowKeys, const UDataTable* DungeonModifierTable,
		const TMap<FName, FString>& LiveCounts = TMap<FName, FString>());

	/** The panel's heading, e.g. "Floor 5: 2 dungeon modifiers". */
	static FString HeadingFor(int32 FloorNumber, int32 ModifierCount);

	/**
	 * A modifier's name with what is built of it, e.g. "Edict of Silence (not
	 * built yet)".
	 *
	 * NOTHING AFTER A BUILT ONE'S NAME, so the mark stands out on the ones that
	 * need it rather than being on every line.
	 *
	 * EXCEPT A LIVE COUNT, WHICH IS APPENDED WHEN THE LINE CARRIES ONE, e.g.
	 * "Brand of the Aggressor (3 of 20)". A row that counts nothing is unchanged,
	 * which is what keeps every existing assertion about this text true.
	 */
	static FString NameLineFor(const FCataclysmFloorModifierLine& Line);
};
