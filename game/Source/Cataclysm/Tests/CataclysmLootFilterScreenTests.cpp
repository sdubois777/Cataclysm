// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interface/CataclysmLootFilterLayout.h"
#include "Interface/CataclysmLootFilterWidget.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmLootFilter.h"
#include "Items/CataclysmLootFilterPreferences.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

/**
 * The loot filter's preferences file and its screen, issue #2846, slice LF2.
 *
 * WHAT IS NOT COVERED, said plainly: the drawing, and a click reaching `Press`. A headless test has no Widget Blueprint,
 * so the screen's panels and buttons never exist; the tests press the same commands the buttons carry and read the
 * labels the redraw would give them (`ButtonsShown`). Nor is the real preferences file touched: the file tests write to
 * a path of their own under the project's Saved folder and delete it.
 */
namespace CataclysmLootFilterScreenTest
{
	using FPrefs = UCataclysmLootFilterStore;
	using FLayout = UCataclysmLootFilterLayout;

	/** A player's filter with `RuleCount` Show rules. */
	FCataclysmLootFilter Own(const TCHAR* Name, int32 RuleCount = 0)
	{
		FCataclysmLootFilter Filter;
		Filter.Name = Name;
		Filter.Rules.SetNum(RuleCount);
		return Filter;
	}

	/** Whether any button the screen last drew carries this command. */
	bool Shows(const UCataclysmLootFilterWidget& Screen, const FString& Command)
	{
		return Screen.ButtonsShown().ContainsByPredicate([&Command](const TPair<FString, FString>& Each)
		{
			return Each.Key == Command;
		});
	}
}

// ---------------------------------------------------------------------------
// LS1. The three built-in names cannot be taken, changed or deleted
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterBuiltInsProtectedTest,
	"Cataclysm.LootFilterScreen.TheBuiltInNamesCannotBeTakenChangedOrDeleted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The coordinating session's condition of 2026-11-22: the three built-in names must be impossible to overwrite. Every
 * change to a built-in is refused with the built-in sentence; a rename to one, in another case and with spaces, is
 * refused; and a file holding a player's filter called "Strict" has it renamed when read, while "Strict" still finds
 * the built-in with its two rules.
 */
bool FCataclysmLootFilterBuiltInsProtectedTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterScreenTest;
	FCataclysmLootFilterPreferences Prefs = FPrefs::NewPlayer();
	Prefs.Filters.Add(Own(TEXT("Mine"), 1));
	FString Refusal;
	FString Name;

	TestEqual(TEXT("three built-ins"), FPrefs::BuiltInNames().Num(), 3);
	for (const FString& BuiltIn : FPrefs::BuiltInNames())
	{
		const TCHAR* Why = FPrefs::BuiltInRefusal;
		Refusal.Reset();
		TestFalse(FString::Printf(TEXT("**no rule is added to %s**"), *BuiltIn), FPrefs::AddRule(Prefs, BuiltIn, Refusal));
		TestEqual(TEXT("with the built-in sentence"), Refusal, FString(Why));
		TestFalse(TEXT("**none removed**"), FPrefs::RemoveRule(Prefs, BuiltIn, 0, Refusal));
		TestFalse(TEXT("**none moved**"), FPrefs::MoveRule(Prefs, BuiltIn, 0, 1, Refusal));
		TestFalse(TEXT("**none replaced**"), FPrefs::SetRule(Prefs, BuiltIn, 0, FCataclysmLootFilterRule(), Refusal));
		TestFalse(TEXT("**it is not renamed**"), FPrefs::RenameFilter(Prefs, BuiltIn, TEXT("Other"), Refusal));
		TestFalse(TEXT("**it is not deleted**"), FPrefs::DeleteFilter(Prefs, BuiltIn, Refusal));
		TestEqual(TEXT("and the built-in sentence again"), Refusal, FString(Why));
	}

	TestFalse(TEXT("**Mine cannot be renamed \" strict \"**"), FPrefs::RenameFilter(Prefs, TEXT("Mine"), TEXT(" strict "), Refusal));
	TestEqual(TEXT("because the name is taken"), Refusal, FString(FPrefs::NameTakenRefusal));
	TestFalse(TEXT("**nor \"EVERYTHING\"**"), FPrefs::RenameFilter(Prefs, TEXT("Mine"), TEXT("EVERYTHING"), Refusal));
	TestTrue(TEXT("CONTROL: Mine can be renamed \"My own\""), FPrefs::RenameFilter(Prefs, TEXT("Mine"), TEXT("My own"), Refusal));

	FCataclysmLootFilterPreferences Edited = FPrefs::NewPlayer();
	Edited.Filters.Add(Own(TEXT("Strict"), 0));
	Edited.Active = TEXT("Strict");
	const FCataclysmLootFilterPreferences Read = FPrefs::Sanitised(Edited);
	TestTrue(TEXT("**a stored \"Strict\" is renamed when read**"),
			 Read.Filters.Num() == 1 && Read.Filters[0].Name == TEXT("My filter 1"));
	FCataclysmLootFilter Found;
	TestTrue(TEXT("**and \"Strict\" still finds the built-in, with its two rules**"),
			 FPrefs::Find(Read, TEXT("Strict"), Found) && Found.Rules.Num() == 2);
	TestTrue(TEXT("CONTROL: a built-in can be copied"), FPrefs::CopyFilter(Prefs, TEXT("Strict"), Name, Refusal));
	return true;
}

