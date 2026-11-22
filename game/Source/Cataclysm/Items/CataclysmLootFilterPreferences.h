// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/CataclysmLootFilter.h"
#include "CataclysmLootFilterPreferences.generated.h"

/**
 * The player's own loot filters and which filter is active: what the preferences file holds. Issue #2846, slice LF2.
 *
 * THE OWNER'S RULING O4 OF 2026-11-22: "Own preferences file". One file on this machine, OUTSIDE THE SAVE RECORDS, so
 * no save version moves and every character on the machine shares the filters: a filter changes only which names are
 * drawn, so sharing it across lethality modes gives no character an advantage. The file is
 * `UCataclysmLootFilterPreferences::FilePath()`.
 *
 * ONLY THE PLAYER'S OWN FILTERS ARE STORED. The three built-in filters are never written to the file and are made from
 * code every time they are asked for (`UCataclysmLootFilter::BuiltInNamed`), which is the first of the three ways
 * their names are protected; `UCataclysmLootFilterPreferences` says the other two.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmLootFilterPreferences
{
	GENERATED_BODY()

	/** The file's own version, so a later change to its shape can read an older file. */
	UPROPERTY()
	int32 Version = 1;

	/** The name of the active filter, a built-in's or one of `Filters`. */
	UPROPERTY()
	FString Active;

	/** The player's own filters, at most `UCataclysmLootFilterPreferences::MaxSavedFilters`. */
	UPROPERTY()
	TArray<FCataclysmLootFilter> Filters;
};

/**
 * Every change the loot filter screen can make to the preferences, each answering whether it was made and, when not,
 * the sentence the screen shows. Static functions over the struct, so every rule is tested without a screen or a file.
 *
 * HOW THE THREE BUILT-IN NAMES ARE PROTECTED, all three needed:
 *   1. They are not stored. The file holds only the player's filters, and a built-in is made from code whenever it is
 *      asked for, so nothing written to the file can change one.
 *   2. Every change that names a filter refuses a built-in: no rule can be added to, removed from, moved in or edited
 *      in one, and one cannot be renamed or deleted (`BuiltInRefusal`). To change one, the player copies it.
 *   3. No player filter can take a built-in's name, in any case and with any spaces around it: a rename to one is
 *      refused (`NameTakenRefusal`), and a file edited by hand to hold one has that filter renamed when it is read
 *      (`Sanitised`).
 */
