// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Player/CataclysmGameMode.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Misc/PackageName.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmCharacterSelectWidget.h"
#include "Interface/CataclysmNewCharacterWidget.h"
#include "Player/CataclysmFrontEndGameMode.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmGameInstance.h"
#include "Misc/ScopeExit.h"
#include "Save/CataclysmSaveLoad.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/UnrealType.h"

namespace CataclysmCharacterSelectTest
{
	/** A readable row. */
	FCataclysmCharacterSummary ARow(const TCHAR* Name, int32 Level)
	{
		FCataclysmCharacterSummary Row;
		Row.SlotName = FString::Printf(TEXT("Character_%s"), Name);
		Row.CharacterId = FGuid::NewGuid();
		Row.bReadable = true;
		Row.Name = Name;
		Row.Level = Level;
		return Row;
	}

	/** A row whose run was saved on `Floor` of `Dungeon`, on day `Day`. */
	FCataclysmCharacterSummary InARun(FCataclysmCharacterSummary Row, int32 Day, const TCHAR* Dungeon, int32 Floor)
	{
		Row.bInARun = true;
		Row.bRunReadable = true;
		Row.Day = Day;
		Row.Dungeon = FName(Dungeon);
		Row.Floor = Floor;
		return Row;
	}

	FCataclysmCharacterSummary AnUnreadableRow()
	{
		FCataclysmCharacterSummary Row;
		Row.SlotName = TEXT("Character_Unreadable");
		Row.CharacterId = FGuid::NewGuid();
		Row.Why = TEXT("not a JSON object");
		return Row;
	}
}

