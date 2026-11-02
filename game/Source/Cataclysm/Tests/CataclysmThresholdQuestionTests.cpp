// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Dungeon/CataclysmFloorObject.h"
#include "Interface/CataclysmChoicePanelWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"

/**
 * Tests for the question asked before an equip takes the Worn Residue to or past this tier's Consumption Threshold.
 * Issue #2687.
 *
 * `docs/Cataclysm_GDD_v2.md`, "The Consumption Threshold, one per difficulty tier": "If equipping an item would take
 * Worn Residue to or past the threshold for the tier being played, the game states the resulting total and the
 * consequence, and asks the player to confirm before the equip happens." The consequence sentence waits for issue
 * #2688; the rest is here.
 *
 * EVERY TEST NAMES ITS TIER, so no test depends on the tier a world without a game mode answers. Tier 1's threshold is
 * 1950 and tier 8's is 5600, the design's table.
 */
namespace CataclysmThresholdQuestionTest
{
	const TCHAR* HelmBase = TEXT("Head_Helm");
	const TCHAR* BootsBase = TEXT("Boots_Sabatons");
	const TCHAR* OneHandedBase = TEXT("Weapon_Sword");
	const TCHAR* OtherOneHandedBase = TEXT("Weapon_Dagger");
	const TCHAR* TwoHandedBase = TEXT("Weapon_Greatsword");

	/** A character's two components, with no actor, as the wearing tests build them. */
	struct FCarriedAndWorn
	{
		FCarriedAndWorn()
			: Inventory(NewObject<UCataclysmInventoryComponent>(GetTransientPackage()))
			, Equipment(NewObject<UCataclysmEquipmentComponent>(GetTransientPackage()))
		{
		}

		int32 Everything() const { return Inventory->NumItems() + Equipment->NumEquipped(); }

		/** Put an item on directly, as a character already wearing it. */
		void Wear(const FCataclysmItem& Item)
		{
			FCataclysmItem First;
			FCataclysmItem Second;
			ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
			Equipment->Equip(Item, First, Second, Went);
		}

		UCataclysmInventoryComponent* Inventory = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	FCataclysmItem Of(const TCHAR* Base, float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.Residue = Residue;
		return Item;
	}

