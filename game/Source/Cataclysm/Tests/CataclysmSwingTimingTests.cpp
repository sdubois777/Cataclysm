// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSwingTiming.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Tests/CataclysmTestSkip.h"

/**
 * Tests for when a blow lands relative to the swing that throws it. Issue #1133.
 *
 * WHAT THIS GUARDS. Before issue #1133 every skill dealt its damage, drew its
 * arc and left its ground effects in the frame the ability activated, while the
 * swing animation played for one to nearly two seconds beside it. An enemy took
 * damage while the weapon was still going backwards.
 *
 * WHY THE ARITHMETIC IS TESTED AND THE WAIT IS NOT. A world built by
 * `UWorld::CreateWorld` is never ticked, so no automation test in this project
 * can watch a timer fire or an animation advance. That is why
 * `UCataclysmSwingTiming::SecondsUntilSwingConnects` is a pure static function
 * taking four numbers: every decision the wait makes is decided here, where it
 * can be checked, and the timer only counts.
 *
 * `UCataclysmBasicAttack` is built the same way and its header says the same
 * thing. This file is the same bargain.
 *
 * WHAT THIS DELIBERATELY DOES NOT COVER, and it is the important half:
 *
 *   WHETHER THE BLOW LOOKS LIKE IT LANDS WITH THE WEAPON. That is what issue
 *   #1133 is actually about, and nothing here can see it. The automation command
 *   passes `-nullrhi`, so nothing reaches a screen. Somebody has to play it and
 *   look.
 *
 *   WHETHER THE MARKER IS ON THE RIGHT FRAME. These tests supply the marker
 *   position as a number. Where it truly belongs on each clip was measured
 *   separately and is recorded in `game/docs/player-source-assets.md`, and that
 *   measurement refused two of the four clips.
 */

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

namespace CataclysmSwingTimingTest
{
	/** Answers "no marker on this clip", which is a negative number. */
	constexpr float Unmarked = -1.0f;

	/** Close enough for a figure in seconds. Well under a frame at 60 a second. */
	constexpr float CloseEnough = 0.0001f;

	/** Nothing is driving a swing rate, so there is no following swing. */
	constexpr float NoInterval = 0.0f;
}

// --------------------------------------------------------------------------
// The blow lands now when there is no animation to wait for
// --------------------------------------------------------------------------

/**
 * THE MOST IMPORTANT TEST IN THIS FILE, because it is what stops issue #1133
 * from silently deleting all damage everywhere it is not looking.
 *
 * Every enemy animates from its own class and never reports a swing. Every
 * checkout without the animation assets plays nothing. Every test in this suite
 * runs in a world that is never ticked, so a wait would never end. All three
 * arrive here, and all three have to deal their damage at once, exactly as they
 * did before the blow was allowed to arrive late.
 */
CATACLYSM_TEST(FCataclysmSwingWithNoClipLandsAtOnceTest,
	"Cataclysm.SwingTiming.ACharacterWithNoAnimationStrikesImmediately")
{
	using namespace CataclysmSwingTimingTest;

	TestEqual(TEXT("a clip of no length waits for nothing"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			/*ClipLength=*/0.0f, Unmarked, /*PlayRate=*/1.0f, NoInterval),
		0.0f, CloseEnough);

	TestEqual(TEXT("a negative clip length waits for nothing"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			-1.0f, Unmarked, 1.0f, NoInterval),
		0.0f, CloseEnough);

	// A rate of zero would divide by zero rather than wait forever, so it is
	// refused in the same place and for the same reason.
	TestEqual(TEXT("a play rate of zero waits for nothing"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			1.0f, 0.4f, /*PlayRate=*/0.0f, NoInterval),
		0.0f, CloseEnough);

	return true;
}

// --------------------------------------------------------------------------
// The marker on the clip decides when
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmMarkerDecidesWhenTest,
	"Cataclysm.SwingTiming.TheMarkerOnTheClipDecidesWhenTheBlowLands")
{
	using namespace CataclysmSwingTimingTest;

	// At the authored speed the wait is the marker's own position.
	TestEqual(TEXT("a marker at 0.37 s in a one second clip waits 0.37 s"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			1.0f, 0.37f, 1.0f, NoInterval),
		0.37f, CloseEnough);

	// ZERO IS A REAL ANSWER AND NOT "UNMARKED". A blow that lands on the first
	// frame is a legitimate clip. This is why MarkedStrikeSeconds answers a
	// NEGATIVE number for "no marker" rather than zero, and why the test inside
	// SecondsUntilSwingConnects is on the sign.
	TestEqual(TEXT("a marker on the first frame waits for nothing"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			1.0f, 0.0f, 1.0f, NoInterval),
		0.0f, CloseEnough);

	// A marker past the end of its own clip is a mistake, and it is clamped to
	// the clip rather than producing a wait longer than the animation.
	TestEqual(TEXT("a marker past the end of the clip is clamped to the end"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			1.0f, 5.0f, 1.0f, NoInterval),
		1.0f, CloseEnough);

	return true;
}

