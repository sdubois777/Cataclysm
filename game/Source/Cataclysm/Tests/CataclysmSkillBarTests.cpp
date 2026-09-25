// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Interface/CataclysmHUD.h"
#include "Interface/CataclysmSkillBar.h"

namespace CataclysmSkillBarTest
{
	/**
	 * An actor the bar can be read from: an ability system and two granted
	 * skills, and nothing else.
	 *
	 * SMALLER THAN THE FIGHTER THE OTHER TEST FILES KEEP, on purpose. Thirteen
	 * files carry their own `FScopedFighter`, each built for what that file
	 * measures. The bar asks a character three things -- which ability is in
	 * which slot, what it costs, and whether a lock refuses it -- so this one
	 * carries the two attribute sets those need and stops there.
	 *
	 * NO PLAYER CONTROLLER, which is why every box's key text is empty here.
	 * `UCataclysmSkillBar::Read` handles that already and says so; these tests
	 * are about the lock, and `EveryKeyTheGameBindsFitsInABox` above covers keys.
	 */
	struct FBarCharacter
	{
		explicit FBarCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>(FVector::ZeroVector,
											  FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();

			// AN ATTRIBUTE SET NEEDS AN OWNER WITH A REGISTERED COMPONENT, or
			// writing to it crashes the whole run rather than failing a test.
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmCombatAttributeSet>(Actor));
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Put a skill in a slot, carrying the slot tag a designed row would carry. */
	template <typename T>
	T* Grant(FBarCharacter& Who, ECataclysmAbilitySlot Slot, const FString& Params,
			 const FString& TagCell)
	{
		const FGameplayAbilitySpecHandle Handle =
			Who.AbilitySystem->GiveAbilityInSlot(T::StaticClass(), Slot,
												 /*Level=*/100, Who.Actor);
		FGameplayAbilitySpec* Spec =
			Handle.IsValid() ? Who.AbilitySystem->FindAbilitySpecFromHandle(Handle)
							 : nullptr;
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = TEXT("Test Skill");
			Instance->Params = UCataclysmSkillShapes::ParseParams(Params);

			// READ THE TAG CELL THE WAY THE REAL PATH READS IT, so a test cannot
			// pass with a tag a designed row could not produce.
			Instance->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		}
		return Instance;
	}

	/** Lock skills, optionally only those carrying one tag. */
	void LockSkills(FBarCharacter& Who, const FGameplayTag& OnlyThisSlot)
	{
		FCataclysmStatModifier Lock;
		Lock.Bucket = ECataclysmStatBucket::Flat;
		Lock.Source = ECataclysmModifierSource::Enchantment;
		Lock.Value = 1.0f;
		if (OnlyThisSlot.IsValid())
		{
			Lock.RequiredTags.AddTag(OnlyThisSlot);
		}

		FCataclysmStatInputs Inputs;
		Inputs.Base = 0.0f;
		Inputs.Modifiers.Add(Lock);

		TMap<FName, FCataclysmStatInputs> Stats;
		Stats.Add(FName(UCataclysmSkillSlots::LockedStat), Inputs);
		Who.AbilitySystem->SetStatInputs(MoveTemp(Stats));
	}

