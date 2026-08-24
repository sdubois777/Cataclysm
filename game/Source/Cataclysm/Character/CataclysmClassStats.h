// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "CataclysmClassStats.generated.h"

class UDataTable;

/**
 * A character's eight attribute points, before anything scales them.
 *
 * One point per level, plus whatever the Maw grants. What each is worth lives in
 * game/Data/Attributes.csv, not here.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmAttributePoints
{
	GENERATED_BODY()

	// SaveGame ON ALL EIGHT, because this struct is a field of
	// UCataclysmCharacterSave and the save writer walks only properties carrying
	// that marker. Without it the record serialises an empty object, the fixture
	// and the record disagree, and a character's allocation is silently lost on
	// every save. Issue #50.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Attributes") int32 Agility = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Attributes") int32 Ferocity = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Attributes") int32 Constitution = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Attributes") int32 Vitality = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Attributes") int32 Mind = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Attributes") int32 Spirit = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Attributes") int32 Efficacy = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Cataclysm|Attributes") int32 Luck = 0;

	/** How many points are spent in total. A character has one per level. */
	int32 Total() const;

	/** Points in an attribute by name, matching game/Data/Attributes.csv. */
	int32 PointsIn(const FString& Attribute) const;

	/**
	 * Every attribute name, spelled as game/Data/Attributes.csv spells them.
	 *
	 * ONE LIST RATHER THAN EIGHT PLACES REPEATING IT. The stat-to-attribute map,
	 * the console commands, the save record and the tests all need the same
	 * eight names, and a ninth attribute should mean editing one list.
	 */
	static TArray<FString> Names();

	/**
	 * Add to one attribute by name. Returns false when the name is not one of
	 * the eight, so a caller can tell a mistyped name from a refused spend.
	 */
	bool AddTo(const FString& Attribute, int32 Count);
};

/**
 * The same eight attributes AFTER everything that scales them.
 *
 * WHY THIS IS NOT `FCataclysmAttributePoints`. A point is a whole number a
 * character spent. An attribute is what that count is worth once gear has
 * increased it, and `docs/Cataclysm_GDD_v2.md` is explicit that the two are
 * different things: "Gear does not grant attribute points. It increases the
 * attribute the character already has." Eight `int32` cannot carry 60 Vitality
 * increased by 20%, which is 72.
 *
 * SO THE POINTS ARE A BASE AND THIS IS THE RESULT. The eight run through the
 * ordinary three-bucket pipeline like any other stat, and it is this -- not the
 * point count -- that drives the percentages in `game/Data/Attributes.csv`.
 * Issues #50 and #897.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmAttributeValues
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Attributes") float Agility = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Attributes") float Ferocity = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Attributes") float Constitution = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Attributes") float Vitality = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Attributes") float Mind = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Attributes") float Spirit = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Attributes") float Efficacy = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cataclysm|Attributes") float Luck = 0.0f;

	/** One attribute by name, matching game/Data/Attributes.csv. */
	float ValueIn(const FString& Attribute) const;

	/** Set one attribute by name. False when the name is not one of the eight. */
	bool SetIn(const FString& Attribute, float Value);

	/** The spent points, before anything has scaled them. */
	static FCataclysmAttributeValues FromPoints(const FCataclysmAttributePoints& Points);
};

/**
 * Where a character's stats start, before gear and before attributes.
 *
 * Reads game/Data/ClassStats.csv and game/Data/Attributes.csv, both generated
 * from the design workbook. Every function takes the tables as arguments rather
 * than finding them, so a test can build its own.
 *
 * WHAT THIS IS FOR. The stat pipeline answers "given a base and some modifiers,
 * what is this stat worth". Until now nothing in the game could say what the
 * base was: there were no class stat lines and no attribute effects anywhere in
 * the project, so the reference build test had to quote both from the Python
 * model as literals.
 *
 * ATTRIBUTES ONLY EVER SCALE. A point becomes an entry in the increased bucket,
 * never a flat addition, so it multiplies a base that something else supplied.
 * A stat with no base gains nothing from its attribute, which is the design
 * working rather than failing.
 */
UCLASS()
class CATACLYSM_API UCataclysmClassStats : public UObject
{
	GENERATED_BODY()

public:
	/** The row name carrying the stat line every class inherits. */
	static const FString DefaultClassName;

	/** A character has one attribute point per level, plus the Maw's. */
	static constexpr int32 MaxLevel = 100;

	/**
	 * A class's starting value for a stat at a level.
	 *
	 * Resolved as base plus per-level gain times the levels above the first, so
	 * a level 1 character has exactly the base. Falls back to the shared default
	 * line when the class does not override the stat, and to zero when neither
	 * names it -- which is legitimate, because most classes leave most of the
	 * 33 stats alone.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Class")
	static float BaseFor(const UDataTable* ClassTable, const FString& ClassName,
						 const FString& Stat, int32 Level);

	/** Whether the class overrides this stat rather than inheriting it. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Class")
	static bool Overrides(const UDataTable* ClassTable, const FString& ClassName,
						  const FString& Stat);

	/**
	 * What a character's attribute points contribute to a stat, as one modifier
	 * in the increased bucket.
	 *
	 * Returns false when no attribute touches the stat, so a caller can tell
	 * "nothing applies" from "zero points spent".
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Class")
	static bool AttributeModifierFor(const UDataTable* AttributeTable,
									 const FCataclysmAttributePoints& Points,
									 const FString& Stat,
									 FCataclysmStatModifier& OutModifier);

	/**
	 * The same thing from RESOLVED attribute values rather than spent points.
	 *
	 * This is what the game uses. `AttributeModifierFor` above takes the raw
	 * point counts and is the honest signature for "before anything scales
	 * them", which is what the reference build test wants; it forwards to this.
	 * The two are separate names rather than an overload because Unreal's header
	 * tool cannot generate reflection for two functions sharing one name.
	 *
	 * Returns false when no attribute touches the stat, so a caller can tell
	 * "nothing applies" from "nothing spent".
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Class")
	static bool AttributeModifierForValues(const UDataTable* AttributeTable,
										   const FCataclysmAttributeValues& Values,
										   const FString& Stat,
										   FCataclysmStatModifier& OutModifier);

	/** Every stat any class or the default line names. */
	static TArray<FString> StatsNamedByClasses(const UDataTable* ClassTable);

	/** Every class the table names, not counting the shared default line. */
	static TArray<FString> ClassNames(const UDataTable* ClassTable);
};
