// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillSlots.h"
#include "Interface/CataclysmHUD.h"
#include "Interface/CataclysmSkillBar.h"

/**
 * Tests for the player's skill bar, issue #49.
 *
 * WHAT THEY CAN AND CANNOT REACH, AND IT IS THE SAME WALL THE REST OF THE
 * HEADS-UP DISPLAY HAS. `ACataclysmHUD::DrawHUD` never runs under test: the
 * automation command in `tools/unreal_build.py` passes `-nullrhi`, so there is
 * no canvas to draw on. Everything that DECIDES what the bar shows therefore
 * lives in `UCataclysmSkillBar` and is checked here; the drawing itself is not
 * covered by anything and cannot be.
 *
 * WHAT THAT LEAVES UNCOVERED, said plainly: that the rectangles land where these
 * numbers say, and that they are legible. A person has to look at that.
 */

// ---------------------------------------------------------------------------
// Which slots get a box
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarSlotsTest,
	"Cataclysm.SkillBar.EverySlotThePlayerPressesGetsABoxAndNothingElseDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarSlotsTest::RunTest(const FString& Parameters)
{
	const TArray<ECataclysmAbilitySlot> Shown = UCataclysmSkillBar::SlotsShown();

	TestEqual(TEXT("six slots get a box"), Shown.Num(), 6);

	// THE BASIC ATTACK IS THE ONE LEFT OUT, and it is left out for a reason the
	// ability system itself agrees with: it has no cooldown tag, because the
	// design makes it automatic and attack speed sets its rate. A box for it
	// would show a key that does not exist over a wait that never happens.
	TestFalse(TEXT("the automatic basic attack does not get a box"),
			  Shown.Contains(ECataclysmAbilitySlot::BasicAttack));

	TestFalse(TEXT("and neither does None"),
			  Shown.Contains(ECataclysmAbilitySlot::None));

	// AND EVERY OTHER SLOT DOES. This is the half that notices a slot being
	// added to the game and forgotten here, which would be a key the player can
	// press with nothing on screen for it.
	for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
	{
		if (Slot == ECataclysmAbilitySlot::BasicAttack)
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("slot %d gets a box"),
				 static_cast<int32>(Slot)), Shown.Contains(Slot));
	}

	// NO SLOT GETS TWO BOXES.
	TSet<ECataclysmAbilitySlot> Seen;
	for (const ECataclysmAbilitySlot Slot : Shown)
	{
		bool bAlready = false;
		Seen.Add(Slot, &bAlready);
		TestFalse(FString::Printf(TEXT("slot %d appears once"),
				  static_cast<int32>(Slot)), bAlready);
	}

	// EVERY SLOT SHOWN HAS A NAME TO WRITE IN ITS BOX, even before anything is
	// granted into it. An unnamed empty box says nothing about why it is empty.
	for (const ECataclysmAbilitySlot Slot : Shown)
	{
		TestFalse(FString::Printf(TEXT("slot %d has a name"),
				  static_cast<int32>(Slot)),
				  UCataclysmSkillBar::NameForEmptySlot(Slot).IsEmpty());
	}

	return true;
}