// --------------------------------------------------------------------------
// An unmarked clip still behaves
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmUnmarkedClipFallsBackTest,
	"Cataclysm.SwingTiming.AnUnmarkedClipFallsBackToTheMiddleOfItself")
{
	using namespace CataclysmSwingTimingTest;

	// EVERY CLIP IN THE GAME IS UNMARKED UNTIL SOMEBODY MARKS IT. This is not a
	// hypothetical branch: the four player clips and the thirteen enemy clips
	// all carried no marker when issue #1133 was opened.
	TestEqual(TEXT("an unmarked one second clip waits half a second"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			1.0f, Unmarked, 1.0f, NoInterval),
		0.5f, CloseEnough);

	TestEqual(TEXT("an unmarked 1.6667 second clip waits half of that"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			1.6667f, Unmarked, 1.0f, NoInterval),
		0.83335f, CloseEnough);

	return true;
}

// --------------------------------------------------------------------------
// It scales with attack speed, which is the whole point
// --------------------------------------------------------------------------

/**
 * THE PROPERTY THE PROJECT OWNER ASKED FOR BY NAME: a way of timing the blow
 * "that would allow for it to scale with attack speed and everything".
 *
 * The play rate is what already carries attack speed --
 * `ACataclysmPlayerCharacter::PlayAttackAnimation` sets it to the clip's length
 * over the swing interval -- so dividing the marker's position by that rate is
 * the whole mechanism. Nothing else has to be told about attack speed.
 */
CATACLYSM_TEST(FCataclysmWaitScalesWithAttackSpeedTest,
	"Cataclysm.SwingTiming.PlayingTheSwingTwiceAsFastHalvesTheWait")
{
	using namespace CataclysmSwingTimingTest;

	const float AtAuthoredSpeed = UCataclysmSwingTiming::SecondsUntilSwingConnects(
		1.0f, 0.40f, /*PlayRate=*/1.0f, NoInterval);
	const float AtDoubleSpeed = UCataclysmSwingTiming::SecondsUntilSwingConnects(
		1.0f, 0.40f, /*PlayRate=*/2.0f, NoInterval);

	TestEqual(TEXT("at the authored speed the wait is the marker"),
		AtAuthoredSpeed, 0.40f, CloseEnough);
	TestEqual(TEXT("at twice the speed the wait is half"),
		AtDoubleSpeed, 0.20f, CloseEnough);

	// Stated as the relationship as well as the two figures, because the
	// relationship is what has to survive a change to either number.
	TestEqual(TEXT("doubling the rate halves the wait"),
		AtDoubleSpeed * 2.0f, AtAuthoredSpeed, CloseEnough);

	// THE REAL FIGURES FROM THE GAME. MM_Attack_01 is 1.0000 s and its measured
	// strike is 0.3708 s. A Dagger at 1.5 swings a second gives a play rate of
	// 1.5, so the blow should land at 0.3708 / 1.5.
	TestEqual(TEXT("the measured clip at a dagger's speed"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			1.0f, 0.3708f, 1.5f, NoInterval),
		0.2472f, CloseEnough);

	return true;
}

// --------------------------------------------------------------------------
// Past the play rate ceiling the blow is pulled back inside the swing
// --------------------------------------------------------------------------

/**
 * WHY THIS CLAMP EXISTS. `ACataclysmPlayerCharacter` caps the play rate at 2.5
 * and attack speed has no design ceiling, so past that cap the clip takes longer
 * than the interval between swings. `MM_Attack_03` is already exactly at the cap
 * on the fastest base weapon with no affixes at all. Without a clamp the blow
 * for one swing would land after the following swing had already begun.
 */