// ---------------------------------------------------------------------------
// LS2. The file keeps the player's filters and the active one, never a built-in
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterFileTest,
	"Cataclysm.LootFilterScreen.TheFileKeepsThePlayersFiltersAndTheActiveOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A filter with a restyle rule, made active, written to a file of the test's own and read back: the same name, rule
 * and active name. The file holds no built-in. No file reads as a new player, with the owner's default active, and so
 * does a file that is not the preferences' JSON. The real file's path is under Saved/Preferences, not a save slot.
 */
bool FCataclysmLootFilterFileTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterScreenTest;
	FCataclysmLootFilterPreferences Prefs = FPrefs::NewPlayer();
	FCataclysmLootFilter Mine = Own(TEXT("Rings only"));
	FCataclysmLootFilterRule Rule;
	Rule.Action = ECataclysmLootFilterAction::Restyle;
	Rule.Slots = {TEXT("Ring")};
	Rule.TextColour = 9;
	Rule.Size = 2;
	Mine.Rules.Add(Rule);
	Prefs.Filters.Add(Mine);
	Prefs.Active = TEXT("Rings only");

	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("LootFilterScreenTest.json"));
	ON_SCOPE_EXIT { IFileManager::Get().Delete(*Path); };
	if (!TestTrue(TEXT("set-up: the file is written"), FPrefs::Save(Path, Prefs)))
	{
		return false;
	}
	FCataclysmLootFilterPreferences Back;
	TestTrue(TEXT("it reads"), FPrefs::Load(Path, Back));
	TestEqual(TEXT("**the active name comes back**"), Back.Active, FString(TEXT("Rings only")));
	TestTrue(TEXT("**the filter and its rule come back**"),
			 Back.Filters.Num() == 1 && Back.Filters[0].Rules.Num() == 1
				 && Back.Filters[0].Rules[0].Action == ECataclysmLootFilterAction::Restyle
				 && Back.Filters[0].Rules[0].Slots == TArray<FString>({TEXT("Ring")})
				 && Back.Filters[0].Rules[0].TextColour == 9 && Back.Filters[0].Rules[0].Size == 2);

	const FString Json = FPrefs::ToJson(Prefs);
	TestFalse(TEXT("**no built-in's rules are written: Strict's Quality rarity is not in the file**"),
			  Json.Contains(TEXT("Quality")));

	FCataclysmLootFilterPreferences Missing;
	TestFalse(TEXT("no file does not read"), FPrefs::Load(Path + TEXT(".absent"), Missing));
	TestEqual(TEXT("**and is a new player: the owner's default active**"), FPrefs::ActiveFilter(Missing).Name,
			  FString(UCataclysmLootFilterRules::DefaultFilterName));
	FCataclysmLootFilterPreferences Garbled;
	TestFalse(TEXT("text that is not the file's JSON does not read"), FPrefs::FromJson(TEXT("not json"), Garbled));
	TestEqual(TEXT("and is a new player too"), Garbled.Filters.Num(), 0);

	TestTrue(TEXT("the real file is Saved/Preferences/LootFilters.json"),
			 FPrefs::FilePath().EndsWith(TEXT("Preferences/LootFilters.json")));
	return true;
}

