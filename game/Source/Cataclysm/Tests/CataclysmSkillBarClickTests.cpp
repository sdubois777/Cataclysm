// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Interface/CataclysmSkillBar.h"
#include "Player/CataclysmPlayerController.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * A left press on a skill box names that box's key, which opens the skill screen with it selected, and "K: skills" is
 * drawn by the bar. Issue #837 slice 2, the owner's Q3 of 2026-11-24 ("Approve all three (Recommended)"), as Diablo II
 * and Last Epoch open their choice from the bar.
 *
 * WHAT THESE GUARD. `UCataclysmSkillBar::KeyUnder` and `OpenScreenHint`, and `ACataclysmPlayerController::SkillBoxKeyAt`
 * on a possessed player. That the press is asked about the bar before anything else, and returns without a move or a
 * swing, is read out of the controller by `tools/tests/test_a_skill_box_opens_the_skill_screen.py`: a test world has no
 * viewport to press in.
 */
namespace CataclysmSkillBarClickTest
{
	constexpr float Wide = 1920.0f;
	constexpr float High = 1080.0f;

	/** One box for each slot an ability can occupy, in that order. */
	TArray<FCataclysmSkillBarSlot> EveryBox()
	{
		TArray<FCataclysmSkillBarSlot> Out;
		for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
		{
			FCataclysmSkillBarSlot& Box = Out.AddDefaulted_GetRef();
			Box.Slot = Slot;
		}
		return Out;
	}

	/** The middle of box `Index` of `Count`. */
	FVector2D MiddleOf(int32 Index, int32 Count)
	{
		const float Half = UCataclysmSkillBar::BoxSizePx * 0.5f;
		return UCataclysmSkillBar::BoxOriginFor(Index, Count, Wide, High) + FVector2D(Half, Half);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarClickKeyTest,
	"Cataclysm.SkillBar.AClickOnABoxNamesThatBoxesKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Every box's middle names its own key, the basic attack's included. */
bool FCataclysmSkillBarClickKeyTest::RunTest(const FString&)
{
	using namespace CataclysmSkillBarClickTest;
	const TArray<FCataclysmSkillBarSlot> Boxes = EveryBox();
	for (int32 Index = 0; Index < Boxes.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("**box %d names its key**"), Index),
				 UCataclysmSkillBar::KeyUnder(Boxes, MiddleOf(Index, Boxes.Num()), Wide, High) == Boxes[Index].Slot);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarClickOffTest,
	"Cataclysm.SkillBar.AClickBetweenOrOffTheBoxesNamesNoKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A point in the gap between the first two boxes, one above the bar and the screen's middle name no key, so such a
 * press is the move or the swing it always was. CONTROL: the first box's middle names its key.
 */
bool FCataclysmSkillBarClickOffTest::RunTest(const FString&)
{
	using namespace CataclysmSkillBarClickTest;
	const TArray<FCataclysmSkillBarSlot> Boxes = EveryBox();
	const FVector2D First = UCataclysmSkillBar::BoxOriginFor(0, Boxes.Num(), Wide, High);
	const FVector2D Gap(First.X + UCataclysmSkillBar::BoxSizePx + UCataclysmSkillBar::BoxGapPx * 0.5f,
						First.Y + UCataclysmSkillBar::BoxSizePx * 0.5f);
	const FVector2D Above(First.X + UCataclysmSkillBar::BoxSizePx * 0.5f, First.Y - 20.0f);
	TestTrue(TEXT("**the gap between two boxes names no key**"),
			 UCataclysmSkillBar::KeyUnder(Boxes, Gap, Wide, High) == ECataclysmAbilitySlot::None);
	TestTrue(TEXT("above the bar names no key"),
			 UCataclysmSkillBar::KeyUnder(Boxes, Above, Wide, High) == ECataclysmAbilitySlot::None);
	TestTrue(TEXT("the screen's middle names no key"),
			 UCataclysmSkillBar::KeyUnder(Boxes, FVector2D(Wide * 0.5f, High * 0.5f), Wide, High)
				 == ECataclysmAbilitySlot::None);
	TestTrue(TEXT("CONTROL: the first box's middle names its key"),
			 UCataclysmSkillBar::KeyUnder(Boxes, MiddleOf(0, Boxes.Num()), Wide, High) == Boxes[0].Slot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarControllerKeyTest,
	"Cataclysm.SkillBar.TheControllerNamesTheKeyOfTheBoxUnderAPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A possessed player wearing its starting Greataxe: the controller reads its own bar, and the middle of the Heavy key's
 * box names Heavy. CONTROL: the screen's middle names nothing, and so does any point with no viewport.
 */
bool FCataclysmSkillBarControllerKeyTest::RunTest(const FString&)
{
	using namespace CataclysmSkillBarClickTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerController* Controller = World->SpawnActor<ACataclysmPlayerController>();
	ACataclysmPlayerCharacter* Player = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player state, a controller and a player"), State && Controller && Player))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Player);
	const TArray<FCataclysmSkillBarSlot> Bar = UCataclysmSkillBar::Read(Player);
	const int32 Heavy = Bar.IndexOfByPredicate([](const FCataclysmSkillBarSlot& Box)
		{ return Box.Slot == ECataclysmAbilitySlot::Heavy; });
	if (!TestTrue(TEXT("set-up: the bar has a Heavy box"), Bar.IsValidIndex(Heavy)))
	{
		return false;
	}
	const FVector2D Viewport(Wide, High);
	TestTrue(TEXT("**the Heavy box's middle names Heavy**"),
			 Controller->SkillBoxKeyAt(MiddleOf(Heavy, Bar.Num()), Viewport) == ECataclysmAbilitySlot::Heavy);
	TestTrue(TEXT("CONTROL: the screen's middle names nothing"),
			 Controller->SkillBoxKeyAt(FVector2D(Wide * 0.5f, High * 0.5f), Viewport) == ECataclysmAbilitySlot::None);
	TestTrue(TEXT("CONTROL: with no viewport nothing is named"),
			 Controller->SkillBoxKeyAt(MiddleOf(Heavy, Bar.Num()), FVector2D::ZeroVector) == ECataclysmAbilitySlot::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillBarHintTest,
	"Cataclysm.SkillBar.TheBarNamesTheKeyThatOpensTheScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The line drawn by the bar is "K: skills". That K is the key bound to the skill screen is read out of
 * `tools/generate_input_assets.py` by `tools/tests/test_a_skill_box_opens_the_skill_screen.py`.
 */
bool FCataclysmSkillBarHintTest::RunTest(const FString&)
{
	TestEqual(TEXT("**the bar names K**"), FString(UCataclysmSkillBar::OpenScreenHint), FString(TEXT("K: skills")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
