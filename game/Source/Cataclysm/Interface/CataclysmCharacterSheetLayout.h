// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "Character/CataclysmClassStats.h"
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmCharacterSheetLayout.generated.h"

class UAbilitySystemComponent;

/**
 * The five groups the character sheet is divided into.
 *
 * NOT INVENTED HERE. `STAT_GROUPS` in `sim/cataclysm_sim/character.py` already
 * divides the sheet's stats into exactly these five, in this order, and its own
 * comment says they follow the way `game/Config/Tags/CataclysmTags.ini` groups
 * its `Stat.*` tags. A screen that grouped them differently would be a third
 * opinion about a question already answered twice.
 *
 * SPELLED `Defence` AND `Offence` HERE AND `Defense` AND `Offense` THERE. The
 * model is written in American spelling throughout and this project's prose is
 * not. `HeadingFor` below is what a player reads and it uses the project's
 * spelling; `ModelGroupName` gives the model's, so the test that compares the
 * two lists has something to compare.
 */
UENUM(BlueprintType)
enum class ECataclysmSheetGroup : uint8
{
	/** What the character has a pool of: health, mana, energy shield, resource. */
	Resource	= 0		UMETA(DisplayName = "Resource"),

	/** What comes back on its own, and what is taken from a hit dealt. */
	Recovery	= 1		UMETA(DisplayName = "Recovery"),

	/** What happens to a hit arriving, including the eight resistances. */
	Defence		= 2		UMETA(DisplayName = "Defence"),

	/** What the character's own hits do. */
	Offence		= 3		UMETA(DisplayName = "Offence"),

	/** Movement, cooldowns and what drops. */
	Utility		= 4		UMETA(DisplayName = "Utility"),
};

/**
 * One row of the character sheet: a name, a number, and an optional remark.
 *
 * THREE FIELDS AND NOT ONE STRING, so the Widget Blueprint can put the number
 * in its own column and draw the remark dimmer, and so a test can check the
 * number without matching the words around it.
 *
 * THE REMARK IS WHERE A STAT SAYS WHAT IT MEANS. "20%" beside "Block chance"
 * does not tell a player that a block removes half a hit rather than all of it,
 * and the rule that armour never removes more than 75% cannot be read off the
 * armour figure at all.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmStatLine
{
	GENERATED_BODY()

	/** What a player calls it: "Armour", "War resistance". */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Character")
	FString Name;

	/** The figure, already carrying its unit: "1240", "20.0%", "4.0 m/s". */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Character")
	FString Value;

	/** What the figure means, or empty when the figure speaks for itself. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Character")
	FString Note;

	FCataclysmStatLine() = default;

	FCataclysmStatLine(FString InName, FString InValue, FString InNote = FString())
		: Name(MoveTemp(InName))
		, Value(MoveTemp(InValue))
		, Note(MoveTemp(InNote))
	{
	}

	/** The three run together, for a log line or a test failure message. */
	FString AsLine() const;
};

/**
 * What the character sheet says, worked out without drawing anything.
 *
 * WHY IT IS A SEPARATE CLASS FROM THE WIDGET, which is the reason
 * `UCataclysmEmpireMapLayout` and `UCataclysmPassiveTreeLayout` are separate
 * from their widgets: the automation test command in `tools/unreal_build.py`
 * passes `-nullrhi`, so nothing that reaches the screen can be watched by a
 * test. Every word below is decided from an ability system component and a
 * difficulty tier, so all of it is covered while the drawing stays uncovered.
 *
 * THE LIST OF STATS IS THE MODEL'S LIST, NOT A NEW ONE. There are 46 stats on
 * this project's character sheet and which 46 has been settled for a long time:
 * `sim/cataclysm_sim/character.py` holds them in `STAT_GROUPS`, and the
 * automation test `Cataclysm.Attributes.CharacterSheetIsComplete` counts the
 * attribute sets against that number and names every attribute deliberately
 * left off. Sixteen attributes carry a comment saying in as many words that
 * they are NOT on the character sheet. This class reads the same 46 and adds
 * nothing, so a stat cannot appear on the screen that the model does not model.
 * `tools/tests/test_the_character_sheet_shows_the_model_stats.py` fails if the
 * two lists ever disagree.
 *
 * WHAT IT DOES NOT DO. It does not decide whether the result is legible, it
 * does not lay anything out in pixels -- unlike the empire map's layout class,
 * because a sheet is a list and a list needs no arithmetic to place -- and it
 * cannot tell anybody whether the sheet is useful to play with. Somebody has to
 * look. Issue #1233.
 */