	/** The box for one slot, or null when the bar drew none. */
	const FCataclysmSkillBarSlot* BoxFor(const TArray<FCataclysmSkillBarSlot>& Bar,
										 ECataclysmAbilitySlot Slot)
	{
		return Bar.FindByPredicate(
			[Slot](const FCataclysmSkillBarSlot& Box) { return Box.Slot == Slot; });
	}
}

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
	using namespace CataclysmSkillBarTest;

	// THIS IS THE ONE THAT ALREADY COST SOMEBODY AN EVENING. Issue #653 was
	// reported as "sometimes all of my abilities just become disabled", and it
	// was an empty mana pool with nothing on screen saying so.
	//
	// ON A CHARACTER SINCE ISSUE #1910. The rule is asked of a character's own
	// pool rather than of two numbers, so the numbers are put in its mana.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBarCharacter Who(World);
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();
	Who.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 100.0f);
	const auto AffordsWith = [&Who, &Mana](float Held, float Cost)
	{
		Who.AbilitySystem->SetNumericAttributeBase(Mana, Held);
		return UCataclysmSkillBar::CanAfford(Who.AbilitySystem, Mana, Cost);
	};

	TestTrue(TEXT("plenty of mana pays a small cost"), AffordsWith(100.0f, 10.0f));
	TestFalse(TEXT("too little mana does not"), AffordsWith(9.0f, 10.0f));

	// EXACTLY ENOUGH IS ENOUGH. A bar that greyed out a skill the character could
	// actually cast would be worse than no bar at all.
	TestTrue(TEXT("exactly enough mana pays"), AffordsWith(10.0f, 10.0f));

	// A FREE SKILL IS ALWAYS PAYABLE, including on an empty pool. The Movement
	// slot's designed cost is zero.
	TestTrue(TEXT("a skill that costs nothing is payable with nothing"),
			 AffordsWith(0.0f, 0.0f));

	// AND NOTHING IS GREYED OUT WHILE THERE IS NO POOL TO READ. A pool that
	// cannot be read reads as zero, so asking it anyway in the frames after a
	// pawn appears would grey out every skill, which is issue #653 again.
	TestTrue(TEXT("with no ability system every cost is payable"),
			 UCataclysmSkillBar::CanAfford(nullptr, Mana, 10.0f));
	AActor* Bare = World->SpawnActor<AActor>(FVector::ZeroVector,
											 FRotator::ZeroRotator);
	const UCataclysmAbilitySystemComponent* NoSets =
		Bare ? NewObject<UCataclysmAbilitySystemComponent>(Bare) : nullptr;
	if (TestNotNull(TEXT("an ability system with no attribute sets"), NoSets))
	{
		TestTrue(TEXT("and with no attribute set holding the pool, neither is any"),
				 UCataclysmSkillBar::CanAfford(NoSets, Mana, 10.0f));
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarWaterToBloodTest,
	"Cataclysm.SkillBar.ASkillIsPayableFromHealthWhenTheManaPoolBecameHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The bar shows a skill as payable when the cast would be paid from health.
 * Issue #1910.
 *
 * WHAT WENT WRONG. The bar compared raw mana with each skill's cost. Water to
 * Blood sets the mana pool to zero and pays every cost from health, so a
 * Masochist holding it saw every skill with a cost drawn as unaffordable while
 * each one cast.
 *
 * THROUGH `Read`, THE FUNCTION THE HEADS-UP DISPLAY CALLS, rather than
 * `CanAfford` alone, so this fails if the bar stops asking the rule even while
 * the rule itself is right.
 *
 * THE FLAG IS A STAT INPUT, for the reason `FCataclysmSkillWaterToBloodCostTest`
 * gives: the option is asked through `StatForSkill`, so writing the attribute
 * would measure nothing.
 */
bool FCataclysmSkillBarWaterToBloodTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSkillBarTest;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBarCharacter Who(World);
	const UCataclysmStrikeSkill* Strike = Grant<UCataclysmStrikeSkill>(
		Who, ECataclysmAbilitySlot::Heavy, TEXT("Radius=4; Angle=360"),
		TEXT("Slot.Heavy"));
	if (!TestNotNull(TEXT("a heavy skill was granted"), Strike))
	{
		return false;
	}
	const float Cost = Strike->GetManaCost();
	if (!TestTrue(FString::Printf(TEXT("the heavy skill costs something (%.1f)"),
								  Cost),
				  Cost > 0.0f))
	{
		return false;
	}

	// NO MANA AT ALL, which is how the option leaves a character, and far more
	// health than the cost.
	Who.AbilitySystem->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 0.0f);
	Who.AbilitySystem->SetNumericAttributeBase(Vital::GetManaAttribute(), 0.0f);
	Who.AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(),
											  100'000.0f);
	Who.AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(),
											  100'000.0f);

	// THE CONTROL FIRST. Without the option a character with no mana cannot pay,
	// so what follows is evidence of the option and not of a bar that marks
	// everything payable.
	{
		const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Who.Actor);
		const FCataclysmSkillBarSlot* Box = BoxFor(Bar, ECataclysmAbilitySlot::Heavy);
		if (!TestNotNull(TEXT("the bar drew a box for the heavy skill"), Box))
		{
			return false;
		}
		TestFalse(TEXT("without the option, no mana cannot pay"), Box->bAffordable);
	}

	FCataclysmStatModifier Traded;
	Traded.Bucket = ECataclysmStatBucket::Flat;
	Traded.Source = ECataclysmModifierSource::PassiveKeystone;
	Traded.Value = 1.0f;

	FCataclysmStatInputs Inputs;
	Inputs.Base = 0.0f;
	Inputs.Modifiers.Add(Traded);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(UCataclysmSkillTemplate::ManaPoolBecomesHealthStat), Inputs);
	Who.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	// THE HEADLINE. The same character, holding the option, can pay from health.
	{
		const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Who.Actor);
		const FCataclysmSkillBarSlot* Box = BoxFor(Bar, ECataclysmAbilitySlot::Heavy);
		if (!TestNotNull(TEXT("the bar still draws the heavy skill"), Box))
		{
			return false;
		}
		TestTrue(TEXT("holding the option, it is payable from health"),
				 Box->bAffordable);
	}

	// AND HEALTH IS ASKED THE WAY THE CAST ASKS IT: strictly more than the cost,
	// because a cost that took the last of it would kill.
	Who.AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Cost);
	{
		const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Who.Actor);
		const FCataclysmSkillBarSlot* Box = BoxFor(Bar, ECataclysmAbilitySlot::Heavy);
		if (!TestNotNull(TEXT("and draws it again"), Box))
		{
			return false;
		}
		TestFalse(TEXT("with health exactly equal to the cost it is not payable"),
				  Box->bAffordable);
	}

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