// ---------------------------------------------------------------------------
// LS3. Reading a file holds every limit
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterSanitisedTest,
	"Cataclysm.LootFilterScreen.ReadingAFileHoldsEveryLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A file edited by hand to hold twelve filters, one of 80 rules, an empty name, a duplicate and a name of 41
 * characters, with an active name that names nothing: read as ten filters, the long one cut to 75 rules, every name
 * unique and kept to the rules, and the owner's default active (N1 75 rules, N3 10 filters).
 */
bool FCataclysmLootFilterSanitisedTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterScreenTest;
	FCataclysmLootFilterPreferences Edited;
	Edited.Filters.Add(Own(TEXT("Long"), 80));
	Edited.Filters.Add(Own(TEXT("")));
	Edited.Filters.Add(Own(TEXT("Twice")));
	Edited.Filters.Add(Own(TEXT("twice")));
	Edited.Filters.Add(Own(*FString::ChrN(41, TEXT('x'))));
	for (int32 Index = 0; Index < 7; ++Index)
	{
		Edited.Filters.Add(Own(*FString::Printf(TEXT("Extra %d"), Index)));
	}
	Edited.Active = TEXT("Nothing of that name");

	const FCataclysmLootFilterPreferences Read = FPrefs::Sanitised(Edited);
	TestEqual(TEXT("**twelve filters read as ten**"), Read.Filters.Num(), FPrefs::MaxSavedFilters);
	TestEqual(TEXT("**80 rules read as 75**"), Read.Filters[0].Rules.Num(), UCataclysmLootFilterRules::MaxRules);
	TestEqual(TEXT("the empty name is replaced"), Read.Filters[1].Name, FString(TEXT("My filter 1")));
	TestEqual(TEXT("the first \"Twice\" is kept"), Read.Filters[2].Name, FString(TEXT("Twice")));
	TestEqual(TEXT("**the duplicate is renamed**"), Read.Filters[3].Name, FString(TEXT("My filter 2")));
	TestEqual(TEXT("**the 41-character name is renamed**"), Read.Filters[4].Name, FString(TEXT("My filter 3")));
	TestEqual(TEXT("**an active name that names nothing becomes the default**"), Read.Active,
			  FString(UCataclysmLootFilterRules::DefaultFilterName));
	return true;
}

// ---------------------------------------------------------------------------
// LS4. New, copy, rename, delete, and the filter in use
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterManageTest,
	"Cataclysm.LootFilterScreen.NewCopyRenameDeleteAndTheFilterInUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * New filters are "My filter 1" and "My filter 2"; a copy of Strict holds Strict's two rules; a filter made active and
 * renamed stays active under its new name; deleting it makes the default active; the eleventh filter is refused.
 */
bool FCataclysmLootFilterManageTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterScreenTest;
	FCataclysmLootFilterPreferences Prefs = FPrefs::NewPlayer();
	FString Name;
	FString Refusal;
	TestTrue(TEXT("a new filter"), FPrefs::NewFilter(Prefs, Name, Refusal));
	TestEqual(TEXT("**named My filter 1**"), Name, FString(TEXT("My filter 1")));
	TestTrue(TEXT("a copy of Strict"), FPrefs::CopyFilter(Prefs, TEXT("Strict"), Name, Refusal));
	TestEqual(TEXT("**named My filter 2**"), Name, FString(TEXT("My filter 2")));
	FCataclysmLootFilter Copy;
	TestTrue(TEXT("**holding Strict's two rules**"), FPrefs::Find(Prefs, Name, Copy) && Copy.Rules.Num() == 2);

	TestTrue(TEXT("My filter 2 made active"), FPrefs::SetActive(Prefs, TEXT("my filter 2"), Refusal));
	TestEqual(TEXT("in its own spelling"), Prefs.Active, FString(TEXT("My filter 2")));
	TestTrue(TEXT("renamed"), FPrefs::RenameFilter(Prefs, TEXT("My filter 2"), TEXT("  Tight  "), Refusal));
	TestEqual(TEXT("**still active, under its new name, trimmed**"), FPrefs::ActiveFilter(Prefs).Name, FString(TEXT("Tight")));
	TestFalse(TEXT("an empty name is refused"), FPrefs::RenameFilter(Prefs, TEXT("Tight"), TEXT("   "), Refusal));
	TestEqual(TEXT("with its sentence"), Refusal, FString(FPrefs::NameEmptyRefusal));
	TestFalse(TEXT("a name of 41 characters is refused"),
			  FPrefs::RenameFilter(Prefs, TEXT("Tight"), FString::ChrN(41, TEXT('y')), Refusal));
	TestTrue(TEXT("deleted"), FPrefs::DeleteFilter(Prefs, TEXT("Tight"), Refusal));
	TestEqual(TEXT("**the default is active again**"), FPrefs::ActiveFilter(Prefs).Name,
			  FString(UCataclysmLootFilterRules::DefaultFilterName));
	TestFalse(TEXT("an unknown filter cannot be made active"), FPrefs::SetActive(Prefs, TEXT("Nope"), Refusal));

	while (Prefs.Filters.Num() < FPrefs::MaxSavedFilters)
	{
		FPrefs::NewFilter(Prefs, Name, Refusal);
	}
	TestFalse(TEXT("**the eleventh filter is refused**"), FPrefs::NewFilter(Prefs, Name, Refusal));
	TestEqual(TEXT("with its sentence"), Refusal, FString(FPrefs::TooManyFiltersRefusal));
	TestFalse(TEXT("**and an eleventh copy**"), FPrefs::CopyFilter(Prefs, TEXT("Everything"), Name, Refusal));
	TestEqual(TEXT("the list: three built-ins, then ten"), FPrefs::AllNames(Prefs).Num(), 13);
	return true;
}

// ---------------------------------------------------------------------------
// LS5. Rules are added, moved, replaced and removed, to 75
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterRuleListTest,
	"Cataclysm.LootFilterScreen.RulesAreAddedMovedReplacedAndRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmLootFilterRuleListTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterScreenTest;
	FCataclysmLootFilterPreferences Prefs = FPrefs::NewPlayer();
	Prefs.Filters.Add(Own(TEXT("Mine")));
	FString Refusal;
	TestTrue(TEXT("a rule is added"), FPrefs::AddRule(Prefs, TEXT("Mine"), Refusal));
	TestTrue(TEXT("a second"), FPrefs::AddRule(Prefs, TEXT("Mine"), Refusal));

	FCataclysmLootFilterRule Hide;
	Hide.Action = ECataclysmLootFilterAction::Hide;
	TestTrue(TEXT("the second replaced by a Hide rule"), FPrefs::SetRule(Prefs, TEXT("Mine"), 1, Hide, Refusal));
	TestTrue(TEXT("**moved up**"), FPrefs::MoveRule(Prefs, TEXT("Mine"), 1, -1, Refusal));
	TestTrue(TEXT("**the Hide rule is now first**"),
			 Prefs.Filters[0].Rules[0].Action == ECataclysmLootFilterAction::Hide);
	TestFalse(TEXT("**nothing moves above the first**"), FPrefs::MoveRule(Prefs, TEXT("Mine"), 0, -1, Refusal));
	TestEqual(TEXT("with its sentence"), Refusal, FString(FPrefs::NoSuchRuleRefusal));
	TestTrue(TEXT("the first removed"), FPrefs::RemoveRule(Prefs, TEXT("Mine"), 0, Refusal));
	TestTrue(TEXT("one Show rule left"), Prefs.Filters[0].Rules.Num() == 1
										   && Prefs.Filters[0].Rules[0].Action == ECataclysmLootFilterAction::Show);

	while (Prefs.Filters[0].Rules.Num() < UCataclysmLootFilterRules::MaxRules)
	{
		FPrefs::AddRule(Prefs, TEXT("Mine"), Refusal);
	}
	TestFalse(TEXT("**the 76th rule is refused**"), FPrefs::AddRule(Prefs, TEXT("Mine"), Refusal));
	TestEqual(TEXT("with its sentence"), Refusal, FString(FPrefs::TooManyRulesRefusal));
	return true;
}

