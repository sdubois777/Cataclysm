// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interface/CataclysmCharacterCreationWidget.h"
#include "Save/CataclysmSavePartition.h"
#include "CataclysmNewCharacterWidget.generated.h"

class UButton;
class UCataclysmGameInstance;
class UCheckBox;
class UEditableTextBox;
class UPanelWidget;
class UTextBlock;

/**
 * The new-character screen on the front end: the creator's weapon type and damage type, and the choices locked at
 * creation that the creator lacked -- a name, the lethality mode, the Solo Self-Found flag and offline or online. Issue
 * #753, select-screen step 4a, ruled 2026-11-01; its asset, `WBP_NewCharacter`, is made in step 4b.
 *
 * WHAT THE DESIGN SETTLES, `docs/Cataclysm_GDD_v2.md`, "Difficulty Options": one lethality mode, and separately the Solo
 * Self-Found flag; both "locked at character creation"; offline or online "created as either ... and never changes".
 * `docs/Save_System_Design.md`: "two characters may share a name", so a name is never checked against the others.
 *
 * WHAT IS A JUDGEMENT, ruled 2026-11-01: the name's rules, below. The genre's rules were found only in players' reports,
 * none official: Diablo IV up to 12 letters and not unique; Path of Exile ASCII letters and the underscore, with players
 * asking for the accented letters it lacks; nothing found for Last Epoch.
 *
 * ONLINE IS SHOWN AND CANNOT BE CHOSEN, ruled 2026-11-01. The design seals offline characters off because "a local file
 * can be edited", and every save is a local file until an online service exists, so a character created online now
 * would be exactly that.
 */
UCLASS()
class CATACLYSM_API UCataclysmNewCharacterWidget : public UCataclysmCharacterCreationWidget
{
	GENERATED_BODY()

public:
	/** A name's length, after the spaces at either end are trimmed. A judgement, ruled 2026-11-01. */
	static constexpr int32 LeastNameLength = 2;
	static constexpr int32 MostNameLength = 16;

	/** What the refusal of Online says. */
	static constexpr const TCHAR* OnlineIsNotBuilt = TEXT("Online play needs a service that does not exist yet");

	/**
	 * Whether a typed name is allowed, and the name it becomes: trimmed at both ends; 2 to 16 characters; letters of any
	 * alphabet; single spaces, apostrophes and hyphens only between two letters. False with the rule that failed.
	 */
	static bool NameIsAllowed(const FString& Typed, FString& OutName, FString& OutWhy);

	/** The name typed so far; whether it is allowed is shown, and decided at Confirm. */
	void SetName(const FString& Typed);
	const FString& GetCharacterName() const { return Name; }

	void ChooseLethality(ECataclysmLethality Lethality);
	void SetSoloSelfFound(bool bSoloSelfFound);

	/** Offline, or Online, which is refused while no online service exists. */
	bool ChoosePopulation(ECataclysmPopulation Population);

	const FCataclysmCharacterPartition& GetPartition() const { return Partition; }

	/** Why the last Confirm or choice here was refused, or empty. */
	FString CreationRefusalText() const { return CreationRefusal; }

	/**
	 * Hand the new character to the game instance and open the sandbox, where the level's start makes it
	 * (`ACataclysmGameMode::LoadThePendingCharacter`). False, with the refusal shown, for a name the rules refuse, a
	 * weapon type and damage type that cannot be confirmed, or Online.
	 *
	 * @param Instance  the game instance to hand it to; the one this screen belongs to when null
	 */
	bool ConfirmInto(UCataclysmGameInstance* Instance);

	/** The creator's Confirm, which here is `ConfirmInto` this screen's game instance. */
	virtual bool Confirm() override;

	/** Back to the select screen, nothing handed over. */
	void GoBack();

	/** The level Confirm asked to open. Travel does not run in an automation test, so a test reads this. */
	FString LevelRequested() const { return Requested; }

	virtual void NativeConstruct() override;

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> NameBox;

	/** Standard, Hardcore and Heretic, one `WBP_ChoiceButton` each. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> LethalityBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> SoloSelfFoundBox;

	/** Offline, and Online shown and unavailable. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> PopulationBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CreationRefusalLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> BackButton;

private:
	UFUNCTION()
	void HandleNameChanged(const FText& Text);

	UFUNCTION()
	void HandleSoloSelfFoundChanged(bool bIsChecked);

	UFUNCTION()
	void HandleLethalityClicked(FName Value);

	UFUNCTION()
	void HandlePopulationClicked(FName Value);

	UFUNCTION()
	void HandleBackClicked();

	void FillTheLockedChoices();
	void ShowCreationRefusal(const FString& Why);

	FString Name;
	FCataclysmCharacterPartition Partition;
	FString CreationRefusal;
	FString Requested;
};
