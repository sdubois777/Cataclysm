// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Empire/CataclysmCityUpgrade.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmCityScreenLayout.generated.h"

class UCataclysmEmpireRun;

/**
 * One upgrade the city screen offers, and whether it can be bought.
 *
 * A REASON RATHER THAN JUST A FLAG. Fourteen of the twenty-four upgrades cannot
 * be bought because their effect is not built, and a player looking at a greyed
 * row wants to know which of the several possible reasons applies. The reason is
 * the same text `UCataclysmCityUpgradeRules::ResultText` gives, so the screen and
 * the console command say the same thing about the same refusal.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmCityUpgradeOffer
{
	GENERATED_BODY()

	/** Which row of `game/Data/CityUpgrades.csv` this is. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FName RowName;

	/** Architect, Explorer, Treasurer or Artisan. Empty for the one whose
	 *  branch is undecided. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FString Branch;

	/** What it does, in the words the design workbook uses. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FString Effect;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	ECataclysmCityUpgradeEffect EffectKind = ECataclysmCityUpgradeEffect::None;

	/** Whether the city already has it. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bHeld = false;

	/** Whether buying it now would go through. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	bool bCanBuy = false;

	/** Why it cannot be bought, or empty when it can. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Empire")
	FString Refusal;
};

/**
 * Every word the city screen shows, and none of the drawing.
 *
 * WHY IT IS SPLIT OFF, which is the reason `UCataclysmEmpireMapLayout` and
 * `UCataclysmCharacterSheetLayout` exist: the automation test command passes
 * `-nullrhi` and draws nothing, so anything that reaches the screen cannot be
 * watched by a test. What each line SAYS is covered here; **whether the result
 * is legible is not, and no test on this project can tell anyone that.**
 *
 * IT READS THE DATATABLE AND THE RUN AND WRITES NOTHING. Buying goes through
 * `UCataclysmEmpireRun::BuyCityUpgrade`, which is where every rule lives. This
 * asks the same rules what they would say, so the screen cannot drift from what
 * a purchase actually does -- a screen that decided for itself which upgrades
 * were available would be a second opinion about a question already answered.
 */
UCLASS()
class CATACLYSM_API UCataclysmCityScreenLayout : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The heading: which city, and what tier it is.
	 *
	 * SAFE WITH NO RUN AND NO SUCH CITY. It says so rather than being empty, so
	 * a screen opened before a run started shows why it is empty.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString TitleTextFor(const UCataclysmEmpireRun* Run, int32 CityId);

	/**
	 * What the city is worth right now: defence, population, and whether the
	 * Cataclysm can reach it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString StatusTextFor(const UCataclysmEmpireRun* Run, int32 CityId);

	/** "1 of 3 upgrade slots filled." */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString SlotsTextFor(const UCataclysmEmpireRun* Run, int32 CityId);

	/**
	 * One line per dungeon standing on the city, deepest threat first.
	 *
	 * SOONEST TO RESOLVE FIRST, because the whole reason to look at a city is to
	 * decide whether it is about to be bitten and by what.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static TArray<FString> DungeonLinesFor(const UCataclysmEmpireRun* Run,
										   int32 CityId);

	/** One line per upgrade the city has already bought, in the order bought. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static TArray<FString> HeldLinesFor(const UCataclysmEmpireRun* Run,
										int32 CityId);

	/**
	 * Every upgrade in `game/Data/CityUpgrades.csv`, in table order, with
	 * whether this city could buy it and why not.
	 *
	 * ALL TWENTY-FOUR AND NOT ONLY THE BUYABLE ONES. The screen shows the ones
	 * that cannot be built in a section of their own; hiding them would make the
	 * game look as though it has ten city upgrades rather than twenty-four with
	 * fourteen waiting on other systems.
	 *
	 * EMPTY WHEN THE DATATABLE IS MISSING, which is the same answer
	 * `UCataclysmCityUpgradeMapping::AllRowNames` gives, and the screen says so
	 * rather than showing nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static TArray<FCataclysmCityUpgradeOffer> OffersFor(
		const UCataclysmEmpireRun* Run, int32 CityId);

	/** The heading over the upgrades a city could buy now. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString BuyableHeading();

	/** The heading over the upgrades no city can buy yet, and why they are
	 *  shown at all. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString NotBuiltHeading(int32 Count);

	/** The heading over what the city already has. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString HeldHeading();

	/** The heading over the dungeons standing on the city. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString DungeonHeading(int32 Count);

	/** What a button for one offer says. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString ButtonTextFor(const FCataclysmCityUpgradeOffer& Offer);

	/** What one row in the not-built section says. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Empire")
	static FString NotBuiltLineFor(const FCataclysmCityUpgradeOffer& Offer);
};