/**
 * Each row of the select screen says what ruling 2 lists: the name, the level, the lethality, the Solo Self-Found and
 * population flags, the weapon type, and the run's day, with its floor when it was saved inside a dungeon; an unnamed
 * character says so; an unreadable slot is shown as unreadable, with why. Issue #753, select-screen step 3a. Prints
 * "SELECT ROWS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectRowsTest,
	"Cataclysm.CharacterSelect.EachRowShowsWhatTheRulingLists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectRowsTest::RunTest(const FString&)
{
	using namespace CataclysmCharacterSelectTest;

	FCataclysmCharacterSummary Solo = InARun(ARow(TEXT("Vesper"), 12), 9, TEXT("Crypt"), 3);
	Solo.Partition.Lethality = ECataclysmLethality::Hardcore;
	Solo.Partition.Population = ECataclysmPopulation::Online;
	Solo.Partition.bSoloSelfFound = true;
	Solo.WeaponType = TEXT("Greatsword");
	FCataclysmCharacterSummary Unnamed = ARow(TEXT(""), 1);
	FCataclysmCharacterSummary OnTheSurface = InARun(ARow(TEXT("Corin"), 4), 21, TEXT("Sandbox"), 0);
	const FString SoloText = UCataclysmCharacterSelectWidget::RowText(Solo);
	const FString UnnamedText = UCataclysmCharacterSelectWidget::RowText(Unnamed);
	const FString SurfaceText = UCataclysmCharacterSelectWidget::RowText(OnTheSurface);
	const FString UnreadableText = UCataclysmCharacterSelectWidget::RowText(AnUnreadableRow());
	AddInfo(FString::Printf(TEXT("SELECT ROWS: [%s] [%s] [%s] [%s]"), *SoloText, *UnnamedText, *SurfaceText, *UnreadableText));

	TestTrue(TEXT("the name"), SoloText.StartsWith(TEXT("Vesper, ")));
	TestTrue(TEXT("the level"), SoloText.Contains(TEXT("level 12")));
	TestTrue(TEXT("the lethality"), SoloText.Contains(TEXT("Hardcore")));
	TestTrue(TEXT("the Solo Self-Found flag"), SoloText.Contains(TEXT("Solo Self-Found")));
	TestTrue(TEXT("the population"), SoloText.Contains(TEXT("Online")));
	TestTrue(TEXT("the weapon type"), SoloText.Contains(TEXT("Greatsword")));
	TestTrue(TEXT("the run's day and the floor it was saved on"), SoloText.Contains(TEXT("in a run: day 9, floor 3 of Crypt")));
	TestTrue(TEXT("an unnamed character says so"), UnnamedText.StartsWith(TEXT("(unnamed)")));
	TestTrue(TEXT("a run saved outside a dungeon gives its day and no floor"),
			 SurfaceText.EndsWith(TEXT("in a run: day 21")));
	TestTrue(TEXT("an unreadable slot is shown as unreadable"), UnreadableText.StartsWith(TEXT("Unreadable save")));
	TestTrue(TEXT("with why"), UnreadableText.Contains(TEXT("not a JSON object")));
	return true;
}

/**
 * Continue opens the level a character's run needs -- the dungeon level for a run saved inside a dungeon, the sandbox
 * otherwise -- and hands the character to the game instance; an unreadable row cannot be continued and says why; New
 * opens the sandbox with nothing pending. Travel does not run in an automation test, so the level asked for is read.
 * Issue #753, select-screen step 3a. Prints "SELECT CONTINUE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectContinueTest,
	"Cataclysm.CharacterSelect.ContinueOpensTheLevelTheRunNeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectContinueTest::RunTest(const FString&)
{
	using namespace CataclysmCharacterSelectTest;

	const FCataclysmCharacterSummary InADungeon = InARun(ARow(TEXT("Deep"), 8), 5, TEXT("Crypt"), 3);
	const FCataclysmCharacterSummary InTheSandbox = InARun(ARow(TEXT("Near"), 3), 2, TEXT("Sandbox"), 1);
	const FCataclysmCharacterSummary InNoRun = ARow(TEXT("Idle"), 1);
	FString DungeonMap, SandboxMap, NoRunMap, UnreadableMap, Why;
	const bool bDungeon = UCataclysmCharacterSelectWidget::MapToContinue(InADungeon, DungeonMap, Why);
	const bool bSandbox = UCataclysmCharacterSelectWidget::MapToContinue(InTheSandbox, SandboxMap, Why);
	const bool bNoRun = UCataclysmCharacterSelectWidget::MapToContinue(InNoRun, NoRunMap, Why);
	FString UnreadableWhy;
	const bool bUnreadable = UCataclysmCharacterSelectWidget::MapToContinue(AnUnreadableRow(), UnreadableMap, UnreadableWhy);

	UCataclysmCharacterSelectWidget* Screen = NewObject<UCataclysmCharacterSelectWidget>();
	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	Screen->SetRowsForTests({InADungeon, AnUnreadableRow()});
	const bool bContinued = Screen->ContinueAt(0, Instance);
	const FGuid Handed = Instance->PendingCharacterId;
	const FString ContinueLevel = Screen->LevelRequested();
	Instance->PendingCharacterId.Invalidate();
	const bool bUnreadableContinued = Screen->ContinueAt(1, Instance);
	const FString Refusal = Screen->RefusalText();
	Instance->PendingCharacterId = InADungeon.CharacterId;
	Screen->StartNew(Instance);

	AddInfo(FString::Printf(TEXT("SELECT CONTINUE: dungeon run -> %s, sandbox run -> %s, no run -> %s, unreadable %s (%s); "
								 "Continue %s, handed %s, opened %s; New asked for the new-character screen: %s, pending %s"),
							*DungeonMap, *SandboxMap, *NoRunMap, bUnreadable ? TEXT("CONTINUED") : TEXT("refused"),
							*UnreadableWhy, bContinued ? TEXT("went") : TEXT("REFUSED"),
							Handed == InADungeon.CharacterId ? TEXT("the character") : TEXT("NOTHING"), *ContinueLevel,
							Screen->NewCharacterRequested() ? TEXT("yes") : TEXT("NO"),
							Instance->PendingCharacterId.IsValid() ? TEXT("STILL SET") : TEXT("none")));

	TestTrue(TEXT("a run saved in a dungeon opens the dungeon level"),
			 bDungeon && DungeonMap == UCataclysmCharacterSelectWidget::DungeonMap);
	TestTrue(TEXT("a run saved in the sandbox opens the sandbox"),
			 bSandbox && SandboxMap == UCataclysmCharacterSelectWidget::SandboxMap);
	TestTrue(TEXT("a character in no run opens the sandbox"), bNoRun && NoRunMap == UCataclysmCharacterSelectWidget::SandboxMap);
	TestFalse(TEXT("an unreadable row cannot be continued"), bUnreadable);
	TestFalse(TEXT("and says why"), UnreadableWhy.IsEmpty());
	TestTrue(TEXT("Continue goes"), bContinued);
	TestEqual(TEXT("Continue hands the character to the game instance"), Handed, InADungeon.CharacterId);
	TestEqual(TEXT("and asks for its level"), ContinueLevel, FString(UCataclysmCharacterSelectWidget::DungeonMap));
	TestFalse(TEXT("Continue on an unreadable row is refused"), bUnreadableContinued);
	TestFalse(TEXT("and the screen shows why"), Refusal.IsEmpty());
	TestFalse(TEXT("New leaves nothing pending"), Instance->PendingCharacterId.IsValid());
	// RULED 2026-11-01, select-screen step 4a: New asks for the new-character screen, where it opened the sandbox ("and
	// opens the sandbox", LevelRequested == SandboxMap) until the creator gained the choices locked at creation.
	TestTrue(TEXT("and asks for the new-character screen"), Screen->NewCharacterRequested());
	return true;
}

/**
 * The select screen's controller asks for the menu input mode through the game's one function for an input mode
 * (issue #1015), with the cursor shown and clicks taken; the game's own controller still asks for the playing mode.
 * WHAT THIS CANNOT SEE: the mode the engine applies. `APlayerController::SetInputMode` applies a mode only with a game
 * viewport and a local player, and records its name only then; a test world has neither. So whether the menu's buttons
 * take clicks is for the owner's launch. Issue #753, select-screen step 3a, ruled 2026-10-31. Prints "SELECT INPUT MODE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectInputModeTest,
	"Cataclysm.CharacterSelect.TheFrontEndAsksForTheMenuInputMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectInputModeTest::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// DEFERRED AND NEVER FINISHED, so its `BeginPlay` does not run and look for the screen's asset, which step 3b makes.
	ACataclysmFrontEndPlayerController* FrontEnd = World->SpawnActorDeferred<ACataclysmFrontEndPlayerController>(
		ACataclysmFrontEndPlayerController::StaticClass(), FTransform::Identity);
	APlayerController* Playing = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("set-up: the front end's controller"), FrontEnd) || !TestNotNull(TEXT("set-up: a controller"), Playing))
	{
		return false;
	}
	UCataclysmCharacterSelectWidget* Menu = NewObject<UCataclysmCharacterSelectWidget>();
	const FString MenuMode = FrontEnd->UseTheMenuInputMode(Menu);
	FString PlayingMode;
	ACataclysmPlayerController::ApplyPlayingInputModeTo(*Playing, nullptr, &PlayingMode);
	AddInfo(FString::Printf(TEXT("SELECT INPUT MODE: the front end asked for '%s' (kept '%s'), cursor %s, clicks %s; the "
								 "game's controller '%s'"),
							*MenuMode, *FrontEnd->InputModeAskedFor(), FrontEnd->bShowMouseCursor ? TEXT("shown") : TEXT("HIDDEN"),
							FrontEnd->bEnableClickEvents ? TEXT("taken") : TEXT("NOT TAKEN"), *PlayingMode));

	TestEqual(TEXT("the front end asks for the menu mode"), FrontEnd->InputModeAskedFor(), FString(TEXT("Menu")));
	TestTrue(TEXT("with the cursor shown"), FrontEnd->bShowMouseCursor);
	TestTrue(TEXT("and clicks taken"), FrontEnd->bEnableClickEvents);
	TestEqual(TEXT("CONTROL: the game's own controller asks for the playing mode"), PlayingMode, FString(TEXT("Playing")));
	FrontEnd->Destroy();
	return true;
}

/**
 * The front end's controller, asked for the new-character screen when its asset does not load -- as between steps 4a
 * and 4b -- replaces nothing and says so: no crash, no input mode asked for, so the select screen stays. The asset's
 * path is pointed at one that does not exist, through the property, so the test still holds once step 4b makes the real
 * one. WHAT THIS CANNOT SEE: the select screen's refusal text, which needs the screen made by `CreateWidget` with the
 * controller as its owner, and a test world cannot (#1841). Select-screen step 4a, asked 2026-11-01. Prints "NEW SCREEN
 * MISSING".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectNewScreenMissingTest,
	"Cataclysm.CharacterSelect.AMissingNewCharacterScreenReplacesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectNewScreenMissingTest::RunTest(const FString&)
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a test world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// DEFERRED AND NEVER FINISHED, so its `BeginPlay` does not run and show the select screen.
	ACataclysmFrontEndPlayerController* FrontEnd = World->SpawnActorDeferred<ACataclysmFrontEndPlayerController>(
		ACataclysmFrontEndPlayerController::StaticClass(), FTransform::Identity);
	FSoftClassProperty* Asset = FindFProperty<FSoftClassProperty>(ACataclysmFrontEndPlayerController::StaticClass(),
																	 TEXT("NewCharacterScreenClass"));
	if (!TestNotNull(TEXT("set-up: the front end's controller"), FrontEnd)
		|| !TestNotNull(TEXT("set-up: its new-character screen's asset property"), Asset))
	{
		return false;
	}
	Asset->SetPropertyValue_InContainer(FrontEnd, FSoftObjectPtr(FSoftObjectPath(
		TEXT("/Game/Interface/WBP_NoSuchScreen.WBP_NoSuchScreen_C"))));
	AddExpectedError(TEXT("A front end screen's asset is missing"), EAutomationExpectedErrorFlags::Contains, 1);

	const bool bShown = FrontEnd->ShowTheNewCharacterScreen();
	AddInfo(FString::Printf(TEXT("NEW SCREEN MISSING: %s; input mode asked for '%s'; the select screen %s"),
							bShown ? TEXT("SHOWN") : TEXT("not shown"), *FrontEnd->InputModeAskedFor(),
							FrontEnd->SelectScreen() ? TEXT("PRESENT") : TEXT("as it was, none")));

	TestFalse(TEXT("a missing new-character screen is not shown"), bShown);
	TestTrue(TEXT("and replaces nothing: no input mode asked for"), FrontEnd->InputModeAskedFor().IsEmpty());
	FrontEnd->Destroy();
	return true;
}

/**
 * The select screen's asset: the path the front end's controller opens loads, is the screen's class, and its tree holds
 * the widgets the screen binds. Read from the asset's own widget tree, because a test world has no game instance for
 * `CreateWidget` (#1841), as the choice panel's asset test does. Fails until `tools/generate_interface_assets.py` has
 * made `WBP_CharacterSelect`. Issue #753, select-screen step 3b, ruled 2026-10-31. Prints "SELECT ASSET".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectAssetTest,
	"Cataclysm.CharacterSelect.TheSelectScreenAssetLoadsAndHoldsItsBoundWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectAssetTest::RunTest(const FString&)
{
	// THE PATH THE CONTROLLER ITSELF HOLDS, so the test cannot name a different one from the one play opens.
	const TSoftClassPtr<UCataclysmCharacterSelectWidget>& Path =
		GetDefault<ACataclysmFrontEndPlayerController>()->SelectScreenAsset();
	UClass* ScreenClass = Path.LoadSynchronous();
	AddInfo(FString::Printf(TEXT("SELECT ASSET: %s %s"), *Path.ToString(), ScreenClass ? TEXT("loads") : TEXT("DOES NOT LOAD")));
	if (!TestNotNull(TEXT("the select screen's asset loads"), ScreenClass))
	{
		return false;
	}
	TestTrue(TEXT("it is the select screen's class"), ScreenClass->IsChildOf(UCataclysmCharacterSelectWidget::StaticClass()));

	const UWidgetBlueprintGeneratedClass* Generated = Cast<UWidgetBlueprintGeneratedClass>(ScreenClass);
	const UWidgetTree* Tree = Generated ? Generated->GetWidgetTreeArchetype() : nullptr;
	if (!TestNotNull(TEXT("it is a widget blueprint with a tree"), Tree))
	{
		return false;
	}
	TestTrue(TEXT("its tree holds CharacterList, a panel"), Cast<UPanelWidget>(Tree->FindWidget(TEXT("CharacterList"))) != nullptr);
	TestTrue(TEXT("RefusalLabel, a text block"), Cast<UTextBlock>(Tree->FindWidget(TEXT("RefusalLabel"))) != nullptr);
	TestTrue(TEXT("NewButton, a button"), Cast<UButton>(Tree->FindWidget(TEXT("NewButton"))) != nullptr);
	TestTrue(TEXT("and TitleLabel, a text block"), Cast<UTextBlock>(Tree->FindWidget(TEXT("TitleLabel"))) != nullptr);
	// AND STEP 5'S DELETE AND ITS CONFIRMATION, ruled 2026-11-01: window A remakes the asset with them.
	TestTrue(TEXT("DeleteButton, a button"), Cast<UButton>(Tree->FindWidget(TEXT("DeleteButton"))) != nullptr);
	TestTrue(TEXT("ConfirmPanel, a panel"), Cast<UPanelWidget>(Tree->FindWidget(TEXT("ConfirmPanel"))) != nullptr);
	TestTrue(TEXT("ConfirmText, a text block"), Cast<UTextBlock>(Tree->FindWidget(TEXT("ConfirmText"))) != nullptr);
	TestTrue(TEXT("ConfirmNameBox, an editable text box"),
			 Cast<UEditableTextBox>(Tree->FindWidget(TEXT("ConfirmNameBox"))) != nullptr);
	TestTrue(TEXT("ConfirmDeleteButton, a button"), Cast<UButton>(Tree->FindWidget(TEXT("ConfirmDeleteButton"))) != nullptr);
	TestTrue(TEXT("CancelButton, a button"), Cast<UButton>(Tree->FindWidget(TEXT("CancelButton"))) != nullptr);
	return true;
}

/**
 * The confirmation a delete asks for, as Diablo IV asks it: the character's name typed exactly, case and all, or DELETE
 * for a save with no name to type. Its text says what goes: the run when it is in one; for a Solo Self-Found character
 * its own empire upgrade tree and private stash (rulings 4 and 5), which an ordinary one's does not mention; for an
 * unreadable save, that none of that can be told, that any run stays and its backups are kept. Issue #753, select-screen
 * step 5, ruled 2026-11-01. Prints "DELETE WARNING".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectDeleteWarningTest,
	"Cataclysm.CharacterSelect.ADeleteWarnsWhatGoesAndAsksForTheName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectDeleteWarningTest::RunTest(const FString&)
{
	using namespace CataclysmCharacterSelectTest;
	using FScreen = UCataclysmCharacterSelectWidget;

	FCataclysmCharacterSummary Solo = InARun(ARow(TEXT("Vesper"), 12), 9, TEXT("Crypt"), 3);
	Solo.Partition.bSoloSelfFound = true;
	const FCataclysmCharacterSummary Ordinary = ARow(TEXT("Corin"), 4);
	const FCataclysmCharacterSummary Unnamed = ARow(TEXT(""), 1);
	const FCataclysmCharacterSummary Unreadable = AnUnreadableRow();
	const FString SoloText = FScreen::DeleteWarning(Solo);
	const FString OrdinaryText = FScreen::DeleteWarning(Ordinary);
	const FString UnreadableText = FScreen::DeleteWarning(Unreadable);
	AddInfo(FString::Printf(TEXT("DELETE WARNING: Solo [%s] Ordinary [%s] Unreadable [%s]; words '%s', '%s', '%s'"),
							*SoloText, *OrdinaryText, *UnreadableText, *FScreen::ConfirmationWordFor(Solo),
							*FScreen::ConfirmationWordFor(Unnamed), *FScreen::ConfirmationWordFor(Unreadable)));

	TestTrue(TEXT("a Solo Self-Found character's warning names its empire upgrade tree"),
			 SoloText.Contains(TEXT("empire upgrade tree")));
	TestTrue(TEXT("and its private stash"), SoloText.Contains(TEXT("private stash")));
	TestTrue(TEXT("and its run, by its day"), SoloText.Contains(TEXT("run, on day 9")));
	TestFalse(TEXT("an ordinary character's names neither"),
			  OrdinaryText.Contains(TEXT("empire upgrade tree")) || OrdinaryText.Contains(TEXT("stash")));
	TestTrue(TEXT("an unreadable save's says it cannot be read"), UnreadableText.Contains(TEXT("cannot be read")));
	TestTrue(TEXT("and that its backups are kept"), UnreadableText.Contains(TEXT("backups are kept")));
	TestEqual(TEXT("a named character is confirmed by its name"), FScreen::ConfirmationWordFor(Solo), FString(TEXT("Vesper")));
	TestEqual(TEXT("an unnamed one by DELETE"), FScreen::ConfirmationWordFor(Unnamed), FString(FScreen::DeleteWord));
	TestEqual(TEXT("an unreadable one by DELETE"), FScreen::ConfirmationWordFor(Unreadable), FString(FScreen::DeleteWord));
	TestTrue(TEXT("the name typed exactly confirms"), FScreen::ConfirmationMatches(Solo, TEXT("Vesper")));
	TestFalse(TEXT("the name in the wrong case does not"), FScreen::ConfirmationMatches(Solo, TEXT("vesper")));
	TestFalse(TEXT("nor with a space after it"), FScreen::ConfirmationMatches(Solo, TEXT("Vesper ")));
	return true;
}

/**
 * Delete mode on the select screen: a row clicked in it asks to delete its character and does not continue it, an
 * unreadable row included; the wrong name typed keeps Delete refused and deletes nothing; Cancel closes the confirmation
 * and keeps the rows; and out of delete mode a row no longer asks. Rows are given, and nothing here reaches a file.
 * Issue #753, select-screen step 5, ruled 2026-11-01. Prints "DELETE MODE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectDeleteModeTest,
	"Cataclysm.CharacterSelect.DeleteModeAsksAndTheWrongNameDeletesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectDeleteModeTest::RunTest(const FString&)
{
	using namespace CataclysmCharacterSelectTest;

	UCataclysmCharacterSelectWidget* Screen = NewObject<UCataclysmCharacterSelectWidget>();
	Screen->SetRowsForTests({ARow(TEXT("Vesper"), 12), AnUnreadableRow()});

	Screen->SetDeleteMode(true);
	Screen->ClickRow(0);
	const int32 Asked = Screen->PendingDeleteIndex();
	const FString Requested = Screen->LevelRequested();
	const FString Text = Screen->ConfirmationText();
	Screen->SetConfirmationTyped(TEXT("vesper"));
	const bool bWrongCanConfirm = Screen->CanConfirmDelete();
	const bool bWrongConfirmed = Screen->ConfirmDelete();
	const FString WrongWhy = Screen->RefusalText();
	const int32 DeletedAfterWrong = Screen->LastDeleted().Num();
	Screen->SetConfirmationTyped(TEXT("Vesper"));
	const bool bRightCanConfirm = Screen->CanConfirmDelete();
	Screen->CancelDelete();
	const int32 AfterCancel = Screen->PendingDeleteIndex();
	const int32 RowsAfterCancel = Screen->Rows().Num();
	Screen->ClickRow(1);
	Screen->SetConfirmationTyped(TEXT("DELETE"));
	const bool bUnreadableCanConfirm = Screen->PendingDeleteIndex() == 1 && Screen->CanConfirmDelete();
	Screen->SetDeleteMode(false);
	Screen->ClickRow(1);
	const int32 OutOfMode = Screen->PendingDeleteIndex();
	AddInfo(FString::Printf(TEXT("DELETE MODE: row 0 asked %d, level '%s', text [%s]; 'vesper' %s, confirm %s (%s), %d deleted; "
								 "'Vesper' %s; after Cancel %d, %d rows; the unreadable row with DELETE %s; out of the mode %d"),
							Asked, *Requested, *Text, bWrongCanConfirm ? TEXT("MATCHES") : TEXT("does not match"),
							bWrongConfirmed ? TEXT("WENT") : TEXT("refused"), *WrongWhy, DeletedAfterWrong,
							bRightCanConfirm ? TEXT("matches") : TEXT("DOES NOT MATCH"), AfterCancel, RowsAfterCancel,
							bUnreadableCanConfirm ? TEXT("can be confirmed") : TEXT("CANNOT"), OutOfMode));

	TestEqual(TEXT("in delete mode a row asks to delete its character"), Asked, 0);
	TestTrue(TEXT("and does not continue it"), Requested.IsEmpty());
	TestTrue(TEXT("the confirmation names it"), Text.Contains(TEXT("Vesper")));
	TestFalse(TEXT("the wrong name keeps Delete disabled"), bWrongCanConfirm);
	TestFalse(TEXT("and Delete with it deletes nothing"), bWrongConfirmed || DeletedAfterWrong != 0);
	TestTrue(TEXT("and says what to type"), WrongWhy.Contains(TEXT("Vesper")));
	TestTrue(TEXT("the name typed exactly enables Delete"), bRightCanConfirm);
	TestEqual(TEXT("Cancel closes the confirmation"), AfterCancel, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and keeps the rows"), RowsAfterCancel, 2);
	TestTrue(TEXT("an unreadable row asks too, confirmed by DELETE"), bUnreadableCanConfirm);
	TestEqual(TEXT("out of delete mode a row no longer asks"), OutOfMode, static_cast<int32>(INDEX_NONE));
	return true;
}

/**
 * The new-character screen's asset: the path the front end's controller opens when New is pressed loads, is the
 * new-character screen's class, and its tree holds the twelve widgets the screen binds, by name and type -- six from
 * the creator it derives from and six of its own. Read from the asset's own widget tree, as the select screen's asset
 * test does, because a test world cannot make the screen with `CreateWidget` (#1841); WHAT THIS CANNOT SEE is the
 * screen shown when New is pressed, which is on the owner's launch list. Fails until
 * `tools/generate_interface_assets.py` has made `WBP_NewCharacter`. Issue #753, select-screen step 4b, ruled
 * 2026-11-01. Prints "NEW CHARACTER ASSET".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNewCharacterAssetTest,
	"Cataclysm.CharacterSelect.TheNewCharacterScreenAssetLoadsAndHoldsItsBoundWidgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmNewCharacterAssetTest::RunTest(const FString&)
{
	// THE PATH THE CONTROLLER ITSELF HOLDS, so the test cannot name a different one from the one New opens.
	const TSoftClassPtr<UCataclysmNewCharacterWidget>& Path =
		GetDefault<ACataclysmFrontEndPlayerController>()->NewCharacterScreenAsset();
	UClass* ScreenClass = Path.LoadSynchronous();
	AddInfo(FString::Printf(TEXT("NEW CHARACTER ASSET: %s %s"), *Path.ToString(),
							ScreenClass ? TEXT("loads") : TEXT("DOES NOT LOAD")));
	if (!TestNotNull(TEXT("the new-character screen's asset loads"), ScreenClass))
	{
		return false;
	}
	TestTrue(TEXT("it is the new-character screen's class"),
			 ScreenClass->IsChildOf(UCataclysmNewCharacterWidget::StaticClass()));

	const UWidgetBlueprintGeneratedClass* Generated = Cast<UWidgetBlueprintGeneratedClass>(ScreenClass);
	const UWidgetTree* Tree = Generated ? Generated->GetWidgetTreeArchetype() : nullptr;
	if (!TestNotNull(TEXT("it is a widget blueprint with a tree"), Tree))
	{
		return false;
	}
	auto Holds = [this, Tree](const TCHAR* Name, UClass* Type)
	{
		const UWidget* Found = Tree->FindWidget(FName(Name));
		TestTrue(FString::Printf(TEXT("its tree holds %s, a %s"), Name, *Type->GetName()),
				 Found && Found->IsA(Type));
	};
	// THE CREATOR'S SIX.
	Holds(TEXT("WeaponTypeBox"), UPanelWidget::StaticClass());
	Holds(TEXT("DamageTypeBox"), UPanelWidget::StaticClass());
	Holds(TEXT("SummaryLabel"), UTextBlock::StaticClass());
	Holds(TEXT("UnlockedLabel"), UTextBlock::StaticClass());
	Holds(TEXT("RefusalLabel"), UTextBlock::StaticClass());
	Holds(TEXT("ConfirmButton"), UButton::StaticClass());
	// AND THE NEW SCREEN'S SIX.
	Holds(TEXT("NameBox"), UEditableTextBox::StaticClass());
	Holds(TEXT("LethalityBox"), UPanelWidget::StaticClass());
	Holds(TEXT("SoloSelfFoundBox"), UCheckBox::StaticClass());
	Holds(TEXT("PopulationBox"), UPanelWidget::StaticClass());
	Holds(TEXT("CreationRefusalLabel"), UTextBlock::StaticClass());
	Holds(TEXT("BackButton"), UButton::StaticClass());
	return true;
}

/**
 * The game starts on the front end's level, and that level's game mode is the front end's: `GameDefaultMap` names the
 * level `ACataclysmGameMode::FrontEndMap` names, which a refused load returns to, and its world settings name
 * `ACataclysmFrontEndGameMode`. Fails until step 3b's generator run has made `L_FrontEnd` and the start map names it.
 * Issue #753, select-screen step 3b, ruled 2026-10-31. Prints "SELECT START MAP".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectStartMapTest,
	"Cataclysm.CharacterSelect.TheGameStartsOnTheFrontEndMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectStartMapTest::RunTest(const FString&)
{
	const FString FrontEnd = ACataclysmGameMode::FrontEndMap;
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *FrontEnd, *FPackageName::GetShortName(FrontEnd));

	// THE START MAP AS THE ENGINE'S CONFIG HOLDS IT, the setting `UGameMapsSettings` reads; read here, because that class
	// is in the EngineSettings module, which this module does not depend on.
	FString StartMap;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), StartMap, GEngineIni);
	const UWorld* Level = LoadObject<UWorld>(nullptr, *ObjectPath);
	const AWorldSettings* Settings = Level && Level->PersistentLevel ? Level->PersistentLevel->GetWorldSettings() : nullptr;
	const UClass* GameMode = Settings ? Settings->DefaultGameMode.Get() : nullptr;
	AddInfo(FString::Printf(TEXT("SELECT START MAP: the game starts on '%s'; '%s' %s, its game mode %s"), *StartMap,
							*ObjectPath, Level ? TEXT("loads") : TEXT("DOES NOT LOAD"),
							GameMode ? *GameMode->GetName() : TEXT("NONE")));

	TestEqual(TEXT("the game's start map is the front end"), StartMap, ObjectPath);
	TestNotNull(TEXT("the front end's level loads"), Level);
	TestTrue(TEXT("its game mode is the front end's"), GameMode == ACataclysmFrontEndGameMode::StaticClass());
	return true;
}

/**
 * New is refused when the account holds 24 characters, "as one pool" (`docs/Cataclysm_GDD_v2.md`), counting an
 * unreadable slot, which still holds one; with 23 it asks for the new-character screen. The rows are given, not read
 * from the SaveGames folder, which holds other tests' saves (#2658). Issue #753, select-screen step 4a. Prints "NEW AT 24".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCharacterSelectNewAtTwentyFourTest,
	"Cataclysm.CharacterSelect.NewIsRefusedWhenTheAccountHoldsTwentyFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCharacterSelectNewAtTwentyFourTest::RunTest(const FString&)
{
	using namespace CataclysmCharacterSelectTest;

	UCataclysmGameInstance* Instance = NewObject<UCataclysmGameInstance>();
	TArray<FCataclysmCharacterSummary> TwentyFour;
	for (int32 Index = 0; Index < 23; ++Index)
	{
		TwentyFour.Add(ARow(TEXT("Held"), 1));
	}
	TwentyFour.Add(AnUnreadableRow());
	TArray<FCataclysmCharacterSummary> TwentyThree = TwentyFour;
	TwentyThree.Pop();

	UCataclysmCharacterSelectWidget* Full = NewObject<UCataclysmCharacterSelectWidget>();
	Full->SetRowsForTests(TwentyFour);
	const bool bAtTwentyFour = Full->StartNew(Instance);
	UCataclysmCharacterSelectWidget* Room = NewObject<UCataclysmCharacterSelectWidget>();
	Room->SetRowsForTests(TwentyThree);
	Instance->PendingCharacterId = FGuid::NewGuid();
	const bool bAtTwentyThree = Room->StartNew(Instance);
	AddInfo(FString::Printf(TEXT("NEW AT 24: with 24 (one unreadable) %s (%s); with 23 %s, the new-character screen %s"),
							bAtTwentyFour ? TEXT("WENT") : TEXT("refused"), *Full->RefusalText(),
							bAtTwentyThree ? TEXT("went") : TEXT("REFUSED"),
							Room->NewCharacterRequested() ? TEXT("asked for") : TEXT("NOT ASKED FOR")));

	TestFalse(TEXT("New is refused at 24 characters, an unreadable slot among them"), bAtTwentyFour);
	TestTrue(TEXT("and says why"), Full->RefusalText().Contains(TEXT("24")));
	TestFalse(TEXT("and asks for no new-character screen"), Full->NewCharacterRequested());
	TestTrue(TEXT("with 23, New goes"), bAtTwentyThree);
	TestTrue(TEXT("and asks for the new-character screen"), Room->NewCharacterRequested());
	TestFalse(TEXT("with nothing pending"), Instance->PendingCharacterId.IsValid());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