UCLASS()
class CATACLYSM_API UCataclysmCharacterSheetLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** How many stats the sheet holds. Forty-six. */
	static int32 SheetStatCount();

	/** The five groups, in the order they are shown. */
	static const TArray<ECataclysmSheetGroup>& Groups();

	/** The heading a player reads above a group. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Character")
	static FString HeadingFor(ECataclysmSheetGroup Group);

	/**
	 * The name `sim/cataclysm_sim/character.py` gives this group.
	 *
	 * FOR THE TEST THAT COMPARES THE TWO LISTS AND FOR NOTHING ELSE. No player
	 * ever reads it.
	 */
	static FString ModelGroupName(ECataclysmSheetGroup Group);

	/** The stats in one group, named as the model and the class tables name
	 *  them: `max_health`, `armor`, `resistance_war`. */
	static const TArray<FString>& StatsIn(ECataclysmSheetGroup Group);

	/** All 46, in group order. */
	static const TArray<FString>& SheetStats();

	/** What a player calls one stat. "armor" becomes "Armour". */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Character")
	static FString NameFor(const FString& Stat);

	/**
	 * One row of the sheet.
	 *
	 * SAFE WITH NO ABILITY SYSTEM COMPONENT, which is what a screen opened
	 * before a character exists has. Every figure reads zero and the row is
	 * still named, so the sheet has the shape it will have rather than being
	 * empty.
	 *
	 * @param DifficultyTier  what the armour figure and the resistance penalty
	 *                        are worked out against. Both change with it and
	 *                        neither can be stated without one.
	 */
	static FCataclysmStatLine LineFor(const FString& Stat,
									  const UAbilitySystemComponent* ASC,
									  int32 DifficultyTier);

	/** Every row in one group. */
	static TArray<FCataclysmStatLine> LinesIn(ECataclysmSheetGroup Group,
											  const UAbilitySystemComponent* ASC,
											  int32 DifficultyTier);

	// ----------------------------------------------------------------------
	// The eight attributes, which are not sheet stats
	// ----------------------------------------------------------------------

	/**
	 * One row per attribute, showing the points spent in it.
	 *
	 * SEPARATE FROM THE 46 BECAUSE AN ATTRIBUTE IS NOT ONE OF THEM. The sheet
	 * stats are what the character's numbers ARE; the eight attributes are what
	 * a player SPENDS to move them, and `Cataclysm.Attributes.CharacterSheetIsComplete`
	 * counts the primary attribute set separately for the same reason. Issue
	 * #50 is the allocation; issue #1233 is the sheet.
	 */
	static TArray<FCataclysmStatLine> AttributeLines(
		const FCataclysmAttributePoints& Points);

	/** "3 points to spend", "No points to spend". */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Character")
	static FString UnspentPointsText(int32 Unspent);

	// ----------------------------------------------------------------------
	// Figures
	// ----------------------------------------------------------------------

	/**
	 * A plain figure, with a decimal place only when it needs one.
	 *
	 * SO 606 HEALTH IS NOT "606.0" AND 4.2 SECONDS IS NOT "4". A sheet holding
	 * both large whole pools and small fractions cannot use one number of
	 * decimal places for all of them.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Character")
	static FString Number(float Value);

	/** The same, with a percent sign. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Character")
	static FString Percent(float Value);

	/**
	 * Whether this stat is read as a percentage.
	 *
	 * PER STAT AND NOT BY GUESSING FROM THE NAME. Which of the 46 are
	 * percentages is a fact about the design, recorded in the attribute set
	 * headers and in `DEFAULT_STAT_LINE`, and a name-shaped guess gets
	 * `spell_damage` and `armor` wrong in opposite directions.
	 */
	static bool IsPercentage(const FString& Stat);
};
