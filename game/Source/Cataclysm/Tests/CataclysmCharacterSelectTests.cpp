// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Interface/CataclysmCharacterSelectWidget.h"
#include "Player/CataclysmGameInstance.h"
#include "Save/CataclysmSaveLoad.h"

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
								 "Continue %s, handed %s, opened %s; New opened %s, pending %s"),
							*DungeonMap, *SandboxMap, *NoRunMap, bUnreadable ? TEXT("CONTINUED") : TEXT("refused"),
							*UnreadableWhy, bContinued ? TEXT("went") : TEXT("REFUSED"),
							Handed == InADungeon.CharacterId ? TEXT("the character") : TEXT("NOTHING"), *ContinueLevel,
							*Screen->LevelRequested(), Instance->PendingCharacterId.IsValid() ? TEXT("STILL SET") : TEXT("none")));

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
	TestEqual(TEXT("and opens the sandbox"), Screen->LevelRequested(), FString(UCataclysmCharacterSelectWidget::SandboxMap));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