// ---------------------------------------------------------------------------
// A skill a lock is refusing. Issue #1810
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarUnscopedLockTest,
	"Cataclysm.SkillBar.AnUnscopedLockMarksEverySkillOnTheBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarUnscopedLockTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSkillBarTest;

	// WHAT A SILENCED PLAYER SAW BEFORE THIS: nothing. Issue #1810. The refusal
	// in `UCataclysmSkillTemplate::CanActivateAbility` returns before the
	// engine's own checks so the player is not told the wrong reason, and no
	// reason was ever put in its place. This is the reason.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBarCharacter Who(World);
	const bool bGranted =
		Grant<UCataclysmMovementSkill>(Who, ECataclysmAbilitySlot::Movement,
									   TEXT("Mode=Blink; Range=9; Radius=2"),
									   TEXT("Slot.Movement")) != nullptr
		&& Grant<UCataclysmStrikeSkill>(Who, ECataclysmAbilitySlot::Heavy,
										TEXT("Radius=4; Angle=360"),
										TEXT("Slot.Heavy")) != nullptr;
	if (!TestTrue(TEXT("two skills were granted"), bGranted))
	{
		return false;
	}

	// THE CONTROL FIRST, so what follows is evidence of the lock rather than of
	// a bar that marks everything.
	{
		const TArray<FCataclysmSkillBarSlot> Before = UCataclysmSkillBar::Read(Who.Actor);
		const FCataclysmSkillBarSlot* Step = BoxFor(Before, ECataclysmAbilitySlot::Movement);
		const FCataclysmSkillBarSlot* Swing = BoxFor(Before, ECataclysmAbilitySlot::Heavy);
		if (!TestNotNull(TEXT("the bar drew a box for the movement skill"), Step)
			|| !TestNotNull(TEXT("and one for the heavy skill"), Swing))
		{
			return false;
		}
		if (!TestTrue(TEXT("both boxes hold a skill"), Step->bFilled && Swing->bFilled))
		{
			return false;
		}
		TestFalse(TEXT("neither is marked locked before anything locks them"),
				  Step->bLocked || Swing->bLocked);
	}

	// THE LOCK, CARRYING NO TAGS, which is what "preventing all skill usage"
	// means and what the dungeon rule `Celestial_Edict_of_Silence` applies.
	LockSkills(Who, FGameplayTag());

	const TArray<FCataclysmSkillBarSlot> After = UCataclysmSkillBar::Read(Who.Actor);

	int32 Holding = 0;
	int32 Marked = 0;
	for (const FCataclysmSkillBarSlot& Box : After)
	{
		if (!Box.bFilled)
		{
			continue;
		}
		++Holding;
		Marked += Box.bLocked ? 1 : 0;
	}

	// COUNTED RATHER THAN SPOT-CHECKED, so this says how many boxes it looked at
	// as well as what it found. An empty box holds no skill and is not counted
	// either way; only two of the six slots were granted anything.
	TestEqual(TEXT("the bar drew two boxes holding a skill"), Holding, 2);
	TestEqual(TEXT("and an unscoped lock marks every one of them"), Marked, Holding);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarScopedLockTest,
	"Cataclysm.SkillBar.ALockScopedToOneSlotMarksOnlyThatBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarScopedLockTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSkillBarTest;

	// THIS IS THE TEST THE WHOLE DESIGN RESTS ON, and the test above cannot do
	// its job. Issue #1810. The lock is read through
	// `UCataclysmAbilitySystemComponent::StatForSkill`, which scopes each
	// modifier by the tags it is handed, so one stat serves both an enchantment
	// that locks a single slot and a dungeon rule that locks everything.
	//
	// THE OBVIOUS MISTAKE IS TO READ IT ONCE FOR THE WHOLE BAR, the way mana is
	// read once above -- and an UNSCOPED lock answers the same either way, so the
	// test above would pass with that mistake in place. A scoped lock is the only
	// arrangement where the two implementations differ.
	//
	// `game/Data/EnchantmentEffects.csv` holds two rows that do exactly this, so
	// it is not a hypothetical shape: one is scoped to `Slot.Movement` and one to
	// `Slot.Ultimate`.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBarCharacter Who(World);
	const bool bGranted =
		Grant<UCataclysmMovementSkill>(Who, ECataclysmAbilitySlot::Movement,
									   TEXT("Mode=Blink; Range=9; Radius=2"),
									   TEXT("Slot.Movement")) != nullptr
		&& Grant<UCataclysmStrikeSkill>(Who, ECataclysmAbilitySlot::Heavy,
										TEXT("Radius=4; Angle=360"),
										TEXT("Slot.Heavy")) != nullptr;
	if (!TestTrue(TEXT("two skills were granted"), bGranted))
	{
		return false;
	}

	LockSkills(Who, CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::Movement));

	const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Who.Actor);
	const FCataclysmSkillBarSlot* Step = BoxFor(Bar, ECataclysmAbilitySlot::Movement);
	const FCataclysmSkillBarSlot* Swing = BoxFor(Bar, ECataclysmAbilitySlot::Heavy);
	if (!TestNotNull(TEXT("the bar drew a box for the movement skill"), Step)
		|| !TestNotNull(TEXT("and one for the heavy skill"), Swing))
	{
		return false;
	}
	if (!TestTrue(TEXT("both boxes hold a skill, so both could have been marked"),
				  Step->bFilled && Swing->bFilled))
	{
		return false;
	}

	// BOTH ASSERTIONS OR NEITHER. Either one alone passes for an implementation
	// that ignores the scoping entirely -- marking everything, or marking
	// nothing -- which is the argument
	// `Cataclysm.Skills.ALockScopedToOneSlotLeavesTheOtherSlotsAlone` already
	// makes where it holds the same property at the refusal.
	TestTrue(TEXT("a lock scoped to the movement slot marks the movement box"),
			 Step->bLocked);
	TestFalse(TEXT("and leaves the box in another slot alone"), Swing->bLocked);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarLockedTintTest,
	"Cataclysm.SkillBar.ALockedAndUnaffordableBoxShowsLockedRatherThanUnaffordable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarLockedTintTest::RunTest(const FString& Parameters)
{
	// A LOCKED SKILL IS REFUSED AT EVERY MANA LEVEL, so the lock is the fact that
	// decides whether the box can be used and the other order would tell a
	// silenced player to go and find mana. The same kind of argument `TintFor`
	// already makes for showing unpayable before the wait.
	FCataclysmSkillBarSlot Both;
	Both.bFilled = true;
	Both.bLocked = true;
	Both.bAffordable = false;

	FCataclysmSkillBarSlot Broke;
	Broke.bFilled = true;
	Broke.bLocked = false;
	Broke.bAffordable = false;

	FCataclysmSkillBarSlot Locked;
	Locked.bFilled = true;
	Locked.bLocked = true;
	Locked.bAffordable = true;

	FCataclysmSkillBarSlot Ready;
	Ready.bFilled = true;

	// THE WINDOW HAS TO EXIST BEFORE THE ASSERTION MEANS ANYTHING. If the locked
	// and unpayable colours were ever given the same value, the assertion below
	// would pass whichever order `TintFor` checked them in, and this test would
	// go on passing while saying nothing. Asserted first for that reason.
	if (!TestFalse(TEXT("the locked and unpayable colours differ, so an order exists"),
				   UCataclysmSkillBar::TintFor(Locked)
					   .Equals(UCataclysmSkillBar::TintFor(Broke), 0.001f)))
	{
		return false;
	}
	if (!TestTrue(TEXT("and the box under test really is both locked and unpayable"),
				  Both.bLocked && !Both.bAffordable))
	{
		return false;
	}

	TestTrue(TEXT("a box that is both shows the locked colour"),
			 UCataclysmSkillBar::TintFor(Both)
				 .Equals(UCataclysmSkillBar::TintFor(Locked), 0.001f));

	// AND IT IS ITS OWN STATE, not a shade of one that already existed.
	TestFalse(TEXT("a locked box looks different from a ready one"),
			  UCataclysmSkillBar::TintFor(Locked)
				  .Equals(UCataclysmSkillBar::TintFor(Ready), 0.001f));

	// EVERY BOX IS STILL DRAWN, for the reason the affordability test gives: a
	// colour with no opacity is a box nobody can see, which reads as the bar
	// losing a slot.
	TestTrue(TEXT("a locked box is still visible"),
			 UCataclysmSkillBar::TintFor(Locked).A > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarNoLockTest,
	"Cataclysm.SkillBar.NoBoxIsMarkedWhenNothingIsLocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarNoLockTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSkillBarTest;

	// THE CONTROL FOR THE WHOLE FEATURE. A bar that marked every box locked would
	// pass both tests above and would grey out every skill of every character who
	// has no lock on them at all, which is the fault issue #653 was reported as
	// and which `bAffordable`'s own comment was written to prevent repeating.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBarCharacter Who(World);
	if (!TestNotNull(TEXT("a skill was granted"),
					 Grant<UCataclysmStrikeSkill>(Who, ECataclysmAbilitySlot::Heavy,
												  TEXT("Radius=4; Angle=360"),
												  TEXT("Slot.Heavy"))))
	{
		return false;
	}

	const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Who.Actor);

	int32 Holding = 0;
	int32 Marked = 0;
	for (const FCataclysmSkillBarSlot& Box : Bar)
	{
		Holding += Box.bFilled ? 1 : 0;
		Marked += Box.bLocked ? 1 : 0;
	}

	TestEqual(TEXT("the bar drew one box holding a skill"), Holding, 1);
	TestEqual(TEXT("and no box of any kind is marked locked"), Marked, 0);

	// AND NOTHING IS SAID IN WORDS EITHER, which is the other half: a character
	// nothing has locked must not be told their skills are locked.
	TestFalse(TEXT("and the words are not shown"),
			  UCataclysmSkillBar::EverySkillIsLocked(Bar));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarLockedWordTest,
	"Cataclysm.SkillBar.TheWordIsShownOnlyWhileEveryDrawnSlotIsLocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarLockedWordTest::RunTest(const FString& Parameters)
{
	// SOME LOCKED IS NOT THE SAME AS ALL LOCKED, and the difference is the whole
	// reason this function exists. Issue #1810. A single-slot enchantment lock is
	// the bar's business and a line of text across the screen for one greyed box
	// would be noise; fifteen seconds with every skill refused is the case a
	// player reads as the game having stopped working.
	auto Slot = [](bool bFilled, bool bLocked)
	{
		FCataclysmSkillBarSlot Box;
		Box.bFilled = bFilled;
		Box.bLocked = bLocked;
		return Box;
	};

	TArray<FCataclysmSkillBarSlot> AllLocked;
	TArray<FCataclysmSkillBarSlot> OneLocked;
	TArray<FCataclysmSkillBarSlot> NoneLocked;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		AllLocked.Add(Slot(true, true));
		OneLocked.Add(Slot(true, Index == 0));
		NoneLocked.Add(Slot(true, false));
	}

	TestTrue(TEXT("six skills, all locked, says so"),
			 UCataclysmSkillBar::EverySkillIsLocked(AllLocked));
	TestFalse(TEXT("six skills with one locked says nothing"),
			  UCataclysmSkillBar::EverySkillIsLocked(OneLocked));
	TestFalse(TEXT("and six skills with none locked says nothing"),
			  UCataclysmSkillBar::EverySkillIsLocked(NoneLocked));

	// FIVE LOCKED OUT OF SIX IS STILL NOT ALL OF THEM. The case above has one
	// locked; this one has one NOT locked, which is the boundary the word turns
	// on and the case an "any locked" reading and an "all locked" reading agree
	// about least.
	TArray<FCataclysmSkillBarSlot> AllButOne = AllLocked;
	AllButOne[5] = Slot(true, false);
	TestFalse(TEXT("five of six locked still says nothing"),
			  UCataclysmSkillBar::EverySkillIsLocked(AllButOne));

	// AN EMPTY BOX HOLDS NO SKILL, so it is neither locked nor unlocked. A
	// character with a slot spare must not be silenced from saying anything, and
	// a character with no skills at all is unarmed rather than silenced.
	TArray<FCataclysmSkillBarSlot> LockedAndEmpty;
	LockedAndEmpty.Add(Slot(true, true));
	LockedAndEmpty.Add(Slot(false, false));
	TestTrue(TEXT("an empty box beside a locked one does not stop the words"),
			 UCataclysmSkillBar::EverySkillIsLocked(LockedAndEmpty));

	TArray<FCataclysmSkillBarSlot> Empty;
	Empty.Add(Slot(false, false));
	TestFalse(TEXT("a bar holding no skills at all says nothing"),
			  UCataclysmSkillBar::EverySkillIsLocked(Empty));
	TestFalse(TEXT("and neither does an empty bar"),
			  UCataclysmSkillBar::EverySkillIsLocked(TArray<FCataclysmSkillBarSlot>()));

	// AND THE WORDS MUST NOT SAY THE PLAYER CANNOT ACT, because that is false:
	// `UCataclysmSkillTemplate::CanActivateAbility` exempts the basic attack
	// unconditionally and `Celestial_Edict_of_Silence`'s row says "Only basic
	// attacks function during this period". The bar draws no box for that slot,
	// so this line is the only place the player is told.
	const FString Notice = UCataclysmSkillBar::LockedNotice();
	TestFalse(TEXT("the words are not empty"), Notice.IsEmpty());
	TestTrue(TEXT("and they say the basic attack still works"),
			 Notice.Contains(TEXT("BASIC ATTACK")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarGrantedNameTest,
	"Cataclysm.SkillBar.ABoxShowsTheNameOfTheSkillTheWeaponGranted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillBarGrantedNameTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSkillBarTest;

	// THIS IS A DEFECT THE LOCK WORK FOUND, NOT PART OF THE LOCK. Issue #1810.
	// `CataclysmSkillBarAbilityIn` returned `Spec.Ability`, which is the CLASS
	// DEFAULT OBJECT. Everything that tells one granted skill from another is
	// stamped on the INSTANCE by `UCataclysmWeaponSlotsComponent`, so
	// `DisplayedName()` answered empty and `Read` fell through to
	// `NameForEmptySlot` -- every box on the bar showed its slot's generic name
	// instead of the skill the weapon granted, for as long as that helper has
	// existed.
	//
	// NOTHING NOTICED BECAUSE NOTHING CALLED `Read` WITH A CHARACTER. The only
	// call in this file before the lock tests was `Read(nullptr)`. This test
	// exists so the fallback written to stop an empty box cannot go back to
	// covering a real name.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBarCharacter Who(World);
	UCataclysmStrikeSkill* Granted =
		Grant<UCataclysmStrikeSkill>(Who, ECataclysmAbilitySlot::Heavy,
									 TEXT("Radius=4; Angle=360"), TEXT("Slot.Heavy"));
	if (!TestNotNull(TEXT("a skill was granted"), Granted))
	{
		return false;
	}

	// A NAME THAT COULD NOT COME FROM ANYWHERE ELSE, so a box carrying it proves
	// the instance was read. The slot's own fallback is the string this is being
	// told apart from, and it is asserted below rather than assumed.
	Granted->SkillName = TEXT("Riven Arc");

	const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Who.Actor);
	const FCataclysmSkillBarSlot* Box = BoxFor(Bar, ECataclysmAbilitySlot::Heavy);
	if (!TestNotNull(TEXT("the bar drew a box for the granted skill"), Box))
	{
		return false;
	}

	const FString Fallback =
		UCataclysmSkillBar::NameForEmptySlot(ECataclysmAbilitySlot::Heavy);
	if (!TestFalse(TEXT("the granted name and the slot's fallback differ, so an "
						"answer exists"),
				   Granted->SkillName.Equals(Fallback)))
	{
		return false;
	}

	TestEqual(TEXT("the box shows the granted skill's own name"),
			  Box->Name, FString(TEXT("Riven Arc")));
	TestNotEqual(TEXT("and not the slot's generic name"), Box->Name, Fallback);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarNextUseLineTest,
	"Cataclysm.SkillBar.TheNextUseLineNamesEachKindOfChargeHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The line drawn above the bar for held next-use charges. Issue #1833, phase 2:
 * the owner's rule that every system ships with a basic interface. One entry
 * per kind, the count only when more than one is held, and nothing at all when
 * none are.
 */
bool FCataclysmSkillBarNextUseLineTest::RunTest(const FString&)
{
	TestEqual(TEXT("one skill charge of 60 and two attack charges worth 40"),
		UCataclysmSkillBar::NextUseLine(60.0f, 1, 40.0f, 2),
		FString(TEXT("Next skill +60%   Next attack +40% (2)")));
	TestTrue(TEXT("and nothing held draws nothing"),
		UCataclysmSkillBar::NextUseLine(0.0f, 0, 0.0f, 0).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarHeldStacksLineTest,
	"Cataclysm.SkillBar.TheLineNamesEffectivenessAndEachEnchantmentsOwnStacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The line above the bar, with an effectiveness charge and two enchantments'
 * own stacks. Issue #1833, timed grants: the own stacks of #2083 shipped with
 * nothing on screen, which the owner's standing rule forbids. "Every 10 seconds
 * gain a stack of momentum" is two rows, on attack and spell damage, granted
 * together, and shows as ONE entry.
 */
bool FCataclysmSkillBarHeldStacksLineTest::RunTest(const FString&)
{
	const TArray<FString> Stacks = {
		UCataclysmSkillBar::OwnStacksEntry(
			{FName(TEXT("attack_damage")), FName(TEXT("spell_damage"))}, 3, 5),
		UCataclysmSkillBar::OwnStacksEntry({FName(TEXT("armor"))}, 2, 10),
	};
	TestEqual(TEXT("a skill charge, an effectiveness charge and two enchantments' stacks"),
		UCataclysmSkillBar::NextUseLine(60.0f, 1, 0.0f, 0, 300.0f, Stacks),
		FString(TEXT("Next skill +60%   Next skill 300% effectiveness   "
					 "attack/spell damage 3/5   armor 2/10")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