UCLASS()
class CATACLYSM_API UCataclysmLootFilterPreferences : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The most filters a player may keep. N3, ruled 2026-11-22, a judgement. */
	static constexpr int32 MaxSavedFilters = 10;

	/** The longest name a filter may have, in characters. A judgement of this slice: it fits the screen's list. */
	static constexpr int32 MaxNameLength = 40;

	/** The refusals the screen shows. */
	static const TCHAR* BuiltInRefusal;
	static const TCHAR* NameTakenRefusal;
	static const TCHAR* NameEmptyRefusal;
	static const TCHAR* NameTooLongRefusal;
	static const TCHAR* TooManyFiltersRefusal;
	static const TCHAR* TooManyRulesRefusal;
	static const TCHAR* NoSuchFilterRefusal;
	static const TCHAR* NoSuchRuleRefusal;

	/** The three built-in names, in the order the screen lists them. */
	static TArray<FString> BuiltInNames();

	/** Whether `Name`, trimmed and ignoring case, is a built-in filter's name. */
	static bool IsBuiltInName(const FString& Name);

	/** Every filter's name, the built-ins first, then the player's in the order kept. */
	static TArray<FString> AllNames(const FCataclysmLootFilterPreferences& Preferences);

	/** The filter of this name, a built-in's or the player's; false when there is none. Built-ins are looked up first. */
	static bool Find(const FCataclysmLootFilterPreferences& Preferences, const FString& Name,
					 FCataclysmLootFilter& OutFilter);

	/** The active filter, or the owner's default ("Hide worse than worn") when the active name names none. */
	static FCataclysmLootFilter ActiveFilter(const FCataclysmLootFilterPreferences& Preferences);

	/** Makes the named filter active. Refused for a name that names no filter. */
	static bool SetActive(FCataclysmLootFilterPreferences& Preferences, const FString& Name, FString& OutRefusal);

	/** A new empty filter, named "My filter N" with the smallest N not taken. Refused at `MaxSavedFilters`. */
	static bool NewFilter(FCataclysmLootFilterPreferences& Preferences, FString& OutName, FString& OutRefusal);

	/** A copy of any filter, a built-in included, named as `NewFilter` names one. Refused at `MaxSavedFilters`. */
	static bool CopyFilter(FCataclysmLootFilterPreferences& Preferences, const FString& From, FString& OutName,
						   FString& OutRefusal);

	/**
	 * Renames one of the player's filters, keeping it active when it was. `To` is trimmed. Refused for a built-in, a
	 * name taken by any filter (a built-in's in any case), an empty name and one longer than `MaxNameLength`.
	 */
	static bool RenameFilter(FCataclysmLootFilterPreferences& Preferences, const FString& From, const FString& To,
							 FString& OutRefusal);

	/** Deletes one of the player's filters. When it was active, the owner's default becomes active. */
	static bool DeleteFilter(FCataclysmLootFilterPreferences& Preferences, const FString& Name, FString& OutRefusal);

	/** Adds a rule at the end of one of the player's filters: a Show rule with no condition. Refused at 75 rules. */
	static bool AddRule(FCataclysmLootFilterPreferences& Preferences, const FString& Name, FString& OutRefusal);

	/** Removes rule `Index` of one of the player's filters. */
	static bool RemoveRule(FCataclysmLootFilterPreferences& Preferences, const FString& Name, int32 Index,
						   FString& OutRefusal);

	/** Moves rule `Index` one place up (`Delta` -1) or down (+1). Refused past either end. */
	static bool MoveRule(FCataclysmLootFilterPreferences& Preferences, const FString& Name, int32 Index, int32 Delta,
						 FString& OutRefusal);

	/** Replaces rule `Index` of one of the player's filters. */
	static bool SetRule(FCataclysmLootFilterPreferences& Preferences, const FString& Name, int32 Index,
						const FCataclysmLootFilterRule& Rule, FString& OutRefusal);

	/**
	 * The preferences as they may be kept: at most `MaxSavedFilters` filters, each of at most 75 rules
	 * (`UCataclysmLootFilter::MaxRules`), and every name unique, not empty, not longer than `MaxNameLength` and no
	 * built-in's; a name that fails is replaced as `NewFilter` names one. An active name that names no filter becomes
	 * the default's. Run on every file read, so a file edited by hand cannot get round the rules above.
	 */
	static FCataclysmLootFilterPreferences Sanitised(const FCataclysmLootFilterPreferences& Preferences);

	/** The preferences as the file's text, JSON. */
	static FString ToJson(const FCataclysmLootFilterPreferences& Preferences);

	/**
	 * The preferences read from the file's text, sanitised. False, leaving `Out` as a new player's (the default active,
	 * no filters of their own), when the text is not the file's JSON.
	 */
	static bool FromJson(const FString& Json, FCataclysmLootFilterPreferences& Out);

	/** Where the file is: `Saved/Preferences/LootFilters.json` under the project. Not a save record's slot. */
	static FString FilePath();

	/** Reads the file at `Path`. False, leaving `Out` as a new player's, when there is no file or it does not read. */
	static bool Load(const FString& Path, FCataclysmLootFilterPreferences& Out);

	/** Writes the preferences to the file at `Path`. */
	static bool Save(const FString& Path, const FCataclysmLootFilterPreferences& Preferences);

	/** A new player's preferences: the owner's default active, no filters of their own. */
	static FCataclysmLootFilterPreferences NewPlayer();
};