CATACLYSM_TEST(FCataclysmBlowNeverLandsAfterTheNextSwingTest,
	"Cataclysm.SwingTiming.TheBlowNeverLandsAfterTheNextSwingBegins")
{
	using namespace CataclysmSwingTimingTest;

	// A long clip pinned at the rate cap against a very short interval. Without
	// the clamp this would be 1.6667 / 2.5 = 0.6667 s, well past the 0.2 second
	// interval, so the blow would land after two more swings had started.
	const float Interval = 0.2f;
	const float Waited = UCataclysmSwingTiming::SecondsUntilSwingConnects(
		1.6667f, /*Marked=*/0.7847f, /*PlayRate=*/2.5f, Interval);

	TestTrue(TEXT("the blow lands before the next swing begins"),
		Waited < Interval);
	TestEqual(TEXT("and it lands at the stated fraction of the interval"),
		Waited,
		Interval * UCataclysmSwingTiming::LatestFractionOfSwingInterval,
		CloseEnough);

	// AND THE CLAMP DOES NOT REACH DOWN AND CHANGE THE ORDINARY CASE. A Greataxe
	// swings every 0.7813 s and MM_Attack_01's blow lands at 0.3708 / 1.28,
	// which is nowhere near the limit. A clamp that fired here would be pulling
	// every blow in the game forward.
	const float Ordinary = UCataclysmSwingTiming::SecondsUntilSwingConnects(
		1.0f, 0.3708f, /*PlayRate=*/1.28f, /*Interval=*/0.7813f);
	TestEqual(TEXT("an ordinary swing is untouched by the clamp"),
		Ordinary, 0.3708f / 1.28f, CloseEnough);

	// An interval of zero means nothing is driving a rate -- a character holding
	// no weapon -- so there is no following swing to land in front of.
	TestEqual(TEXT("no swing interval means no clamp"),
		UCataclysmSwingTiming::SecondsUntilSwingConnects(
			1.6667f, 1.6f, 1.0f, NoInterval),
		1.6f, CloseEnough);

	return true;
}

// --------------------------------------------------------------------------
// Reading the marker off a clip
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmNoClipHasNoMarkerTest,
	"Cataclysm.SwingTiming.AskingANullClipForItsMarkerAnswersUnmarked")
{
	// NEGATIVE RATHER THAN ZERO, and the distinction is load-bearing: zero means
	// "the blow lands on the first frame", which is a real answer a real clip
	// could give.
	TestTrue(TEXT("a null clip reports no marker"),
		UCataclysmSwingTiming::MarkedStrikeSeconds(nullptr) < 0.0f);

	return true;
}

/**
 * THE ONE TEST HERE THAT LOOKS AT A REAL ASSET, and it exists because
 * everything above supplies the marker's position as a number. Nothing else
 * checks that the marker is on the clip at all.
 *
 * WHAT IT WOULD HAVE CAUGHT. `tools/place_swing_markers.py` reported all four
 * clips saved on its first run and wrote nothing to disk, because
 * `AnimationLibrary.add_animation_notify_event` does not mark the package dirty
 * and `save_asset` defaults to saving only dirty packages. Every signal inside
 * that script said success. This is the check that fails when the marker is not
 * really in the file.
 *
 * IT SKIPS RATHER THAN FAILS WITHOUT THE ART, which is the same bargain
 * `CataclysmPlayerAttackAnimationTests.cpp` makes. A checkout without
 * `game/Content/Characters/Mannequins/` has no clips at all, and a character
 * with no clips correctly strikes at once.
 */
CATACLYSM_TEST(FCataclysmClipsCarryTheirMarkerTest,
	"Cataclysm.SwingTiming.TheCycledAttackClipsCarryTheirMarker")
{
	using namespace CataclysmSwingTimingTest;

	// The measured strike time for each cycled clip, from
	// game/docs/player-source-assets.md, in the order the character cycles them.
	const float Expected[3] = { 0.3708f, 0.3458f, 0.7847f };

	int32 Found = 0;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"),
			ACataclysmPlayerCharacter::AttackAnimationFolder,
			ACataclysmPlayerCharacter::AttackAnimationNames[Index],
			ACataclysmPlayerCharacter::AttackAnimationNames[Index]);

		const UAnimSequence* Clip =
			LoadObject<UAnimSequence>(nullptr, *Path);
		if (!Clip)
		{
			continue;
		}

		++Found;
		const float Marked = UCataclysmSwingTiming::MarkedStrikeSeconds(Clip);

		TestTrue(*FString::Printf(TEXT("%s carries a marker"),
			ACataclysmPlayerCharacter::AttackAnimationNames[Index]),
			Marked >= 0.0f);

		// A LOOSE TOLERANCE ON PURPOSE. The exact figure is expected to be moved
		// by playing -- it came from a measurement that refused two of the four
		// clips -- so this guards that the marker is present and roughly where
		// it was put, not that nobody may ever retune it. A marker that drifted
		// by more than a tenth of a second is a mistake rather than tuning.
		TestTrue(*FString::Printf(TEXT("%s marks near its measured strike"),
			ACataclysmPlayerCharacter::AttackAnimationNames[Index]),
			FMath::Abs(Marked - Expected[Index]) < 0.1f);
	}

	if (Found == 0)
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("game/Content/Characters/Mannequins/ is not on this machine, "
				 "so there are no attack clips to carry a marker. That an "
				 "unmarked clip falls back sensibly IS checked above; that the "
				 "real clips are marked is not. See "
				 "game/docs/player-source-assets.md."));
	}

	return true;
}

#endif  // WITH_AUTOMATION_TESTS
