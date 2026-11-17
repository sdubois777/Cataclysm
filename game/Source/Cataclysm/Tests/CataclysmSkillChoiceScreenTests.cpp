// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmSkillAssignmentWidget.h"
#include "Interface/CataclysmSkillBar.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * The skill-choice screen, `UCataclysmSkillAssignmentWidget`. Issue #837 layer 4, ruled 2026-10-25.
 *
 * THE RULINGS. Q1, K and `Cataclysm.ChooseSkills`; Q2, no pause; Q3, a skill chosen for another key is refused, as
 * layer 3's O4 rules; Q4, a kept choice is shown beside the default; Q5, refusals name skills, and the screen's own
 * "Click a slot first, then a skill."; Q7, a four-line tool tip with no numbers; Q8, the six keys in the bar's order
 * and "Basic attack: from the weapon"; Q9, a redraw when the worn weapons change.
 *
 * HEADLESS, ON THE MODEL. The automation command passes `-nullrhi` and a `NewObject` widget has no Widget Blueprint,
 * so no panel and no button of the screen's exists here. Each test drives the public functions a click calls and
 * reads the rows they produce; a tool tip is read off a button the test made, through `DescribeButtonForTests`.
 * Whether the screen is legible, whether a hover shows the pop-up, and a real click are not tested.
 *
 * A POSSESSED PLAYER WEARING THE GREATAXE IT BEGINS WITH, which carries Demonic: its pool is Molten Cleave, Emberhurl,
 * Burning Wrath, Conflagration, Pyroclasm and Infernal Plunge. The helpers are copies of layer 3's, in a namespace of
 * this file's own.
 */
namespace CataclysmSkillChoiceScreenTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	const FName ScreenPyroclasmRow(TEXT("Demonic_Greataxe_Ultimate"));
	const FName ScreenMoltenCleaveRow(TEXT("Demonic_Greataxe_Heavy"));
	const FName ScreenEmberhurlRow(TEXT("Demonic_Greataxe_Special"));

	/** The parts of one possessed player a test reads, and the screen made on it. */
	struct FSkillChoiceScreenCarrier
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
		UCataclysmSkillAssignmentWidget* Screen = nullptr;

		bool IsComplete() const { return Character && State && Equipment && Slots && Screen; }
	};

	/**
	 * A possessed player wearing its starting Greataxe, carrying Demonic, and the screen shown for it, refreshed. One
	 * set-up check.
	 */
	FSkillChoiceScreenCarrier SpawnScreenCarrier(FAutomationTestBase& Test, UWorld* World)
	{
		FSkillChoiceScreenCarrier Made;
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		Made.Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (PlayerState && Controller && Made.Character)
		{
			Controller->SetPlayerState(PlayerState);
			Controller->Possess(Made.Character);
		}
		Made.State = Made.Character ? Made.Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		Made.Equipment = Made.Character ? Made.Character->GetEquipment() : nullptr;
		Made.Slots = Made.Character ? Made.Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
		if (Controller && Made.State)
		{
			Made.Screen = NewObject<UCataclysmSkillAssignmentWidget>(Controller);
			Made.Screen->SetPlayerStateForTests(Made.State);
			Made.Screen->Refresh();
		}
		if (!Test.TestTrue(TEXT("set-up: a possessed player with its state, equipment and weapon slots, and the screen"),
						   Made.IsComplete()))
		{
			return FSkillChoiceScreenCarrier();
		}
		return Made;
	}

	/** A weapon of this base carrying these types and nothing else. */
	FCataclysmItem ScreenWeapon(const TCHAR* Base, const TArray<FName>& Types)
	{
		FCataclysmItem Made;
		Made.Base = FName(Base);
		Made.GearLevel = 0;
		Made.DamageTypes = Types;
		return Made;
	}

	/** Put a weapon on in Weapon1 through the component's own equip, which announces and so refills. One check. */
	bool WearForTheScreen(FAutomationTestBase& Test, const FSkillChoiceScreenCarrier& Carrier, const FCataclysmItem& Item)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		const ECataclysmEquipResult Result =
			Carrier.Equipment->EquipInto(Item, ECataclysmGearSlot::Weapon1, Removed, AlsoRemoved);
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s goes on"), *Item.Base.ToString()),
							 Result == ECataclysmEquipResult::Equipped || Result == ECataclysmEquipResult::Swapped);
	}

	/** The screen's row for a key, as its last redraw read it, or null. Found again after every redraw. */
	const FCataclysmSkillScreenRow* ScreenRowFor(const UCataclysmSkillAssignmentWidget* Screen, ECataclysmAbilitySlot Key)
	{
		return Screen->SlotRows().FindByPredicate([Key](const FCataclysmSkillScreenRow& Row) { return Row.Key == Key; });
	}

	/** The row held on a key as the screen shows it, or None. */
	FName ScreenRowOn(const UCataclysmSkillAssignmentWidget* Screen, ECataclysmAbilitySlot Key)
	{
		const FCataclysmSkillScreenRow* Row = ScreenRowFor(Screen, Key);
		return Row ? Row->HeldRow : NAME_None;
	}

	/** Whether the screen's last redraw has this row in its pool. */
	bool PoolShows(const UCataclysmSkillAssignmentWidget* Screen, FName Row)
	{
		return Screen->PoolRows().ContainsByPredicate(
			[Row](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == Row; });
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillChoiceScreenRowsTest,
	"Cataclysm.SkillChoiceScreen.TheSixSlotRowsShowWhatEachKeyHoldsInTheBarsOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q8: six rows, one per key, in `UCataclysmSkillBar::SlotsShown` order, each naming the skill on its key and saying
 * "default" while nothing is chosen; the basic attack is a line above them, not a row. The control: nothing is chosen.
 */
bool FCataclysmSkillChoiceScreenRowsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChoiceScreenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillChoiceScreenCarrier Carrier = SpawnScreenCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	TestEqual(TEXT("control: nothing is chosen"), Carrier.State->GetSkillAssignment().Num(), 0);
	const TArray<FCataclysmSkillScreenRow>& Rows = Carrier.Screen->SlotRows();
	TestEqual(TEXT("six rows, one per key"), Rows.Num(), 6);

	const TArray<ECataclysmAbilitySlot> BarOrder = UCataclysmSkillBar::SlotsShown();
	const FString Expected = FString::JoinBy(BarOrder, TEXT(", "),
		[](ECataclysmAbilitySlot Key) { return ACataclysmPlayerState::SkillSlotName(Key); });
	const FString Shown = FString::JoinBy(Rows, TEXT(", "),
		[](const FCataclysmSkillScreenRow& Row) { return ACataclysmPlayerState::SkillSlotName(Row.Key); });
	TestEqual(TEXT("in the skill bar's order"), Shown, Expected);

	TestEqual(TEXT("each naming the skill on its key"),
			  FString::JoinBy(Rows, TEXT(", "), [](const FCataclysmSkillScreenRow& Row) { return Row.HeldName; }),
			  FString(TEXT("Molten Cleave, Emberhurl, Burning Wrath, Conflagration, Pyroclasm, Infernal Plunge")));

	int32 Default = 0;
	for (const FCataclysmSkillScreenRow& Row : Rows)
	{
		Default += !Row.bChosen && Row.AsLine().EndsWith(TEXT("(default)")) ? 1 : 0;
	}
	TestEqual(TEXT("and each saying it is the key's default"), Default, 6);

	TestTrue(TEXT("the basic attack is a line above them, from the weapon, and not a row"),
			 FString(UCataclysmSkillAssignmentWidget::BasicAttackLine) == TEXT("Basic attack: from the weapon")
				 && ScreenRowFor(Carrier.Screen, ECataclysmAbilitySlot::BasicAttack) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillChoiceScreenToolTipTest,
	"Cataclysm.SkillChoiceScreen.APoolSkillsToolTipNamesItsSlotWeaponTypeDamageTypeAndDescription",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q7: four lines and no numbers. Pyroclasm's tool tip names it, the slot it was made for, its weapon type and damage
 * type, and its description; on another key it says which. The control: Molten Cleave's tool tip does not carry
 * Pyroclasm's description. A pool button the test made carries the same text as its tool tip.
 */
bool FCataclysmSkillChoiceScreenToolTipTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChoiceScreenTest;

	const TArray<FCataclysmWeaponSkill> Pool = UCataclysmWeaponSkills::PoolFor(
		UCataclysmWeaponSkills::LoadGeneratedTable(), {TEXT("Greataxe")}, {TEXT("Demonic")});
	const FCataclysmWeaponSkill* Pyroclasm =
		Pool.FindByPredicate([](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == ScreenPyroclasmRow; });
	const FCataclysmWeaponSkill* Cleave =
		Pool.FindByPredicate([](const FCataclysmWeaponSkill& Skill) { return Skill.RowName == ScreenMoltenCleaveRow; });
	if (!TestTrue(TEXT("set-up: the table gives Pyroclasm and Molten Cleave, each with a description"),
				  Pyroclasm && Cleave && !Pyroclasm->Description.IsEmpty() && !Cleave->Description.IsEmpty()))
	{
		return false;
	}

	const FString Tip = UCataclysmSkillAssignmentWidget::ToolTipFor(*Pyroclasm, ECataclysmAbilitySlot::Ultimate);
	TestTrue(TEXT("Pyroclasm's tool tip names it"), Tip.StartsWith(TEXT("Pyroclasm\n")));
	TestTrue(TEXT("names the slot it was made for"), Tip.Contains(TEXT("\nMade for the Ultimate slot\n")));
	TestTrue(TEXT("its weapon type and damage type"), Tip.Contains(TEXT("\nGreataxe \u00B7 Demonic\n")));
	TestTrue(TEXT("and its description"), Tip.EndsWith(*Pyroclasm->Description));
	TestEqual(TEXT("in four lines"), Tip.Len() - Tip.Replace(TEXT("\n"), TEXT("")).Len(), 3);
	TestFalse(TEXT("control: Molten Cleave's tool tip does not carry Pyroclasm's description"),
			  UCataclysmSkillAssignmentWidget::ToolTipFor(*Cleave, ECataclysmAbilitySlot::Heavy)
				  .Contains(Pyroclasm->Description));
	TestTrue(TEXT("on another key it says which"),
			 UCataclysmSkillAssignmentWidget::ToolTipFor(*Pyroclasm, ECataclysmAbilitySlot::Heavy)
				 .Contains(TEXT("\nMade for the Ultimate slot, on the Heavy key\n")));

	// A POOL BUTTON THE TEST MADE, described as the screen's own panel describes one.
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillChoiceScreenCarrier Carrier = SpawnScreenCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	UCataclysmChoiceButton* Button = NewObject<UCataclysmChoiceButton>();
	Carrier.Screen->DescribeButtonForTests(*Button, ScreenPyroclasmRow, /*bIsSlot=*/false);
	TestEqual(TEXT("a pool button carries that tool tip"), Button->GetToolTipText().ToString(), Tip);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillChoiceScreenPickTest,
	"Cataclysm.SkillChoiceScreen.ClickingASlotThenASkillPutsItOnThatKeyAndTheBarShowsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Click the Special key, then Pyroclasm: the player state records Pyroclasm on the Special key, the screen's Special
 * row shows it chosen, the Ultimate key it left is empty (layer 3's O4), and the skill bar's Special box names it. The
 * control, read first: the Special key holds Emberhurl.
 */
bool FCataclysmSkillChoiceScreenPickTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChoiceScreenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillChoiceScreenCarrier Carrier = SpawnScreenCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	TestEqual(TEXT("control: the Special key holds Emberhurl"),
			  ScreenRowOn(Carrier.Screen, ECataclysmAbilitySlot::Special), ScreenEmberhurlRow);
	if (!TestTrue(TEXT("set-up: the Special key is selected"), Carrier.Screen->SelectSlot(ECataclysmAbilitySlot::Special)))
	{
		return false;
	}
	TestTrue(TEXT("the status line says the Special slot is selected"),
			 Carrier.Screen->StatusText().Contains(TEXT("The Special slot is selected.")));
	TestTrue(TEXT("clicking Pyroclasm chooses it"), Carrier.Screen->PickSkill(ScreenPyroclasmRow));
	TestEqual(TEXT("with no refusal"), Carrier.Screen->RefusalText(), FString());

	const TArray<FCataclysmSkillChoice>& Choices = Carrier.State->GetSkillAssignment();
	TestTrue(TEXT("the player state records Pyroclasm on the Special key"),
			 Choices.Num() == 1 && Choices[0].Slot == ECataclysmAbilitySlot::Special
				 && Choices[0].SkillRow == ScreenPyroclasmRow);
	const FCataclysmSkillScreenRow* Special = ScreenRowFor(Carrier.Screen, ECataclysmAbilitySlot::Special);
	TestTrue(TEXT("the screen's Special row shows Pyroclasm, chosen"),
			 Special && Special->HeldRow == ScreenPyroclasmRow && Special->bChosen);
	TestEqual(TEXT("the Ultimate key it left is empty (O4)"),
			  ScreenRowOn(Carrier.Screen, ECataclysmAbilitySlot::Ultimate), FName(NAME_None));

	const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Carrier.Character);
	const FCataclysmSkillBarSlot* SpecialBox =
		Bar.FindByPredicate([](const FCataclysmSkillBarSlot& Box) { return Box.Slot == ECataclysmAbilitySlot::Special; });
	TestTrue(TEXT("and the skill bar's Special box names Pyroclasm"), SpecialBox && SpecialBox->Name == TEXT("Pyroclasm"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillChoiceScreenNoSlotTest,
	"Cataclysm.SkillChoiceScreen.ASkillPickedWithNoSlotSelectedIsRefusedAndChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q5: a skill clicked with no key selected is refused with the screen's own sentence, "Click a slot first, then a
 * skill.", and nothing is recorded. The control: with the Heavy key selected, the same click works.
 */
bool FCataclysmSkillChoiceScreenNoSlotTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChoiceScreenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillChoiceScreenCarrier Carrier = SpawnScreenCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	TestFalse(TEXT("a skill clicked with no slot selected is refused"), Carrier.Screen->PickSkill(ScreenPyroclasmRow));
	TestEqual(TEXT("with the screen's own sentence"), Carrier.Screen->RefusalText(),
			  FString(TEXT("Click a slot first, then a skill.")));
	TestEqual(TEXT("and nothing is recorded"), Carrier.State->GetSkillAssignment().Num(), 0);

	TestTrue(TEXT("control: with the Heavy key selected, the same click works"),
			 Carrier.Screen->SelectSlot(ECataclysmAbilitySlot::Heavy) && Carrier.Screen->PickSkill(ScreenPyroclasmRow));
	TestEqual(TEXT("control: and the refusal is cleared"), Carrier.Screen->RefusalText(), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillChoiceScreenRefusalTest,
	"Cataclysm.SkillChoiceScreen.ASkillChosenForAnotherKeyIsRefusedAndTheReasonIsShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q3 and Q5: Pyroclasm chosen for the Heavy key and then clicked for the Special key is refused, not swapped, and the
 * screen shows the player state's sentence, which names the skill and not its row. The control: a pick that is allowed
 * clears the refusal.
 */
bool FCataclysmSkillChoiceScreenRefusalTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChoiceScreenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillChoiceScreenCarrier Carrier = SpawnScreenCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key through the screen"),
					 Carrier.Screen->SelectSlot(ECataclysmAbilitySlot::Heavy)
						 && Carrier.Screen->PickSkill(ScreenPyroclasmRow)))
	{
		return false;
	}

	Carrier.Screen->SelectSlot(ECataclysmAbilitySlot::Special);
	TestFalse(TEXT("clicking Pyroclasm for the Special key is refused"), Carrier.Screen->PickSkill(ScreenPyroclasmRow));
	TestEqual(TEXT("and the screen shows why, by the skill's name"), Carrier.Screen->RefusalText(),
			  FString(TEXT("Pyroclasm is already chosen for the Heavy slot. Clear that slot first.")));
	const TArray<FCataclysmSkillChoice>& Choices = Carrier.State->GetSkillAssignment();
	TestTrue(TEXT("the choice is unchanged: Pyroclasm on the Heavy key, and nothing else"),
			 Choices.Num() == 1 && Choices[0].Slot == ECataclysmAbilitySlot::Heavy);
	TestEqual(TEXT("and the Special key still holds Emberhurl"),
			  ScreenRowOn(Carrier.Screen, ECataclysmAbilitySlot::Special), ScreenEmberhurlRow);

	TestTrue(TEXT("control: Molten Cleave, held nowhere now, may be chosen for the Special key"),
			 Carrier.Screen->PickSkill(ScreenMoltenCleaveRow));
	TestEqual(TEXT("control: and that clears the refusal"), Carrier.Screen->RefusalText(), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillChoiceScreenClearTest,
	"Cataclysm.SkillChoiceScreen.ClearTheSlotForgetsTheChoiceAndTheKeyReturnsToItsDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Clear the slot" (the owner's Q5 and O5): the selected key's choice is forgotten and the key returns to its default,
 * and the skill it held goes back to its own key. The control, read first: with nothing chosen there is nothing to
 * clear.
 */
bool FCataclysmSkillChoiceScreenClearTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChoiceScreenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillChoiceScreenCarrier Carrier = SpawnScreenCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !TestTrue(TEXT("set-up: the Heavy key is selected"), Carrier.Screen->SelectSlot(ECataclysmAbilitySlot::Heavy)))
	{
		return false;
	}

	TestFalse(TEXT("control: with nothing chosen, the clear control is not enabled"),
			  Carrier.Screen->CanClearSelectedSlot());
	TestFalse(TEXT("control: and clearing forgets nothing"), Carrier.Screen->ClearSelectedSlot());

	if (!TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key"), Carrier.Screen->PickSkill(ScreenPyroclasmRow)))
	{
		return false;
	}
	TestTrue(TEXT("with a choice, the clear control is enabled"), Carrier.Screen->CanClearSelectedSlot());
	TestTrue(TEXT("clearing forgets the choice"), Carrier.Screen->ClearSelectedSlot());
	TestEqual(TEXT("nothing is chosen now"), Carrier.State->GetSkillAssignment().Num(), 0);
	const FCataclysmSkillScreenRow* Heavy = ScreenRowFor(Carrier.Screen, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("the Heavy row shows Molten Cleave, its default"),
			 Heavy && Heavy->HeldRow == ScreenMoltenCleaveRow && !Heavy->bChosen);
	TestEqual(TEXT("and Pyroclasm is back on the Ultimate key"),
			  ScreenRowOn(Carrier.Screen, ECataclysmAbilitySlot::Ultimate), ScreenPyroclasmRow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillChoiceScreenKeptTest,
	"Cataclysm.SkillChoiceScreen.AKeptChoiceWhoseWeaponIsOffIsShownBesideTheDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q4: Pyroclasm chosen for the Heavy key, and a War Sword put on in place of the Greataxe. The Heavy row shows the
 * Sword's Rending Strike as its default, with the kept choice beside it in the plan's words; Pyroclasm is not in the
 * pool; the choice is still saved. The Greataxe back, the row shows Pyroclasm chosen and nothing kept. The control,
 * read first: with its weapon worn, the choice is on its key and nothing is kept.
 */
bool FCataclysmSkillChoiceScreenKeptTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChoiceScreenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillChoiceScreenCarrier Carrier = SpawnScreenCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key through the screen"),
					 Carrier.Screen->SelectSlot(ECataclysmAbilitySlot::Heavy)
						 && Carrier.Screen->PickSkill(ScreenPyroclasmRow)))
	{
		return false;
	}
	const FCataclysmSkillScreenRow* Before = ScreenRowFor(Carrier.Screen, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("control: with the Greataxe worn, the Heavy row holds Pyroclasm, chosen, and keeps nothing"),
			 Before && Before->HeldRow == ScreenPyroclasmRow && Before->bChosen && Before->KeptSkillRow.IsNone());

	if (!WearForTheScreen(*this, Carrier, ScreenWeapon(TEXT("Weapon_Sword"), {FName(TEXT("War"))}))
		|| !TestTrue(TEXT("set-up: the screen redraws for the change of weapon"), Carrier.Screen->RefreshIfGearChanged()))
	{
		return false;
	}
	const FCataclysmSkillScreenRow* Kept = ScreenRowFor(Carrier.Screen, ECataclysmAbilitySlot::Heavy);
	if (!TestNotNull(TEXT("set-up: the screen has a Heavy row"), Kept))
	{
		return false;
	}
	TestTrue(TEXT("the Heavy row shows the Sword's Rending Strike as its default"),
			 Kept->HeldRow == FName(TEXT("War_Sword_Heavy")) && Kept->HeldName == TEXT("Rending Strike") && !Kept->bChosen);
	TestEqual(TEXT("the row keeps Pyroclasm's choice"), Kept->KeptSkillRow, ScreenPyroclasmRow);
	TestEqual(TEXT("and says so in the plan's words"), Kept->KeptText,
			  FString(TEXT("Kept: Pyroclasm (Greataxe \u00B7 Demonic). It returns when that weapon is worn.")));
	TestFalse(TEXT("Pyroclasm is not in the pool"), PoolShows(Carrier.Screen, ScreenPyroclasmRow));
	TestTrue(TEXT("and the choice is still saved"), Carrier.State->GetSkillAssignment().Num() == 1
				 && Carrier.State->GetSkillAssignment()[0].SkillRow == ScreenPyroclasmRow);

	if (!WearForTheScreen(*this, Carrier, ScreenWeapon(TEXT("Weapon_Greataxe"), {FName(TEXT("Demonic"))})))
	{
		return false;
	}
	Carrier.Screen->RefreshIfGearChanged();
	const FCataclysmSkillScreenRow* Back = ScreenRowFor(Carrier.Screen, ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("the Greataxe back, the Heavy row holds Pyroclasm, chosen, and keeps nothing"),
			 Back && Back->HeldRow == ScreenPyroclasmRow && Back->bChosen && Back->KeptSkillRow.IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillChoiceScreenRedrawTest,
	"Cataclysm.SkillChoiceScreen.TheScreenRedrawsWhenTheWornWeaponsChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q9: the screen redraws when the worn weapons change, by the equipment's change count, and only then. The control,
 * read first: with nothing changed it redraws nothing, and its pool has no War skill.
 */
bool FCataclysmSkillChoiceScreenRedrawTest::RunTest(const FString&)
{
	using namespace CataclysmSkillChoiceScreenTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillChoiceScreenCarrier Carrier = SpawnScreenCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	TestFalse(TEXT("control: with nothing changed, the screen redraws nothing"), Carrier.Screen->RefreshIfGearChanged());
	TestFalse(TEXT("control: and its pool has no War Sword skill"),
			  PoolShows(Carrier.Screen, FName(TEXT("War_Sword_Heavy"))));

	if (!WearForTheScreen(*this, Carrier, ScreenWeapon(TEXT("Weapon_Sword"), {FName(TEXT("War"))})))
	{
		return false;
	}
	TestTrue(TEXT("a change of weapon redraws it"), Carrier.Screen->RefreshIfGearChanged());
	TestTrue(TEXT("and the pool it shows is the Sword's"), PoolShows(Carrier.Screen, FName(TEXT("War_Sword_Heavy"))));
	TestFalse(TEXT("and with nothing changed since, it redraws nothing again"), Carrier.Screen->RefreshIfGearChanged());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
