// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCharacterCreationWidget.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Cataclysm.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"

const UDataTable* UCataclysmCharacterCreationWidget::WeaponSkills() const
{
	if (!WeaponSkillTable)
	{
		// MUTABLE THROUGH A CONST FUNCTION, deliberately. Loading the generated
		// table is caching rather than a change of state, and every reader of
		// this screen -- what it offers, what it says, whether it can be
		// confirmed -- is a question rather than an instruction.
		const_cast<UCataclysmCharacterCreationWidget*>(this)->WeaponSkillTable =
			UCataclysmWeaponSkills::LoadGeneratedTable();
	}

	return WeaponSkillTable;
}

const UDataTable* UCataclysmCharacterCreationWidget::ItemBases() const
{
	if (!BaseTable)
	{
		const_cast<UCataclysmCharacterCreationWidget*>(this)->BaseTable =
			UCataclysmItemModifiers::LoadBaseTable();
	}

	return BaseTable;
}

void UCataclysmCharacterCreationWidget::SetTables(
	const UDataTable* InWeaponSkillTable, const UDataTable* InBaseTable)
{
	WeaponSkillTable = InWeaponSkillTable;
	BaseTable = InBaseTable;
}

TArray<FName> UCataclysmCharacterCreationWidget::OfferedWeaponTypes() const
{
	return UCataclysmCharacterCreation::StartingWeaponTypes(ItemBases());
}

TArray<FName> UCataclysmCharacterCreationWidget::OfferedDamageTypes() const
{
	return UCataclysmItemModifiers::DamageTypeNames();
}

bool UCataclysmCharacterCreationWidget::DamageTypeIsAvailable(
	FName DamageType) const
{
	if (Choice.WeaponType.IsNone())
	{
		return false;
	}

	return UCataclysmCharacterCreation::DamageTypesFor(WeaponSkills(),
													   Choice.WeaponType)
		.Contains(DamageType);
}

bool UCataclysmCharacterCreationWidget::ChooseWeaponType(FName WeaponType)
{
	if (!OfferedWeaponTypes().Contains(WeaponType))
	{
		return false;
	}

	Choice.WeaponType = WeaponType;

	// THE DAMAGE TYPE IS DROPPED WHEN THE NEW WEAPON CANNOT CARRY IT. See the
	// header: keeping it would leave the screen saying three things about one
	// fact, two of which disagree with the third.
	if (!Choice.DamageType.IsNone() && !DamageTypeIsAvailable(Choice.DamageType))
	{
		Choice.DamageType = NAME_None;
	}

	RefreshDisplay();
	return true;
}

bool UCataclysmCharacterCreationWidget::ChooseDamageType(FName DamageType)
{
	if (!DamageTypeIsAvailable(DamageType))
	{
		return false;
	}

	Choice.DamageType = DamageType;
	RefreshDisplay();
	return true;
}

FText UCataclysmCharacterCreationWidget::SummaryText() const
{
	return FText::FromString(
		UCataclysmCharacterCreation::SummaryFor(WeaponSkills(), Choice));
}

FText UCataclysmCharacterCreationWidget::UnlockedClassesText() const
{
	return FText::FromString(
		UCataclysmCharacterCreation::UnlockedClassesFor(Choice));
}

FText UCataclysmCharacterCreationWidget::RefusalText() const
{
	return FText::FromString(UCataclysmCharacterCreation::RefusalFor(
		WeaponSkills(), ItemBases(), Choice));
}

bool UCataclysmCharacterCreationWidget::CanConfirm() const
{
	return UCataclysmCharacterCreation::IsLegalChoice(WeaponSkills(),
													  ItemBases(), Choice);
}

bool UCataclysmCharacterCreationWidget::Confirm()
{
	if (!CanConfirm())
	{
		return false;
	}

	APlayerController* Controller = GetOwningPlayer();
	ACataclysmPlayerState* State =
		Controller ? Controller->GetPlayerState<ACataclysmPlayerState>() : nullptr;
	if (!State)
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("The character creator has nowhere to record the choice, "
					"because the controller has no Cataclysm player state."));
		return false;
	}

	FString Reason;
	if (!State->ChooseAtCreation(WeaponSkills(), ItemBases(), Choice.WeaponType,
								 Choice.DamageType, Reason))
	{
		// A SECOND REFUSAL AFTER `CanConfirm` SAID YES SHOULD BE IMPOSSIBLE, so
		// it is logged rather than shown. Both ask
		// `UCataclysmCharacterCreation::RefusalFor` with the same two tables.
		UE_LOG(LogCataclysm, Error,
			   TEXT("The player state refused a choice the screen had already "
					"accepted: %s"), *Reason);
		return false;
	}

	// AND THE CHARACTER IS MADE TO MATCH, rather than waiting for the next
	// respawn. The point of the screen is that the owner can see what the choice
	// did, and a screen that changes a number on the player state and nothing
	// visible is the thing the standing rule about user interfaces exists to
	// prevent.
	if (ACataclysmPlayerCharacter* Character =
			Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()))
	{
		Character->ApplyCreationChoice();
	}

	RemoveFromParent();

	// AND THE INPUT MODE GOES BACK, because the controller changed it to open
	// this screen. Closing with the key restores it there; closing by
	// confirming has to restore it here, or the game is left in GameAndUI with
	// nothing on screen to click. Both routes have to leave the same state.
	Controller->SetInputMode(FInputModeGameOnly());
	return true;
}

void UCataclysmCharacterCreationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ConfirmButton)
	{
		// CLEARED FIRST, for the reason `UCataclysmChoiceButton` gives: this
		// runs again every time the screen is added to the viewport.
		ConfirmButton->OnClicked.RemoveAll(this);
		ConfirmButton->OnClicked.AddDynamic(
			this, &UCataclysmCharacterCreationWidget::HandleConfirmClicked);
	}

	if (TitleLabel)
	{
		TitleLabel->SetText(Title);
	}

	RefreshDisplay();
}

void UCataclysmCharacterCreationWidget::RefreshDisplay()
{
	FillChoicePanel(WeaponTypeBox, OfferedWeaponTypes(), /*bWeaponTypes=*/true);
	FillChoicePanel(DamageTypeBox, OfferedDamageTypes(), /*bWeaponTypes=*/false);

	if (SummaryLabel)
	{
		SummaryLabel->SetText(SummaryText());
	}

	if (UnlockedLabel)
	{
		UnlockedLabel->SetText(UnlockedClassesText());
	}

	if (RefusalLabel)
	{
		RefusalLabel->SetText(RefusalText());
	}

	if (ConfirmButton)
	{
		ConfirmButton->SetIsEnabled(CanConfirm());
	}
}

void UCataclysmCharacterCreationWidget::FillChoicePanel(
	UPanelWidget* Panel, const TArray<FName>& Values, bool bWeaponTypes)
{
	if (!Panel)
	{
		return;
	}

	UClass* ButtonClass = LoadedChoiceButtonClass();
	if (!ButtonClass)
	{
		return;
	}

	// THE BUTTONS ARE MADE ONCE AND UPDATED AFTERWARDS, RATHER THAN REBUILT ON
	// EVERY CLICK, and that is a correctness point rather than a saving. This
	// runs from inside a button's own click handler -- choosing a weapon type
	// refreshes the screen -- so emptying the panel here would tear down the
	// Slate widget whose event is still being dispatched. Neither list ever
	// changes length or order: fourteen weapon types and eight damage types,
	// read once from tables that do not move while the game runs.
	bool bNeedsBuilding = Panel->GetChildrenCount() != Values.Num();
	if (!bNeedsBuilding)
	{
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			const UCataclysmChoiceButton* Existing =
				Cast<UCataclysmChoiceButton>(Panel->GetChildAt(Index));
			if (!Existing || Existing->GetChoiceValue() != Values[Index])
			{
				bNeedsBuilding = true;
				break;
			}
		}
	}

	if (bNeedsBuilding)
	{
		Panel->ClearChildren();

		for (const FName& Value : Values)
		{
			UCataclysmChoiceButton* Button =
				CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
			if (!Button)
			{
				continue;
			}

			// SET BEFORE IT IS ADDED, so the button knows what it stands for
			// even if the loop below never runs, which is what happens when
			// CreateWidget fails for one of them.
			Button->SetChoice(Value, FText::FromName(Value), false, true);

			// TWO CALLS RATHER THAN ONE WITH A CHOSEN FUNCTION. `AddDynamic` is
			// a macro that stringifies the function's name to find it by
			// reflection, so the name has to be written out literally and
			// cannot be selected at run time.
			if (bWeaponTypes)
			{
				Button->OnChoiceClicked.AddDynamic(
					this,
					&UCataclysmCharacterCreationWidget::HandleWeaponTypeClicked);
			}
			else
			{
				Button->OnChoiceClicked.AddDynamic(
					this,
					&UCataclysmCharacterCreationWidget::HandleDamageTypeClicked);
			}

			Panel->AddChild(Button);
		}
	}

	for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
	{
		UCataclysmChoiceButton* Button =
			Cast<UCataclysmChoiceButton>(Panel->GetChildAt(Index));
		if (!Button)
		{
			continue;
		}

		const FName Value = Button->GetChoiceValue();
		const bool bChosen = bWeaponTypes ? Choice.WeaponType == Value
										  : Choice.DamageType == Value;

		// A WEAPON TYPE IS ALWAYS AVAILABLE AND A DAMAGE TYPE IS NOT. The list
		// of weapon types is already only the ones a character may start with,
		// so nothing in it is ever refused; a damage type depends on the weapon
		// chosen, which is the relationship this screen exists to show.
		const bool bAvailable = bWeaponTypes || DamageTypeIsAvailable(Value);

		Button->SetChoice(Value, FText::FromName(Value), bChosen, bAvailable);
	}
}

UClass* UCataclysmCharacterCreationWidget::LoadedChoiceButtonClass()
{
	if (ResolvedChoiceButtonClass)
	{
		return ResolvedChoiceButtonClass;
	}

	ResolvedChoiceButtonClass = ChoiceButtonClass.LoadSynchronous();
	if (!ResolvedChoiceButtonClass && !bComplainedAboutTheChoiceButtonClass)
	{
		// ONCE, NOT ON EVERY CLICK. This is reached from the refresh, which
		// runs whenever anything is chosen, and a log line per click would bury
		// whatever else the log was going to say.
		bComplainedAboutTheChoiceButtonClass = true;
		UE_LOG(LogCataclysm, Error,
			   TEXT("The character creator draws no options, because %s could "
					"not be loaded. Run  python tools/run_editor_python.py "
					"tools/generate_interface_assets.py  to build it."),
			   *ChoiceButtonClass.ToString());
	}

	return ResolvedChoiceButtonClass;
}

void UCataclysmCharacterCreationWidget::HandleConfirmClicked()
{
	Confirm();
}

void UCataclysmCharacterCreationWidget::HandleWeaponTypeClicked(FName WeaponType)
{
	ChooseWeaponType(WeaponType);
}

void UCataclysmCharacterCreationWidget::HandleDamageTypeClicked(FName DamageType)
{
	ChooseDamageType(DamageType);
}