	/**
	 * Whether wearing `Carried` over `Worn` at `Tier` is asked about, read through `WearFromCarried`: true when the
	 * answer is the question, false when it went on. Anything else fails the test by name.
	 */
	bool IsAsked(FAutomationTestBase& Test, const TCHAR* What, const FCataclysmItem& Worn,
				 const FCataclysmItem& Carried, int32 Tier)
	{
		FCarriedAndWorn Character;
		Character.Wear(Worn);
		const int32 Slot = Character.Inventory->AddItem(Carried);
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		const ECataclysmWearResult Result = UCataclysmWearing::WearFromCarried(
			Character.Inventory, Character.Equipment, Slot, Went, /*bConfirmedPastThreshold=*/false, Tier);
		const bool bAsked = Result == ECataclysmWearResult::WouldCrossConsumptionThreshold;
		Test.TestTrue(FString::Printf(TEXT("%s: it is asked about or it goes on"), What),
					  bAsked || Result == ECataclysmWearResult::Worn || Result == ECataclysmWearResult::Swapped);
		Test.AddInfo(FString::Printf(TEXT("THRESHOLD QUESTION: %s: %s"), What,
									 bAsked ? TEXT("asked") : TEXT("not asked")));
		return bAsked;
	}
}

// ---------------------------------------------------------------------------
// The question, and the yes
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmThresholdQuestionAsksBeforeACrossing,
	"Cataclysm.ThresholdQuestion.ACrossingEquipIsAskedAboutAndPutsNothingOnUntilConfirmed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmThresholdQuestionAsksBeforeACrossing::RunTest(const FString& Parameters)
{
	using namespace CataclysmThresholdQuestionTest;

	// A helm of 1000 worn and boots of 1000 carried: 2000 once both are on, past tier 1's 1950.
	FCarriedAndWorn Character;
	Character.Wear(Of(HelmBase, 1000.0f));
	const FCataclysmItem Boots = Of(BootsBase, 1000.0f);
	const int32 Slot = Character.Inventory->AddItem(Boots);
	const int32 Before = Character.Everything();

	// CONTROL: what the question will say, read without wearing anything.
	const FCataclysmThresholdCrossing Crossing =
		UCataclysmWearing::CrossingFor(Character.Inventory, Character.Equipment, Slot, /*DifficultyTier=*/1);
	TestEqual(TEXT("CONTROL: the Worn Residue now is the helm's"), Crossing.Now, 1000.0f);
	TestEqual(TEXT("CONTROL: and with the boots on it would be 2000"), Crossing.After, 2000.0f);
	TestEqual(TEXT("CONTROL: against tier 1's threshold of 1950"), Crossing.Threshold, 1950.0f);
	TestTrue(TEXT("CONTROL: which is a crossing"), Crossing.Crosses());

	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	const ECataclysmWearResult Asked = UCataclysmWearing::WearFromCarried(
		Character.Inventory, Character.Equipment, Slot, Went, /*bConfirmedPastThreshold=*/false, 1);
	TestTrue(TEXT("a crossing equip is asked about"), Asked == ECataclysmWearResult::WouldCrossConsumptionThreshold);
	TestNotNull(TEXT("and the boots are still carried"), Character.Inventory->ItemAt(Slot));
	TestEqual(TEXT("and the Worn Residue is still 1000"), Character.Equipment->WornResidue(), 1000.0f);
	TestEqual(TEXT("and nothing was lost"), Character.Everything(), Before);

	const ECataclysmWearResult Yes = UCataclysmWearing::WearAfterConfirming(
		Character.Inventory, Character.Equipment, Slot, Boots, Went, 1);
	TestTrue(TEXT("a yes puts the boots on"), Yes == ECataclysmWearResult::Worn);
	TestEqual(TEXT("and the Worn Residue is then 2000"), Character.Equipment->WornResidue(), 2000.0f);
	TestEqual(TEXT("and still nothing was lost"), Character.Everything(), Before);

	AddInfo(FString::Printf(TEXT("THRESHOLD QUESTION: now %.0f, after %.0f, threshold %.0f; asked '%s', yes '%s'"),
							Crossing.Now, Crossing.After, Crossing.Threshold,
							*UCataclysmWearing::Explain(Asked), *UCataclysmWearing::Explain(Yes)));
	return true;
}

// ---------------------------------------------------------------------------
// Only a crossing asks
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmThresholdQuestionOnlyACrossingAsks,
	"Cataclysm.ThresholdQuestion.OnlyAnEquipThatCrossesTheThresholdIsAskedAbout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmThresholdQuestionOnlyACrossingAsks::RunTest(const FString& Parameters)
{
	using namespace CataclysmThresholdQuestionTest;

	// Ruled 2026-11-01: only the crossing asks, from below the threshold to at or past it.
	TestFalse(TEXT("below it and still below it: not asked"),
			  IsAsked(*this, TEXT("500 worn, 500 more"), Of(HelmBase, 500.0f), Of(BootsBase, 500.0f), 1));
	TestFalse(TEXT("a swap from past it to below it: not asked"),
			  IsAsked(*this, TEXT("a helm of 2000 swapped for one of 100"), Of(HelmBase, 2000.0f),
					  Of(HelmBase, 100.0f), 1));
	TestFalse(TEXT("already past it and still past it: not asked"),
			  IsAsked(*this, TEXT("2000 worn, 100 more"), Of(HelmBase, 2000.0f), Of(BootsBase, 100.0f), 1));
	TestTrue(TEXT("to exactly the threshold: asked"),
			 IsAsked(*this, TEXT("950 worn, 1000 more"), Of(HelmBase, 950.0f), Of(BootsBase, 1000.0f), 1));
	TestFalse(TEXT("the crossing at tier 1 is not one at tier 8, whose threshold is 5600"),
			  IsAsked(*this, TEXT("1000 worn, 1000 more, tier 8"), Of(HelmBase, 1000.0f), Of(BootsBase, 1000.0f), 8));
	return true;
}

// ---------------------------------------------------------------------------
// The words, what comes off, and a cell that changed
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmThresholdQuestionWordsAndWhatComesOff,
	"Cataclysm.ThresholdQuestion.TheQuestionStatesTheTotalAndAYesAboutOneItemPutsOnNoOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmThresholdQuestionWordsAndWhatComesOff::RunTest(const FString& Parameters)
{
	using namespace CataclysmThresholdQuestionTest;

	// THE WORDS: the total and the threshold, and no consequence yet (issue #2688 adds it).
	FCataclysmThresholdCrossing Crossing;
	Crossing.Now = 1000.0f;
	Crossing.After = 2000.4f;
	Crossing.Threshold = 1950.0f;
	TestEqual(TEXT("the question names the item, the total and the threshold"),
			  UCataclysmWearing::ThresholdQuestion(TEXT("Iron Sabatons"), Crossing),
			  FString(TEXT("Wearing Iron Sabatons brings your Worn Residue to 2000, past this tier's Consumption "
						   "Threshold of 1950.")));
	TestEqual(TEXT("and an item with no name is 'this'"), UCataclysmWearing::ThresholdQuestion(FString(), Crossing),
			  FString(TEXT("Wearing this brings your Worn Residue to 2000, past this tier's Consumption Threshold of "
						   "1950.")));

	// WHAT COMES OFF IS SUBTRACTED: a two-handed weapon over two one-handed ones takes both off.
	{
		FCarriedAndWorn Character;
		Character.Wear(Of(OneHandedBase, 800.0f));
		Character.Wear(Of(OtherOneHandedBase, 800.0f));
		const int32 ChangesBefore = Character.Equipment->ChangeCount();
		TestEqual(TEXT("two one-handed weapons of 800 are worn"), Character.Equipment->WornResidue(), 1600.0f);
		TestEqual(TEXT("a two-handed weapon of 300 over them would leave 300"),
				  Character.Equipment->WornResidueIfEquipped(Of(TwoHandedBase, 300.0f)), 300.0f);
		TestEqual(TEXT("and asking changed nothing worn"), Character.Equipment->WornResidue(), 1600.0f);
		TestEqual(TEXT("nor announced a change"), Character.Equipment->ChangeCount(), ChangesBefore);
	}

	// A YES ABOUT ONE ITEM PUTS ON NO OTHER: the cell changed while the question was open.
	{
		FCarriedAndWorn Character;
		Character.Wear(Of(HelmBase, 1000.0f));
		const FCataclysmItem AskedAbout = Of(BootsBase, 1000.0f);
		const int32 Slot = Character.Inventory->AddItem(AskedAbout);
		Character.Inventory->RemoveItemAt(Slot);
		const int32 Again = Character.Inventory->AddItem(Of(BootsBase, 999.0f));
		TestEqual(TEXT("other boots now sit in the same cell"), Again, Slot);

		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		const ECataclysmWearResult Yes = UCataclysmWearing::WearAfterConfirming(
			Character.Inventory, Character.Equipment, Slot, AskedAbout, Went, 1);
		TestTrue(TEXT("the yes is refused, as no longer there"), Yes == ECataclysmWearResult::NoLongerThere);
		TestEqual(TEXT("and the Worn Residue is still the helm's"), Character.Equipment->WornResidue(), 1000.0f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The choice panel asks it
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmThresholdQuestionOnThePanel,
	"Cataclysm.ThresholdQuestion.AQuestionOnTheChoicePanelAnswersOnlyThroughItsYes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmThresholdQuestionOnThePanel::RunTest(const FString& Parameters)
{
	const FName YesKey(TEXT("WearPastTheThreshold"));
	FCataclysmFloorObjectChoice Yes;
	Yes.Key = YesKey;
	Yes.Label = TEXT("Wear it");

	int32 Calls = 0;
	FName Heard;
	const auto Ask = [&](UCataclysmChoicePanelWidget* Panel)
	{
		Panel->SetQuestion(TEXT("Past the Consumption Threshold"), TEXT("Wearing it brings..."), {Yes},
						   TEXT("Keep it off"), [&Calls, &Heard](FName Key)
						   {
							   ++Calls;
							   Heard = Key;
						   });
	};

	// NO BLUEPRINT IN A TEST, so the panel has no buttons; its keys and words are what the player would see.
	UCataclysmChoicePanelWidget* Panel = NewObject<UCataclysmChoicePanelWidget>();
	Ask(Panel);
	TestTrue(TEXT("the panel is asking"), Panel->IsAskingAQuestion());
	TestEqual(TEXT("its title"), Panel->ShownTitle(), FString(TEXT("Past the Consumption Threshold")));
	TestEqual(TEXT("its prompt"), Panel->ShownPrompt(), FString(TEXT("Wearing it brings...")));
	if (TestEqual(TEXT("two buttons: the yes, and the one that chooses nothing"), Panel->ChoiceKeys().Num(), 2))
	{
		TestEqual(TEXT("the yes first"), Panel->ChoiceKeys()[0], YesKey);
		TestEqual(TEXT("then the one that chooses nothing"), Panel->ChoiceKeys()[1],
				  UCataclysmChoicePanelWidget::LeaveKey);
	}

	Panel->ChooseForTests(UCataclysmChoicePanelWidget::LeaveKey);
	TestEqual(TEXT("'Keep it off' calls nothing"), Calls, 0);
	TestFalse(TEXT("and ends the question"), Panel->IsAskingAQuestion());

	Ask(Panel);
	Panel->ChooseForTests(YesKey);
	TestEqual(TEXT("'Wear it' calls the answer once"), Calls, 1);
	TestEqual(TEXT("with its key"), Heard, YesKey);
	TestTrue(TEXT("and the panel says it acted"), Panel->LastChoiceActed());
	TestFalse(TEXT("and the question is over"), Panel->IsAskingAQuestion());

	Ask(Panel);
	Panel->SetObject(nullptr);
	TestFalse(TEXT("showing a floor object ends a question"), Panel->IsAskingAQuestion());
	TestEqual(TEXT("and leaves the object's words, here none"), Panel->ShownTitle(), FString());
	Panel->ChooseForTests(YesKey);
	TestEqual(TEXT("so a press no longer reaches the question's answer"), Calls, 1);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