// ---------------------------------------------------------------------------
// LS6. The editor's buttons change every field, within its bounds
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterEditorTest,
	"Cataclysm.LootFilterScreen.TheEditorsButtonsChangeEveryFieldWithinItsBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every command the editor sends, on a fresh rule: each toggle adds and removes, each cycle wraps, each step stops at
 * its end, the lowest never passes the highest. An unknown command, an unknown slot and a rarity past Cataclysmic are
 * not commands. The restyle's buttons are offered only to a Restyle rule, and the rule line says what the rule does.
 */
bool FCataclysmLootFilterEditorTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterScreenTest;
	FCataclysmLootFilterRule Rule;

	TestTrue(TEXT("Action"), FLayout::EditRule(Rule, TEXT("Action")));
	TestTrue(TEXT("**Show becomes Hide**"), Rule.Action == ECataclysmLootFilterAction::Hide);
	FLayout::EditRule(Rule, TEXT("Action"));
	FLayout::EditRule(Rule, TEXT("Action"));
	TestTrue(TEXT("**and three presses come back to Show**"), Rule.Action == ECataclysmLootFilterAction::Show);

	FLayout::EditRule(Rule, TEXT("Kind"));
	TestTrue(TEXT("Any becomes Gear"), Rule.Kind == ECataclysmLootKind::Gear);
	FLayout::EditRule(Rule, TEXT("Rarity.1"));
	TestTrue(TEXT("**Rarity.1 adds Quality**"), Rule.Rarities == TArray<ECataclysmRarity>({ECataclysmRarity::Quality}));
	FLayout::EditRule(Rule, TEXT("Rarity.1"));
	TestEqual(TEXT("**and removes it again**"), Rule.Rarities.Num(), 0);
	TestFalse(TEXT("Rarity.8 is no rarity"), FLayout::EditRule(Rule, TEXT("Rarity.8")));
	FLayout::EditRule(Rule, TEXT("BelowWorn"));
	TestTrue(TEXT("below-worn on"), Rule.bBelowWornRarity);

	for (int32 Press = 0; Press < 10; ++Press)
	{
		FLayout::EditRule(Rule, TEXT("TierRungs+"));
	}
	TestEqual(TEXT("**tier rungs stop at 7**"), Rule.RungsBelowTierAtLeast, 7);

	TestTrue(TEXT("Slot.Ring"), FLayout::EditRule(Rule, TEXT("Slot.Ring")));
	TestTrue(TEXT("rings named"), Rule.Slots.Contains(TEXT("Ring")));
	TestFalse(TEXT("Slot.Tail is no slot"), FLayout::EditRule(Rule, TEXT("Slot.Tail")));
	TestEqual(TEXT("eleven slots offered"), FLayout::SlotNames().Num(), 11);

	FLayout::EditRule(Rule, TEXT("GearMax-"));
	FLayout::EditRule(Rule, TEXT("GearMax-"));
	TestEqual(TEXT("highest gear level 8"), Rule.MaxGearLevel, 8);
	for (int32 Press = 0; Press < 12; ++Press)
	{
		FLayout::EditRule(Rule, TEXT("GearMin+"));
	}
	TestEqual(TEXT("**the lowest stops at the highest, 8**"), Rule.MinGearLevel, 8);

	FLayout::EditRule(Rule, TEXT("Affix.Stat_Flat_maximum_health"));
	TestTrue(TEXT("an affix row added"), Rule.Affixes.Contains(FName(TEXT("Stat_Flat_maximum_health"))));
	FLayout::EditRule(Rule, TEXT("AffixTier-"));
	TestEqual(TEXT("affix tier stops at 1"), Rule.MinAffixTier, 1);
	FLayout::EditRule(Rule, TEXT("AffixCount+"));
	TestEqual(TEXT("two affixes needed"), Rule.MinAffixCount, 2);

	FLayout::EditRule(Rule, TEXT("Enchant"));
	TestTrue(TEXT("enchantment Yes"), Rule.HasEnchantment == ECataclysmLootTriState::Yes);
	FLayout::EditRule(Rule, TEXT("SocketsMax-"));
	TestEqual(TEXT("most sockets 5"), Rule.MaxSockets, 5);
	const FName FirstType = UCataclysmItemModifiers::DamageTypeNames()[0];
	TestTrue(TEXT("a damage type toggled on"), FLayout::EditRule(Rule, TEXT("Damage.") + FirstType.ToString())
											   && Rule.DamageTypes.Contains(FirstType));
	TestFalse(TEXT("Damage.Nothing is no damage type"), FLayout::EditRule(Rule, TEXT("Damage.Nothing")));
	FLayout::EditRule(Rule, TEXT("MatMin+"));
	TestEqual(TEXT("lowest material tier 2"), Rule.MinMaterialTier, 2);

	FLayout::EditRule(Rule, TEXT("Text-"));
	TestEqual(TEXT("**text colour from none back to palette 11**"), Rule.TextColour, 11);
	FLayout::EditRule(Rule, TEXT("Text+"));
	TestEqual(TEXT("**and forward to none**"), Rule.TextColour, INDEX_NONE);
	FLayout::EditRule(Rule, TEXT("Size+"));
	FLayout::EditRule(Rule, TEXT("Size+"));
	FLayout::EditRule(Rule, TEXT("Size+"));
	TestEqual(TEXT("size stops at 2"), Rule.Size, 2);
	TestFalse(TEXT("an unknown command"), FLayout::EditRule(Rule, TEXT("Colourful")));

	const TArray<FName> Affixes = {FName(TEXT("Stat_Flat_maximum_health"))};
	auto HasCommand = [](const TArray<FCataclysmLootEditorButton>& Buttons, const TCHAR* Command)
	{
		return Buttons.ContainsByPredicate([Command](const FCataclysmLootEditorButton& Each) { return Each.Command == Command; });
	};
	TestFalse(TEXT("**a Show rule offers no text colour**"),
			  HasCommand(FLayout::EditorButtonsFor(Rule, Affixes, 0, nullptr), TEXT("Text+")));
	FCataclysmLootFilterRule Restyle;
	Restyle.Action = ECataclysmLootFilterAction::Restyle;
	TestTrue(TEXT("**a Restyle rule does**"), HasCommand(FLayout::EditorButtonsFor(Restyle, Affixes, 0, nullptr), TEXT("Text+")));

	FCataclysmLootFilterRule Line;
	Line.Action = ECataclysmLootFilterAction::Hide;
	Line.Kind = ECataclysmLootKind::Gear;
	Line.Rarities = {ECataclysmRarity::Everyday, ECataclysmRarity::Quality};
	Line.bBelowWornRarity = true;
	TestEqual(TEXT("**the rule line**"), FLayout::RuleLine(Line, 0, nullptr),
			  FString(TEXT("1. Hide gear: Everyday or Quality; below what is worn")));
	TestEqual(TEXT("a rule with no condition"), FLayout::RuleLine(FCataclysmLootFilterRule(), 2, nullptr),
			  FString(TEXT("3. Show everything")));
	TestEqual(TEXT("the filter line"), FLayout::FilterLine(TEXT("Strict"), true, false, 2),
			  FString(TEXT("Strict, 2 rules (built in)")));
	return true;
}