// ---------------------------------------------------------------------------
// Where the boxes go
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarLayoutTest,
	"Cataclysm.SkillBar.TheBarIsCentredAlongTheBottomAndItsBoxesDoNotOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarLayoutTest::RunTest(const FString& Parameters)
{
	constexpr float Width = 1920.0f;
	constexpr float Height = 1080.0f;

	const int32 Count = UCataclysmSkillBar::SlotsShown().Num();
	const float Size = UCataclysmSkillBar::BoxSizePx;

	const FVector2D First = UCataclysmSkillBar::BoxOriginFor(0, Count, Width, Height);
	const FVector2D Last =
		UCataclysmSkillBar::BoxOriginFor(Count - 1, Count, Width, Height);

	// CENTRED: the gap to the left edge equals the gap to the right edge.
	//
	// DOUBLES, BECAUSE `FVector2D` HOLDS DOUBLES in Unreal 5. Narrowing them to
	// float here would compile with a warning and compare something slightly
	// different from what the drawing uses.
	const double LeftGap = First.X;
	const double RightGap = Width - (Last.X + Size);

	TestTrue(FString::Printf(
		TEXT("the bar is centred: %.1f px to the left, %.1f to the right"),
		LeftGap, RightGap),
		FMath::IsNearlyEqual(LeftGap, RightGap, 0.01));

	// ALONG THE BOTTOM, and inside the screen.
	TestTrue(FString::Printf(TEXT("the bar sits above the bottom edge: %.1f"),
			 Last.Y + Size), Last.Y + Size < Height);
	TestTrue(TEXT("and below the middle of the screen"), First.Y > Height * 0.5f);
	TestTrue(TEXT("and its left edge is on screen"), First.X > 0.0f);

	// THE BOXES DO NOT OVERLAP, and they are in order.
	for (int32 Index = 1; Index < Count; ++Index)
	{
		const FVector2D Before =
			UCataclysmSkillBar::BoxOriginFor(Index - 1, Count, Width, Height);
		const FVector2D Here =
			UCataclysmSkillBar::BoxOriginFor(Index, Count, Width, Height);

		TestTrue(FString::Printf(TEXT("box %d starts after box %d ends"),
				 Index, Index - 1), Here.X >= Before.X + Size);
		TestTrue(FString::Printf(TEXT("box %d is on the same row"), Index),
				 FMath::IsNearlyEqual(Here.Y, Before.Y, 0.01));
	}

	// THE WIDTH IS THE BOXES PLUS THE GAPS BETWEEN THEM, and no gap after the
	// last one. Worked out here rather than read from the function, so this is a
	// second opinion rather than a restatement.
	const float Expected = Count * UCataclysmSkillBar::BoxSizePx
		+ (Count - 1) * UCataclysmSkillBar::BoxGapPx;

	TestEqual(TEXT("the bar is as wide as its boxes and gaps"),
			  UCataclysmSkillBar::BarWidthFor(Count), Expected, 0.01f);
	TestEqual(TEXT("a bar of no boxes has no width"),
			  UCataclysmSkillBar::BarWidthFor(0), 0.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarClearOfVitalsTest,
	"Cataclysm.SkillBar.TheBarDoesNotRunIntoTheHealthAndManaBars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarClearOfVitalsTest::RunTest(const FString& Parameters)
{
	// THE FAULT THIS RULES OUT IS TWO PIECES OF INTERFACE DRAWN ON TOP OF EACH
	// OTHER. The player's health, mana and energy shield stack upward from the
	// bottom LEFT corner and the skill bar is centred along the bottom, so on a
	// wide screen they are far apart -- and on a narrow one they are not
	// obviously so. Both are drawn every frame with no test between them.
	const int32 Count = UCataclysmSkillBar::SlotsShown().Num();
	const float Width = UCataclysmSkillBar::NarrowestCheckedViewportPx;

	const FVector2D First = UCataclysmSkillBar::BoxOriginFor(0, Count, Width, 768.0f);

	// WHERE THE PLAYER'S BARS END, worked out from the heads-up display's own
	// numbers rather than copied: they start `PlayerBarMarginPx` in from the left
	// and are `PlayerBarWidthPx` wide.
	const float VitalsRightEdge = ACataclysmHUD::PlayerBarMarginPx
		+ ACataclysmHUD::PlayerBarWidthPx;

	TestTrue(FString::Printf(
		TEXT("on a %.0f pixel screen the bar starts at %.1f, clear of the "
			 "player's bars which end at %.1f"),
		Width, First.X, VitalsRightEdge),
		First.X > VitalsRightEdge);

	return true;
}

// ---------------------------------------------------------------------------
// What a box says
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarCooldownTest,
	"Cataclysm.SkillBar.TheWaitOnASkillIsShownAsItRunsDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarCooldownTest::RunTest(const FString& Parameters)
{
	// THE SWEEP DRAINS FROM FULL TO EMPTY.
	TestEqual(TEXT("a wait that has just started covers the whole box"),
			  UCataclysmSkillBar::CooldownFractionFor(8.0f, 8.0f), 1.0f, 0.001f);
	TestEqual(TEXT("halfway through, half the box"),
			  UCataclysmSkillBar::CooldownFractionFor(4.0f, 8.0f), 0.5f, 0.001f);
	TestEqual(TEXT("a skill that is ready covers none of it"),
			  UCataclysmSkillBar::CooldownFractionFor(0.0f, 8.0f), 0.0f, 0.001f);

	// A SKILL WITH NO COOLDOWN AT ALL IS NOT A SKILL WAITING FOR EVER. The Aura
	// is a toggle and has no cooldown tag, so it reports a duration of zero, and
	// dividing by that would cover its box permanently.
	TestEqual(TEXT("a wait with no length covers none of the box"),
			  UCataclysmSkillBar::CooldownFractionFor(5.0f, 0.0f), 0.0f, 0.001f);
	TestEqual(TEXT("and neither does a negative one"),
			  UCataclysmSkillBar::CooldownFractionFor(5.0f, -3.0f), 0.0f, 0.001f);

	// A REMAINING TIME LONGER THAN THE DURATION IS CLAMPED rather than drawn
	// past the top of the box.
	TestEqual(TEXT("more remaining than the wait was long still covers one box"),
			  UCataclysmSkillBar::CooldownFractionFor(20.0f, 8.0f), 1.0f, 0.001f);

	// THE NUMBER: tenths below ten seconds, whole seconds above.
	TestEqual(TEXT("a skill that is ready shows no number at all"),
			  UCataclysmSkillBar::CooldownTextFor(0.0f), FString());
	TestEqual(TEXT("and neither does a negative wait"),
			  UCataclysmSkillBar::CooldownTextFor(-2.0f), FString());
	TestEqual(TEXT("a short wait is shown to a tenth"),
			  UCataclysmSkillBar::CooldownTextFor(4.24f), FString(TEXT("4.2")));
	TestEqual(TEXT("just under a second still shows something"),
			  UCataclysmSkillBar::CooldownTextFor(0.4f), FString(TEXT("0.4")));

	// ROUNDED UP ABOVE TEN SECONDS. Rounding down would show "12" for a wait of
	// 12.9 seconds and "12" again a second later, so the number would appear to
	// stall, and it would read "0" for the last whole second of the wait.
	TestEqual(TEXT("a long wait is shown in whole seconds, rounded up"),
			  UCataclysmSkillBar::CooldownTextFor(12.1f), FString(TEXT("13")));
	TestEqual(TEXT("exactly ten seconds is a whole number"),
			  UCataclysmSkillBar::CooldownTextFor(10.0f), FString(TEXT("10")));

	// AND THE NUMBER FITS THE BOX AT EVERY LENGTH A COOLDOWN CAN BE. The longest
	// designed wait is far under a thousand seconds, so four characters is the
	// most this can ever produce.
	for (float Seconds = 0.1f; Seconds < 300.0f; Seconds += 0.7f)
	{
		const FString Text = UCataclysmSkillBar::CooldownTextFor(Seconds);
		if (!TestTrue(FString::Printf(
			TEXT("a wait of %.1f seconds reads as \"%s\", which fits the box"),
			Seconds, *Text), Text.Len() <= 4))
		{
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarAffordTest,
	"Cataclysm.SkillBar.ASkillTheCharacterCannotPayForIsMarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarAffordTest::RunTest(const FString& Parameters)
{
	// THIS IS THE ONE THAT ALREADY COST SOMEBODY AN EVENING. Issue #653 was
	// reported as "sometimes all of my abilities just become disabled", and it
	// was an empty mana pool with nothing on screen saying so.
	TestTrue(TEXT("plenty of mana pays a small cost"),
			 UCataclysmSkillBar::CanAfford(10.0f, 100.0f));
	TestFalse(TEXT("too little mana does not"),
			  UCataclysmSkillBar::CanAfford(10.0f, 9.0f));

	// EXACTLY ENOUGH IS ENOUGH. A bar that greyed out a skill the character could
	// actually cast would be worse than no bar at all.
	TestTrue(TEXT("exactly enough mana pays"),
			 UCataclysmSkillBar::CanAfford(10.0f, 10.0f));

	// A FREE SKILL IS ALWAYS PAYABLE, including on an empty pool. The Movement
	// slot's designed cost is zero.
	TestTrue(TEXT("a skill that costs nothing is payable with nothing"),
			 UCataclysmSkillBar::CanAfford(0.0f, 0.0f));

	// AND THE COLOUR FOLLOWS IT. Three states, three different colours: an empty
	// slot, a slot that cannot be paid for, and one that can.
	FCataclysmSkillBarSlot Empty;
	Empty.bFilled = false;

	FCataclysmSkillBarSlot Broke;
	Broke.bFilled = true;
	Broke.bAffordable = false;

	FCataclysmSkillBarSlot Ready;
	Ready.bFilled = true;
	Ready.bAffordable = true;

	TestFalse(TEXT("an empty slot looks different from a ready one"),
			  UCataclysmSkillBar::TintFor(Empty)
				  .Equals(UCataclysmSkillBar::TintFor(Ready), 0.001f));
	TestFalse(TEXT("an unpayable slot looks different from a ready one"),
			  UCataclysmSkillBar::TintFor(Broke)
				  .Equals(UCataclysmSkillBar::TintFor(Ready), 0.001f));
	TestFalse(TEXT("and different from an empty one"),
			  UCataclysmSkillBar::TintFor(Broke)
				  .Equals(UCataclysmSkillBar::TintFor(Empty), 0.001f));

	// EVERY BOX IS DRAWN, whatever state it is in. A colour with no opacity is a
	// box nobody can see, which would look like the bar losing a slot.
	TestTrue(TEXT("an empty box is still visible"),
			 UCataclysmSkillBar::TintFor(Empty).A > 0.0f);
	TestTrue(TEXT("an unpayable box is still visible"),
			 UCataclysmSkillBar::TintFor(Broke).A > 0.0f);
	TestTrue(TEXT("a ready box is still visible"),
			 UCataclysmSkillBar::TintFor(Ready).A > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarKeyTest,
	"Cataclysm.SkillBar.EveryKeyTheGameBindsFitsInABox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarKeyTest::RunTest(const FString& Parameters)
{
	// THE KEYS THE GAME ACTUALLY BINDS, from the two mapping contexts in
	// `tools/generate_input_assets.py`. The Support slot is on W under mouse
	// movement and on 1 under keyboard movement, which is why both are here.
	TestEqual(TEXT("the right mouse button is RMB"),
			  UCataclysmSkillBar::KeyTextFor(EKeys::RightMouseButton),
			  FString(TEXT("RMB")));
	TestEqual(TEXT("the space bar is Space"),
			  UCataclysmSkillBar::KeyTextFor(EKeys::SpaceBar),
			  FString(TEXT("Space")));
	TestEqual(TEXT("the 1 key is 1 and not One"),
			  UCataclysmSkillBar::KeyTextFor(EKeys::One), FString(TEXT("1")));
	TestEqual(TEXT("a letter key is itself"),
			  UCataclysmSkillBar::KeyTextFor(EKeys::Q), FString(TEXT("Q")));

	// AND EVERY ONE OF THEM FITS. Five characters is what the box holds at the
	// size this text is drawn; the engine's own names -- "Right Mouse Button",
	// "Space Bar", "One" -- are what this function exists to avoid.
	const TArray<FKey> Bound = {
		EKeys::RightMouseButton, EKeys::Q, EKeys::W, EKeys::One,
		EKeys::E, EKeys::R, EKeys::SpaceBar,
	};

	for (const FKey& Key : Bound)
	{
		const FString Text = UCataclysmSkillBar::KeyTextFor(Key);

		TestFalse(FString::Printf(TEXT("%s has a label"), *Key.ToString()),
				  Text.IsEmpty());
		TestTrue(FString::Printf(
			TEXT("%s reads as \"%s\", which fits the box"),
			*Key.ToString(), *Text), Text.Len() <= 5);
	}

	// A KEY THAT IS NOT BOUND TO ANYTHING HAS NO LABEL, rather than a label
	// saying so. An empty box with a name under it is honest; a box reading
	// "None" looks like a key called None.
	TestTrue(TEXT("an invalid key has no label"),
			 UCataclysmSkillBar::KeyTextFor(FKey()).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarNameTest,
	"Cataclysm.SkillBar.ALongSkillNameIsShortenedToFitAndSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarNameTest::RunTest(const FString& Parameters)
{
	// THE FAULT THIS RULES OUT IS TWO NAMES PRINTED OVER EACH OTHER. The weapon
	// skill matrix holds names such as "Devastating Cleave", which is eighteen
	// characters, and the boxes are 64 pixels apart.
	const int32 Limit = UCataclysmSkillBar::MostNameCharacters;

	TestEqual(TEXT("a short name is left alone"),
			  UCataclysmSkillBar::ShortNameFor(TEXT("Cleave")),
			  FString(TEXT("Cleave")));
	TestEqual(TEXT("an empty name stays empty"),
			  UCataclysmSkillBar::ShortNameFor(FString()), FString());

	const FString Long = TEXT("Devastating Cleave");
	const FString Short = UCataclysmSkillBar::ShortNameFor(Long);

	TestTrue(FString::Printf(
		TEXT("\"%s\" is shortened to \"%s\", within %d characters"),
		*Long, *Short, Limit), Short.Len() <= Limit);
	TestTrue(TEXT("and it is marked as shortened rather than cut silently"),
			 Short.EndsWith(TEXT(".")));
	TestTrue(TEXT("and it still begins with the skill's own name"),
			 Long.StartsWith(Short.LeftChop(1)));

	// A NAME EXACTLY AT THE LIMIT IS NOT SHORTENED, which is the edge the mark
	// would otherwise be added at for no reason.
	const FString Exact = FString::ChrN(Limit, TEXT('A'));
	TestEqual(TEXT("a name exactly at the limit is left alone"),
			  UCataclysmSkillBar::ShortNameFor(Exact), Exact);

	const FString OneOver = FString::ChrN(Limit + 1, TEXT('A'));
	TestEqual(FString::Printf(TEXT("a name one character over is cut to %d"), Limit),
			  UCataclysmSkillBar::ShortNameFor(OneOver).Len(), Limit);

	// AND EVERY SLOT'S OWN NAME ALREADY FITS, so an empty bar is never a row of
	// shortened words.
	for (const ECataclysmAbilitySlot Slot : UCataclysmSkillBar::SlotsShown())
	{
		const FString Name = UCataclysmSkillBar::NameForEmptySlot(Slot);
		TestEqual(FString::Printf(TEXT("the name \"%s\" fits without shortening"),
				  *Name), UCataclysmSkillBar::ShortNameFor(Name), Name);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarNoPlayerTest,
	"Cataclysm.SkillBar.ReadingTheBarOfNothingGivesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarNoPlayerTest::RunTest(const FString& Parameters)
{
	// A HEADS-UP DISPLAY DRAWS BEFORE THERE IS A PAWN TO DRAW FOR, on the frames
	// between a level opening and a player appearing. Answering with an empty bar
	// is what lets the drawing code ask without checking first.
	TestEqual(TEXT("no player means no boxes"),
			  UCataclysmSkillBar::Read(nullptr).Num(), 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