// ---------------------------------------------------------------------------
// LS7. The screen presses through the model and shows a built-in's refusal
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterScreenPressTest,
	"Cataclysm.LootFilterScreen.TheScreenPressesThroughTheModelAndShowsRefusals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A screen with a new player's preferences: the default is picked and in use. "New filter", "Add a rule" and the
 * Action button make a filter with one Hide rule; "Use this filter" puts it in use. Picking the built-in "Everything"
 * and pressing "Add a rule" or an editor button is refused with the built-in sentence on the refusal line. Renaming to
 * "Strict" is refused; to "Mine" is done.
 */
bool FCataclysmLootFilterScreenPressTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterScreenTest;
	UCataclysmLootFilterWidget* Screen = NewObject<UCataclysmLootFilterWidget>(GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: a screen"), Screen))
	{
		return false;
	}
	Screen->SetPreferencesForTests(FPrefs::NewPlayer());
	TestEqual(TEXT("**the default is picked**"), Screen->SelectedFilter(), FString(UCataclysmLootFilterRules::DefaultFilterName));
	TestTrue(TEXT("its filter line is drawn"), Shows(*Screen, TEXT("Filter:1")));

	TestTrue(TEXT("New filter"), Screen->Press(TEXT("Do:New")));
	TestEqual(TEXT("**and it is picked**"), Screen->SelectedFilter(), FString(TEXT("My filter 1")));
	TestTrue(TEXT("Add a rule"), Screen->Press(TEXT("Do:AddRule")));
	TestEqual(TEXT("**the rule is picked**"), Screen->SelectedRule(), 0);
	TestTrue(TEXT("**the editor is drawn**"), Shows(*Screen, TEXT("Edit:Action")));
	TestTrue(TEXT("the Action button"), Screen->Press(TEXT("Edit:Action")));
	TestTrue(TEXT("**the rule is now Hide**"),
			 Screen->Preferences().Filters.Num() == 1 && Screen->Preferences().Filters[0].Rules.Num() == 1
				 && Screen->Preferences().Filters[0].Rules[0].Action == ECataclysmLootFilterAction::Hide);
	TestTrue(TEXT("Use this filter"), Screen->Press(TEXT("Do:Use")));
	TestEqual(TEXT("**it is in use**"), UCataclysmLootFilterStore::ActiveFilter(Screen->Preferences()).Name,
			  FString(TEXT("My filter 1")));

	TestTrue(TEXT("pick Everything"), Screen->Press(TEXT("Filter:0")));
	TestFalse(TEXT("**adding a rule to it is refused**"), Screen->Press(TEXT("Do:AddRule")));
	TestEqual(TEXT("**the refusal line says why**"), Screen->RefusalText(), FString(FPrefs::BuiltInRefusal));
	TestTrue(TEXT("pick Strict, which has rules"), Screen->Press(TEXT("Filter:2")) && Screen->SelectedRule() == 0);
	TestFalse(TEXT("**an editor button on it is refused**"), Screen->Press(TEXT("Edit:Action")));
	TestEqual(TEXT("with the built-in sentence"), Screen->RefusalText(), FString(FPrefs::BuiltInRefusal));

	TestTrue(TEXT("pick My filter 1"), Screen->Press(TEXT("Filter:3")));
	Screen->SetTypedNameForTests(TEXT("strict"));
	TestFalse(TEXT("**renaming it \"strict\" is refused**"), Screen->Press(TEXT("Do:Rename")));
	Screen->SetTypedNameForTests(TEXT("Mine"));
	TestTrue(TEXT("renaming it \"Mine\" is done"), Screen->Press(TEXT("Do:Rename")));
	TestEqual(TEXT("**and it stays in use**"), UCataclysmLootFilterStore::ActiveFilter(Screen->Preferences()).Name,
			  FString(TEXT("Mine")));
	TestTrue(TEXT("the refusal line is cleared by a press that works"), Screen->RefusalText().IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
