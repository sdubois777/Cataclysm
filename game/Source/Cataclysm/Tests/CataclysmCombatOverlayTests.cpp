// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmHUD.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"

/**
 * The combat overlay: the bar over a hurt creature, the floating number where a
 * blow lands, and the player's own health. Issue #518.
 *
 * WHAT IS COVERED AND WHAT CANNOT BE. Not one of these tests watches anything
 * reach the screen, and none of them tries. AHUD::PostRender checks
 * FApp::CanEverRender() before it calls DrawHUD, and the automation command in
 * tools/unreal_build.py passes -nullrhi, so DrawHUD never runs under test at
 * all. That is the same wall issue #559 records for the impact particle.
 *
 * SO EVERY JUDGEMENT WAS PUT SOMEWHERE A TEST CAN REACH IT. Whether a number is
 * drawn, what it says, what colour, how large, whether a bar appears, how far
 * above a head, how fast it fades -- all of it is a static function over plain
 * numbers on UCataclysmCombatOverlay, and all of it is below. What is left
 * untested is the drawing itself: the DrawRect and DrawText calls in
 * ACataclysmHUD, and whether the result is legible. Those need a person to look
 * at them.
 *
 * TWO OF THESE GUARD A DESIGN RULE RATHER THAN CODE, both from section XIII of
 * docs/Cataclysm_GDD_v2.md. The health bar must not wear the attack telegraph's
 * #FF3020, which "There is one telegraph colour for the whole game" reserves,
 * and the numbers must distinguish outcomes by more than colour alone, which
 * "Over the World, Not on the Frame" requires in the words "Colour says where
 * the damage went and the text says which outcome it was, so neither is the
 * only channel". Both would be easy to undo by picking a nicer colour later.
 *
 * THE RULES ARE CITED BY HEADING RATHER THAN BY LINE NUMBER, deliberately. The
 * design document is edited, and a line number in a comment is wrong the moment
 * anything above it grows -- including the very change that writes the comment.
 */

namespace CataclysmOverlayTest
{
	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	static ACataclysmEnemyCharacter* SpawnEnemy(UWorld* World, float Health)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
														FRotator::ZeroRotator);
		if (Spawned)
		{
			// The two sides are Players and Monsters. Without a side, an actor
			// is hostile to everything, which is a deliberate failure mode
			// rather than a default worth relying on here.
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			// SetHealth sets the MAXIMUM, and the current value follows it.
			Spawned->SetHealth(Health);
		}
		return Spawned;
	}

	/**
	 * A player pawn wired the way the game wires one.
	 *
	 * THE TWO-ACTOR DANCE IS NOT OPTIONAL. The player's ability system lives on
	 * its player state, so a pawn spawned on its own has none at all and
	 * VitalsOf answers false for it -- which would make a test of the player's
	 * health bar pass for the wrong reason.
	 */
	static ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerCharacter* Pawn =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
														 FRotator::ZeroRotator);
		ACataclysmPlayerState* State =
			World->SpawnActor<ACataclysmPlayerState>();
		if (Pawn && State)
		{
			Pawn->SetPlayerState(State);
			Pawn->OnRep_PlayerState();
		}
		return Pawn;
	}

	/** An outcome in which a blow reached health and nothing stopped it. */
	static FCataclysmDamageResult Landed(float ToHealth)
	{
		FCataclysmDamageResult Outcome;
		Outcome.DealtToHealth = ToHealth;
		return Outcome;
	}

	static FCataclysmIncomingHit OrdinaryHit()
	{
		FCataclysmIncomingHit Hit;
		Hit.Damage = 100.0f;
		return Hit;
	}
}

// --------------------------------------------------------------------------
// Whether a number is drawn at all
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayNumbersHitsThatConnected,
	"Cataclysm.Overlay.ANumberIsDrawnForAHitThatConnected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayNumbersHitsThatConnected::RunTest(const FString&)
{
	TestTrue(TEXT("a blow that reached health gets a number"),
		UCataclysmCombatOverlay::ShouldShowNumberFor(
			CataclysmOverlayTest::OrdinaryHit(),
			CataclysmOverlayTest::Landed(42.0f), /*HealthRemaining=*/500.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayNumbersWhatTheParticleRefuses,
	"Cataclysm.Overlay.ANumberIsDrawnForHitsTheImpactParticleRefuses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayNumbersWhatTheParticleRefuses::RunTest(const FString&)
{
	// THIS IS THE OPPOSITE RULE TO THE PARTICLE'S, ON PURPOSE.
	// UCataclysmImpactEffect::ShouldDrawFor refuses both of these so that a
	// burst means "that landed". A number is wanted for exactly them: a hit
	// stopped dead by armour and resistance is what issues #483 and #644 are
	// about, and it is invisible in play without one.
	const FCataclysmIncomingHit Hit = CataclysmOverlayTest::OrdinaryHit();

	FCataclysmDamageResult Evaded;
	Evaded.bEvaded = true;

	FCataclysmDamageResult WhollyMitigated;

	TestTrue(TEXT("an evaded blow still gets a number"),
		UCataclysmCombatOverlay::ShouldShowNumberFor(Hit, Evaded, 500.0f));
	TestTrue(TEXT("a blow armour and resistance took to nothing gets a number"),
		UCataclysmCombatOverlay::ShouldShowNumberFor(Hit, WhollyMitigated,
													 500.0f));

	// And the particle really does refuse both, so the two rules are genuinely
	// different rather than only described as different. If somebody later
	// makes ShouldDrawFor permissive, this stops claiming a contrast that is no
	// longer there.
	TestFalse(TEXT("the impact particle refuses the evaded blow"),
		UCataclysmImpactEffect::ShouldDrawFor(Hit, Evaded));
	TestFalse(TEXT("and refuses the wholly mitigated blow"),
		UCataclysmImpactEffect::ShouldDrawFor(Hit, WhollyMitigated));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayDrawsNothingOnACorpse,
	"Cataclysm.Overlay.NoNumberIsDrawnForAHitOnSomethingAlreadyDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayDrawsNothingOnACorpse::RunTest(const FString&)
{
	// A burn still ticking on a corpse. Resolve ends with
	// FMath::Min(Damage, Vitals->GetHealth()), so every one of these deals
	// nothing. Issue #570 counted fifty-six arriving over seventy seconds.
	FCataclysmIncomingHit Tick = CataclysmOverlayTest::OrdinaryHit();
	Tick.bIsDamageOverTime = true;

	TestFalse(TEXT("a tick on something at zero health gets no number"),
		UCataclysmCombatOverlay::ShouldShowNumberFor(
			Tick, FCataclysmDamageResult(), /*HealthRemaining=*/0.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayDrawsTheKillingBlow,
	"Cataclysm.Overlay.TheKillingBlowStillGetsItsNumber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayDrawsTheKillingBlow::RunTest(const FString&)
{
	// THE CASE THE CORPSE RULE MUST NOT SWALLOW. A killing blow leaves health at
	// zero, exactly like a hit on a corpse, and it is the single most important
	// number in the fight. What separates them is that this one dealt damage
	// getting there.
	TestTrue(TEXT("the blow that killed something is drawn"),
		UCataclysmCombatOverlay::ShouldShowNumberFor(
			CataclysmOverlayTest::OrdinaryHit(),
			CataclysmOverlayTest::Landed(120.0f), /*HealthRemaining=*/0.0f));

	return true;
}

// --------------------------------------------------------------------------
// What the number says
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayRoundsRatherThanTruncates,
	"Cataclysm.Overlay.ASmallHitReadsAsOneRatherThanAsZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayRoundsRatherThanTruncates::RunTest(const FString&)
{
	// TRUNCATING WOULD MAKE A HIT THAT DID SOMETHING SAY IT DID NOTHING, which
	// is the one thing a damage number must never do -- and "0" already means
	// something specific here, namely that the defence stopped the whole blow.
	TestEqual(TEXT("0.6 damage reads as 1"),
		UCataclysmCombatOverlay::TextFor(CataclysmOverlayTest::Landed(0.6f)),
		FString(TEXT("1")));
	TestEqual(TEXT("41.4 damage reads as 41"),
		UCataclysmCombatOverlay::TextFor(CataclysmOverlayTest::Landed(41.4f)),
		FString(TEXT("41")));

	// AND ROUNDING MUST NOT ROUND IT AWAY EITHER. FMath::RoundToInt is
	// FloorToInt(F + 0.5f), so anything under half a point of damage rounds to
	// zero -- and this reads a bare "0" as "the defence stopped the whole
	// blow", which is the opposite of what happened.
	//
	// THE WINDOW IS REACHABLE AND IT IS NOT AN EDGE CASE.
	// UCataclysmDamageCalculation::Resolve ends with
	// FMath::Min(Damage, Vitals->GetHealth()), so a killing blow's figure is
	// exactly the target's remaining health however large the blow was. A
	// creature sitting on 0.3 health is alive and hittable, and the blow that
	// kills it deals 0.3.
	TestEqual(TEXT("0.42 damage reads as 1, not as 0"),
		UCataclysmCombatOverlay::TextFor(CataclysmOverlayTest::Landed(0.42f)),
		FString(TEXT("1")));
	TestEqual(TEXT("and a killing blow of 0.3 does too"),
		UCataclysmCombatOverlay::TextFor(CataclysmOverlayTest::Landed(0.3f)),
		FString(TEXT("1")));
	TestEqual(TEXT("but nothing at all is still zero"),
		UCataclysmCombatOverlay::FigureFor(0.0f), 0);
	TestEqual(TEXT("and so is a negative figure"),
		UCataclysmCombatOverlay::FigureFor(-3.0f), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayTextAndColourNeverDisagree,
	"Cataclysm.Overlay.TheTextAndTheColourNeverSayOppositeThings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayTextAndColourNeverDisagree::RunTest(const FString&)
{
	// THE TWO CHANNELS MUST AGREE ON EVERY OUTCOME. They are separate functions
	// reading the same struct, and they used to read it differently: TextFor
	// worked from a rounded integer while ColourFor tested the raw float, so a
	// blow dealing 0.42 to health printed the word for "nothing got through" in
	// the colour for "this reached health", on the same number, at the moment it
	// killed something.
	//
	// This is the general guard rather than a second test of the same case: it
	// walks the whole awkward band and both sides of it.
	const FLinearColor ReachedHealth = UCataclysmCombatOverlay::ColourFromHex(
		UCataclysmCombatOverlay::ReachedHealthHex);
	const FLinearColor Absorbed = UCataclysmCombatOverlay::ColourFromHex(
		UCataclysmCombatOverlay::AbsorbedHex);

	const float Amounts[] = { 0.01f, 0.2f, 0.42f, 0.49f, 0.5f, 0.51f, 0.9f,
							  1.0f, 7.0f, 549.0f };

	for (const float Amount : Amounts)
	{
		const FString Reached =
			UCataclysmCombatOverlay::TextFor(
				CataclysmOverlayTest::Landed(Amount));
		TestNotEqual(FString::Printf(
			TEXT("%.2f reached health, so its text is not the stopped zero"),
			Amount), Reached, FString(TEXT("0")));
		TestTrue(FString::Printf(
			TEXT("%.2f reached health, so its colour says so"), Amount),
			UCataclysmCombatOverlay::ColourFor(
				CataclysmOverlayTest::Landed(Amount)).Equals(ReachedHealth));

		FCataclysmDamageResult ShieldOnly;
		ShieldOnly.AbsorbedByShield = Amount;
		TestNotEqual(FString::Printf(
			TEXT("%.2f went to a shield, so its text is not the stopped zero"),
			Amount), UCataclysmCombatOverlay::TextFor(ShieldOnly),
			FString(TEXT("0")));
		TestTrue(FString::Printf(
			TEXT("%.2f went to a shield, so its colour says so"), Amount),
			UCataclysmCombatOverlay::ColourFor(ShieldOnly).Equals(Absorbed));
	}

	// A BLOCK THAT LEFT A FRACTION BEHIND IS NOT A BLOCK THAT LEFT NOTHING. A
	// block removes half the hit, so this is the ordinary shape of a small
	// blocked blow, and it must not read as the word for one that stopped
	// everything.
	FCataclysmDamageResult BlockedButLanded;
	BlockedButLanded.bBlocked = true;
	BlockedButLanded.DealtToHealth = 0.42f;
	TestEqual(TEXT("a block that still let 0.42 through reads as 1"),
		UCataclysmCombatOverlay::TextFor(BlockedButLanded),
		FString(TEXT("1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayNamesEveryOutcomeInWords,
	"Cataclysm.Overlay.AnOutcomeWithNoFigureIsSaidInWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayNamesEveryOutcomeInWords::RunTest(const FString&)
{
	FCataclysmDamageResult Evaded;
	Evaded.bEvaded = true;

	FCataclysmDamageResult Blocked;
	Blocked.bBlocked = true;

	TestEqual(TEXT("an evaded blow says so"),
		UCataclysmCombatOverlay::TextFor(Evaded), FString(TEXT("Evaded")));
	TestEqual(TEXT("a block that left nothing says so"),
		UCataclysmCombatOverlay::TextFor(Blocked), FString(TEXT("Blocked")));
	TestEqual(TEXT("a blow stopped by armour and resistance shows a zero"),
		UCataclysmCombatOverlay::TextFor(FCataclysmDamageResult()),
		FString(TEXT("0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlaySaysWhereTheDamageWent,
	"Cataclysm.Overlay.ANumberSaysWhetherAShieldTookSomeOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlaySaysWhereTheDamageWent::RunTest(const FString&)
{
	FCataclysmDamageResult ShieldOnly;
	ShieldOnly.AbsorbedByShield = 30.0f;

	FCataclysmDamageResult Both;
	Both.DealtToHealth = 12.0f;
	Both.AbsorbedByShield = 30.0f;

	FCataclysmDamageResult ManaOnly;
	ManaOnly.AbsorbedByMana = 8.0f;

	TestEqual(TEXT("a shield swallowing the whole blow shows its figure"),
		UCataclysmCombatOverlay::TextFor(ShieldOnly), FString(TEXT("30")));
	TestEqual(TEXT("health first, then what the shield took"),
		UCataclysmCombatOverlay::TextFor(Both), FString(TEXT("12 (+30)")));
	TestEqual(TEXT("a mana pool swallowing the blow shows its figure"),
		UCataclysmCombatOverlay::TextFor(ManaOnly), FString(TEXT("8")));

	return true;
}

// --------------------------------------------------------------------------
// Colour, and the two design rules it has to obey
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayAvoidsTheTelegraphRed,
	"Cataclysm.Overlay.NoBarWearsTheReservedTelegraphRed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayAvoidsTheTelegraphRed::RunTest(const FString&)
{
	// #FF3020 BELONGS TO THE ATTACK MARKER AND TO NOTHING ELSE. Section XIII of
	// docs/Cataclysm_GDD_v2.md states there is one telegraph colour for the
	// whole game -- not one per Cataclysm, not one per damage type --
	// because the marker has to mean "this ground is about to hurt" in every
	// environment. A health bar in the same red weakens the only signal that
	// must survive all eight. This is the guard against somebody later picking
	// a brighter red because it looks better.
	const FString Reserved(TEXT("FF3020"));
	const FLinearColor Telegraph =
		UCataclysmCombatOverlay::ColourFromHex(*Reserved);

	// EVERY COLOUR THIS CLASS DECLARES, rather than the three that were listed by
	// hand until issue #661. Two were missing: the mana bar, added when the mana
	// display was built, and nothing said so. A named list cannot notice a
	// colour nobody remembered to add to it, so the count below is asserted too.
	struct FNamedColour { const TCHAR* What; const TCHAR* Hex; };
	const FNamedColour All[] = {
		{ TEXT("the bar backing"), UCataclysmCombatOverlay::BarBackingHex },
		{ TEXT("the health bar"), UCataclysmCombatOverlay::HealthFillHex },
		{ TEXT("the shield bar"), UCataclysmCombatOverlay::ShieldFillHex },
		{ TEXT("the mana bar"), UCataclysmCombatOverlay::ManaFillHex },
		{ TEXT("a number that reached health"),
		  UCataclysmCombatOverlay::ReachedHealthHex },
		{ TEXT("a number a pool absorbed"),
		  UCataclysmCombatOverlay::AbsorbedHex },
		{ TEXT("a number nothing got through"),
		  UCataclysmCombatOverlay::NothingThroughHex },
		{ TEXT("a critical strike"),
		  UCataclysmCombatOverlay::CriticalStrikeHex },
	};

	TestEqual(TEXT("every colour this class declares is in the list above"),
		static_cast<int32>(UE_ARRAY_COUNT(All)), 8);

	for (const FNamedColour& Entry : All)
	{
		TestNotEqual(FString::Printf(
			TEXT("%s is not the telegraph red"), Entry.What),
			FString(Entry.Hex), Reserved);

		// AND NOT MERELY A DIFFERENT STRING FROM IT. The check above passes for
		// FF3021, which no eye can tell from FF3020. That matters now that a
		// number is deliberately warm: the critical strike colour added under
		// issue #668 is the first thing in this class chosen to be orange, and
		// "orange, but not that orange" is a distance rather than an inequality.
		const FLinearColor Colour =
			UCataclysmCombatOverlay::ColourFromHex(Entry.Hex);
		const float Distance =
			FMath::Abs(Colour.R - Telegraph.R)
			+ FMath::Abs(Colour.G - Telegraph.G)
			+ FMath::Abs(Colour.B - Telegraph.B);

		TestTrue(FString::Printf(
			TEXT("%s is far enough from the telegraph red to tell apart "
				 "(distance %.2f)"), Entry.What, Distance),
			Distance > 0.25f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayColoursSayWhereDamageWent,
	"Cataclysm.Overlay.FourOutcomesGetFourDifferentColours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayColoursSayWhereDamageWent::RunTest(const FString&)
{
	// THREE OF THESE SAY WHERE THE DAMAGE WENT AND THE FOURTH SAYS WHAT KIND OF
	// HIT IT WAS. That is a second job for one channel and it was taken on
	// deliberately under issue #668, after the project owner played a build where
	// a critical strike was marked only by size and an exclamation mark and could
	// not tell one from an ordinary hit. All four still have to be told apart.
	FCataclysmDamageResult ShieldOnly;
	ShieldOnly.AbsorbedByShield = 30.0f;

	FCataclysmDamageResult Crit = CataclysmOverlayTest::Landed(20.0f);
	Crit.bWasCritical = true;

	struct FNamedOutcome { const TCHAR* What; FCataclysmDamageResult Outcome; };
	const FNamedOutcome All[] = {
		{ TEXT("reaching health"), CataclysmOverlayTest::Landed(20.0f) },
		{ TEXT("being absorbed"), ShieldOnly },
		{ TEXT("being stopped"), FCataclysmDamageResult() },
		{ TEXT("a critical strike"), Crit },
	};

	for (int32 First = 0; First < UE_ARRAY_COUNT(All); ++First)
	{
		for (int32 Second = First + 1; Second < UE_ARRAY_COUNT(All); ++Second)
		{
			TestFalse(FString::Printf(TEXT("%s looks different from %s"),
				All[First].What, All[Second].What),
				UCataclysmCombatOverlay::ColourFor(All[First].Outcome).Equals(
					UCataclysmCombatOverlay::ColourFor(All[Second].Outcome)));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayColourIsNotTheOnlyChannel,
	"Cataclysm.Overlay.ColourIsNotTheOnlyChannelSeparatingTwoOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayColourIsNotTheOnlyChannel::RunTest(const FString&)
{
	// THE DESIGN REQUIRES THIS, in "Over the World, Not on the Frame" in section
	// XIII of docs/Cataclysm_GDD_v2.md: "Colour says where the damage went and
	// the text says which outcome it was, so neither is the only channel."
	// Evaded, blocked and stopped-by-mitigation all share the grey, so the words
	// are what tell them apart, and they must stay different words.
	FCataclysmDamageResult Evaded;
	Evaded.bEvaded = true;

	FCataclysmDamageResult Blocked;
	Blocked.bBlocked = true;

	const FString EvadedText = UCataclysmCombatOverlay::TextFor(Evaded);
	const FString BlockedText = UCataclysmCombatOverlay::TextFor(Blocked);
	const FString StoppedText =
		UCataclysmCombatOverlay::TextFor(FCataclysmDamageResult());

	TestTrue(TEXT("evaded and blocked share one colour"),
		UCataclysmCombatOverlay::ColourFor(Evaded).Equals(
			UCataclysmCombatOverlay::ColourFor(Blocked)));

	TestNotEqual(TEXT("but they do not share a word"), EvadedText, BlockedText);
	TestNotEqual(TEXT("nor does either match a blow stopped by mitigation"),
		EvadedText, StoppedText);
	TestNotEqual(TEXT("nor does blocked match one stopped by mitigation"),
		BlockedText, StoppedText);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayColoursAllParse,
	"Cataclysm.Overlay.EveryColourConstantParsesToSomethingVisible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayColoursAllParse::RunTest(const FString&)
{
	// FColor::FromHex DOES NOT REPORT BAD INPUT, it returns something -- the
	// same trap CataclysmTelegraphMarker.cpp records. A typo in one of the
	// constants would leave a bar drawn in black on a near-black backing, which
	// looks exactly like a bar that is not being drawn at all.
	struct FNamedColour { const TCHAR* What; const TCHAR* Hex; };
	const FNamedColour All[] = {
		{ TEXT("the health fill"), UCataclysmCombatOverlay::HealthFillHex },
		{ TEXT("the shield fill"), UCataclysmCombatOverlay::ShieldFillHex },
		{ TEXT("a number that reached health"),
		  UCataclysmCombatOverlay::ReachedHealthHex },
		{ TEXT("a number a pool absorbed"),
		  UCataclysmCombatOverlay::AbsorbedHex },
		{ TEXT("a number nothing got through"),
		  UCataclysmCombatOverlay::NothingThroughHex },
		{ TEXT("a critical strike"),
		  UCataclysmCombatOverlay::CriticalStrikeHex },
		{ TEXT("the mana fill"), UCataclysmCombatOverlay::ManaFillHex },
	};

	for (const FNamedColour& Entry : All)
	{
		const FLinearColor Colour =
			UCataclysmCombatOverlay::ColourFromHex(Entry.Hex);
		const float Brightest =
			FMath::Max3(Colour.R, Colour.G, Colour.B);

		TestTrue(FString::Printf(
			TEXT("%s is not black, so it can be seen on the dark backing"),
			Entry.What), Brightest > 0.05f);
		TestEqual(FString::Printf(TEXT("%s is fully opaque"), Entry.What),
			Colour.A, 1.0f);
	}

	// The backing is the one that IS meant to be near-black.
	const FLinearColor Backing = UCataclysmCombatOverlay::ColourFromHex(
		UCataclysmCombatOverlay::BarBackingHex);
	TestTrue(TEXT("the backing behind a bar is dark"),
		FMath::Max3(Backing.R, Backing.G, Backing.B) < 0.05f);

	return true;
}

// --------------------------------------------------------------------------
// Size, rise and fade
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayDrawsATickSmaller,
	"Cataclysm.Overlay.ADamageOverTimeTickIsDrawnSmallerThanABlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayDrawsATickSmaller::RunTest(const FString&)
{
	// SIZE IS WHAT SAYS "A TICK IS NOT A BLOW". The impact particle answers the
	// same question by refusing to draw for a tick at all -- issue #563 measured
	// one attack producing seven bursts in five seconds, five of them burn
	// ticks. A number is cheap enough to draw every time, which is what the
	// genre does, but it should not shout as loudly as the strike.
	FCataclysmIncomingHit Tick = CataclysmOverlayTest::OrdinaryHit();
	Tick.bIsDamageOverTime = true;

	// AN ORDINARY OUTCOME, which is to say not a critical strike. Size carries
	// two facts now and this test is about the other one.
	const FCataclysmDamageResult Landed = CataclysmOverlayTest::Landed(20.0f);

	const float BlowScale = UCataclysmCombatOverlay::ScaleFor(
		CataclysmOverlayTest::OrdinaryHit(), Landed);
	const float TickScale = UCataclysmCombatOverlay::ScaleFor(Tick, Landed);

	TestEqual(TEXT("a blow is drawn at full size"), BlowScale, 1.0f);
	TestTrue(TEXT("a tick is drawn smaller than a blow"), TickScale < BlowScale);
	TestTrue(TEXT("but still large enough to read"), TickScale > 0.4f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayNumberRisesThenStops,
	"Cataclysm.Overlay.ANumberRisesUpTheScreenAndThenStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayNumberRisesThenStops::RunTest(const FString&)
{
	const float Lifetime = UCataclysmCombatOverlay::NumberLifetimeSeconds;

	TestEqual(TEXT("it starts where the blow landed"),
		UCataclysmCombatOverlay::RisePixelsFor(0.0f), 0.0f);
	TestTrue(TEXT("it has risen by half way"),
		UCataclysmCombatOverlay::RisePixelsFor(Lifetime * 0.5f) > 0.0f);
	TestTrue(TEXT("and risen further by the end"),
		UCataclysmCombatOverlay::RisePixelsFor(Lifetime * 0.9f)
			> UCataclysmCombatOverlay::RisePixelsFor(Lifetime * 0.5f));
	TestEqual(TEXT("it stops at the full rise rather than running away"),
		UCataclysmCombatOverlay::RisePixelsFor(Lifetime * 10.0f),
		UCataclysmCombatOverlay::NumberRisePixels);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayNumberIsReadableFirst,
	"Cataclysm.Overlay.ANumberIsFullyOpaqueBeforeItBeginsToFade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayNumberIsReadableFirst::RunTest(const FString&)
{
	// A NUMBER THAT STARTS FADING IMMEDIATELY IS HARDEST TO READ AT THE MOMENT
	// IT IS MOST WANTED, which is the instant the blow lands.
	const float Lifetime = UCataclysmCombatOverlay::NumberLifetimeSeconds;

	TestEqual(TEXT("it is fully opaque when it appears"),
		UCataclysmCombatOverlay::FadeFor(0.0f), 1.0f);
	TestEqual(TEXT("and still fully opaque a third of the way through"),
		UCataclysmCombatOverlay::FadeFor(Lifetime / 3.0f), 1.0f);

	const float LateFade = UCataclysmCombatOverlay::FadeFor(Lifetime * 0.9f);
	TestTrue(TEXT("it has begun to fade near the end"), LateFade < 1.0f);
	TestTrue(TEXT("but is not gone yet"), LateFade > 0.0f);

	TestEqual(TEXT("it is gone once its lifetime is up"),
		UCataclysmCombatOverlay::FadeFor(Lifetime), 0.0f);
	TestEqual(TEXT("and stays gone"),
		UCataclysmCombatOverlay::FadeFor(Lifetime * 5.0f), 0.0f);

	return true;
}

// --------------------------------------------------------------------------
// Whether a bar appears over a creature
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayNoBarOverAnUnhurtCreature,
	"Cataclysm.Overlay.NoBarIsDrawnOverACreatureThatHasNotBeenHurt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayNoBarOverAnUnhurtCreature::RunTest(const FString&)
{
	// THE PROJECT OWNER'S DECISION, and what Path of Exile does even with its
	// own "Show Mini Life Bars on Enemies" setting enabled: a bar appears once
	// an enemy has been damaged, and not before. It keeps the design's
	// deliberately dark world from being papered over with interface, and makes
	// a bar mean "this fight has started".
	TestFalse(TEXT("a creature at full health gets no bar"),
		UCataclysmCombatOverlay::ShouldShowBarFor(549.0f, 549.0f));
	TestTrue(TEXT("one that has been hurt does"),
		UCataclysmCombatOverlay::ShouldShowBarFor(548.0f, 549.0f));
	TestTrue(TEXT("and one nearly dead does"),
		UCataclysmCombatOverlay::ShouldShowBarFor(1.0f, 549.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayNoBarOverACorpse,
	"Cataclysm.Overlay.NoBarIsDrawnOverSomethingAtZeroHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayNoBarOverACorpse::RunTest(const FString&)
{
	// An enemy destroys itself on the tick AFTER it dies, because HandleDeath
	// runs inside the gameplay effect callback that dealt the killing blow.
	// Without this rule an empty bar flashes for one frame at the end of every
	// fight.
	TestFalse(TEXT("something at zero health gets no bar"),
		UCataclysmCombatOverlay::ShouldShowBarFor(0.0f, 549.0f));
	TestFalse(TEXT("nor does something with no maximum health at all"),
		UCataclysmCombatOverlay::ShouldShowBarFor(0.0f, 0.0f));

	return true;
}

/**
 * A pool with anything left in it never prints zero.
 *
 * THE ONE THING A HEALTH READOUT MUST NOT SAY about something still standing.
 * Health, mana and the energy shield are unrounded floats that are only
 * clamped, so a character sitting on 0.3 health is alive, standing and
 * hittable -- and rounding alone prints "0 / 500" for them, at exactly the
 * moment the figure is being read hardest. Issue #743 found it on the player's
 * own bars, where it had been since they were built; the creature panel had
 * the rule and the frame did not, so the two disagreed about the same pool.
 *
 * IT IS NOT A RARE CASE. UCataclysmDamageCalculation::Resolve ends with
 * FMath::Min(Damage, Vitals->GetHealth()), so a blow that would have killed
 * leaves health at exactly what was there rather than at a round number.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayPoolNeverReadsZeroWhenAlive,
	"Cataclysm.Overlay.APoolWithAnythingLeftInItNeverReadsZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayPoolNeverReadsZeroWhenAlive::RunTest(const FString&)
{
	using FOverlay = UCataclysmCombatOverlay;

	TestEqual(TEXT("a full pool reads both figures"),
		FOverlay::PoolTextFor(250.0f, 250.0f), FString(TEXT("250 / 250")));

	TestEqual(TEXT("a part-used one reads what is left"),
		FOverlay::PoolTextFor(112.4f, 250.0f), FString(TEXT("112 / 250")));

	// THE ONE THAT MATTERS, and it is what issue #743 was.
	TestEqual(TEXT("a fraction of a point still reads 1"),
		FOverlay::PoolTextFor(0.3f, 250.0f), FString(TEXT("1 / 250")));

	TestEqual(TEXT("and so does one just under half a point"),
		FOverlay::PoolTextFor(0.49f, 250.0f), FString(TEXT("1 / 250")));

	// AN EMPTY POOL READS ZERO, WHICH IS TRUE OF IT. Issue #653 is why the
	// figures are drawn at all: a mana pool at zero and one at one look the
	// same on a bar that short, and the difference decides whether anything
	// can be cast. So zero has to be reachable and has to mean zero.
	TestEqual(TEXT("a pool actually at zero reads zero"),
		FOverlay::PoolTextFor(0.0f, 250.0f), FString(TEXT("0 / 250")));
	TestEqual(TEXT("and so does one somehow below it"),
		FOverlay::PoolTextFor(-4.0f, 250.0f), FString(TEXT("0 / 250")));

	// NO POOL AT ALL SAYS NOTHING RATHER THAN "0 / 0". That is every
	// character before its attributes have arrived, and every class with no
	// energy shield, which is a design position rather than an error.
	TestTrue(TEXT("a pool that does not exist says nothing"),
		FOverlay::PoolTextFor(0.0f, 0.0f).IsEmpty());
	TestTrue(TEXT("and neither does a negative maximum"),
		FOverlay::PoolTextFor(10.0f, -1.0f).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayBarFractionIsBounded,
	"Cataclysm.Overlay.ABarIsNeverFilledPastItsEndsOrBelowZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayBarFractionIsBounded::RunTest(const FString&)
{
	TestEqual(TEXT("half health fills half the bar"),
		UCataclysmCombatOverlay::BarFractionFor(50.0f, 100.0f), 0.5f);
	TestEqual(TEXT("full health fills all of it"),
		UCataclysmCombatOverlay::BarFractionFor(100.0f, 100.0f), 1.0f);
	TestEqual(TEXT("more than full does not overflow"),
		UCataclysmCombatOverlay::BarFractionFor(150.0f, 100.0f), 1.0f);
	TestEqual(TEXT("negative health does not draw backwards"),
		UCataclysmCombatOverlay::BarFractionFor(-5.0f, 100.0f), 0.0f);
	TestEqual(TEXT("no maximum divides by nothing"),
		UCataclysmCombatOverlay::BarFractionFor(50.0f, 0.0f), 0.0f);

	return true;
}

// --------------------------------------------------------------------------
// Which actors get a bar, and where above them it sits
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlaySkipsThePlayerAndTheDead,
	"Cataclysm.Overlay.NoOverheadBarOverThePlayerItselfOrOverTheDead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlaySkipsThePlayerAndTheDead::RunTest(const FString&)
{
	UWorld* World = CataclysmOverlayTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmOverlayTest::SpawnPlayer(World);
	ACataclysmEnemyCharacter* Enemy =
		CataclysmOverlayTest::SpawnEnemy(World, 549.0f);
	if (!TestNotNull(TEXT("a player pawn"), Player)
		|| !TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	TestTrue(TEXT("an ordinary enemy is a candidate for a bar"),
		UCataclysmCombatOverlay::IsOverheadBarCandidate(Enemy, Player));

	// The player's own health is on the frame instead, where it can be read
	// without looking away from what is attacking.
	TestFalse(TEXT("the player's own pawn is not"),
		UCataclysmCombatOverlay::IsOverheadBarCandidate(Player, Player));

	UCataclysmSkillEffects::MarkDead(Enemy);
	TestFalse(TEXT("and neither is an enemy once it is dead"),
		UCataclysmCombatOverlay::IsOverheadBarCandidate(Enemy, Player));

	TestFalse(TEXT("nor is nothing at all"),
		UCataclysmCombatOverlay::IsOverheadBarCandidate(nullptr, Player));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayAnchorsAboveTheBody,
	"Cataclysm.Overlay.ABarSitsAboveACreatureRatherThanInsideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayAnchorsAboveTheBody::RunTest(const FString&)
{
	UWorld* World = CataclysmOverlayTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		CataclysmOverlayTest::SpawnEnemy(World, 549.0f);
	if (!TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	// READ OFF THE BOUNDS RATHER THAN FIXED, because a Brute and an Imp are not
	// the same height and one offset would put a bar inside one head and in mid
	// air over the other. What is checked here is that it clears the body at
	// all: a character's capsule is about 88 cm tall from its centre, so
	// anything at or below the margin alone means the bounds were not read.
	const float Height = UCataclysmCombatOverlay::AnchorHeightFor(Enemy);

	TestTrue(TEXT("the bar clears the creature's own body"),
		Height > UCataclysmCombatOverlay::AnchorMarginCm);

	// An actor with no components has no bounds, and the honest answer for
	// something with no body is the margin alone rather than a guess at one.
	AActor* Bodiless = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("a bare actor"), Bodiless))
	{
		TestEqual(TEXT("something with no body gets the margin alone"),
			UCataclysmCombatOverlay::AnchorHeightFor(Bodiless),
			UCataclysmCombatOverlay::AnchorMarginCm);
	}

	TestEqual(TEXT("and so does nothing at all"),
		UCataclysmCombatOverlay::AnchorHeightFor(nullptr),
		UCataclysmCombatOverlay::AnchorMarginCm);

	return true;
}

// --------------------------------------------------------------------------
// Reading health off either shape of ability system
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayReadsBothOwnerShapes,
	"Cataclysm.Overlay.HealthIsReadFromAPawnOwnedAndAStateOwnedAbilitySystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayReadsBothOwnerShapes::RunTest(const FString&)
{
	// THE OWNER-VERSUS-AVATAR TRAP, WHICH HAS ALREADY COST TWO BUGS. An enemy
	// owns its ability system on its pawn; the player's lives on its player
	// state. Issue #562 drew every blow an enemy landed on the player at the
	// world origin because of that difference, and issue #565 was the same
	// mistake again in the death path. VitalsOf goes through the ability system
	// interface, so it must answer for both without knowing which it holds.
	UWorld* World = CataclysmOverlayTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		CataclysmOverlayTest::SpawnEnemy(World, 549.0f);
	ACataclysmPlayerCharacter* Player =
		CataclysmOverlayTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("an enemy"), Enemy)
		|| !TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	float Health = 0.0f;
	float MaxHealth = 0.0f;

	TestTrue(TEXT("an enemy's health is readable"),
		UCataclysmCombatOverlay::VitalsOf(Enemy, Health, MaxHealth));
	TestEqual(TEXT("and it is the health it was given"), MaxHealth, 549.0f);
	TestEqual(TEXT("and it starts full"), Health, 549.0f);

	Health = 0.0f;
	MaxHealth = 0.0f;
	TestTrue(TEXT("the player's health is readable through its player state"),
		UCataclysmCombatOverlay::VitalsOf(Player, Health, MaxHealth));
	TestTrue(TEXT("and the player has some"), MaxHealth > 0.0f);

	AActor* Bodiless = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("an actor with no ability system"), Bodiless))
	{
		TestFalse(TEXT("something with no ability system answers no"),
			UCataclysmCombatOverlay::VitalsOf(Bodiless, Health, MaxHealth));
	}

	return true;
}

// --------------------------------------------------------------------------
// The heads-up display's own bookkeeping
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayGameModeNamesTheDisplay,
	"Cataclysm.Overlay.TheGameModeNamesTheCataclysmHeadsUpDisplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayGameModeNamesTheDisplay::RunTest(const FString&)
{
	// WITHOUT THIS LINE THE ENGINE SPAWNS A BARE AHUD, which draws nothing and
	// reports nothing. A heads-up display that is simply absent looks exactly
	// like one that is switched off, so there would be no error to follow.
	const ACataclysmGameMode* Defaults =
		GetDefault<ACataclysmGameMode>();
	if (!TestNotNull(TEXT("the game mode's defaults"), Defaults))
	{
		return false;
	}

	TestEqual(TEXT("the game mode names ACataclysmHUD"),
		Defaults->HUDClass.Get(),
		static_cast<UClass*>(ACataclysmHUD::StaticClass()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayKeepsTheNewestNumbers,
	"Cataclysm.Overlay.WhenTooManyNumbersArriveTheOldestAreDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayKeepsTheNewestNumbers::RunTest(const FString&)
{
	UWorld* World = CataclysmOverlayTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmHUD* Display = World->SpawnActor<ACataclysmHUD>();
	if (!TestNotNull(TEXT("a heads-up display"), Display))
	{
		return false;
	}

	// AN AREA SKILL LANDING ON TWENTY ENEMIES PRODUCES TWENTY NUMBERS IN ONE
	// FRAME, and issue #563 measured one attack producing seven impacts in five
	// seconds. The ceiling has to cost the numbers already fading rather than
	// the one describing the blow that just landed.
	const int32 Ceiling = UCataclysmCombatOverlay::MaxNumbersWaiting;
	for (int32 Index = 0; Index < Ceiling + 10; ++Index)
	{
		FCataclysmDamageNumber Number;
		Number.Text = FString::Printf(TEXT("%d"), Index);
		Display->AddDamageNumber(Number);
	}

	TestEqual(TEXT("no more than the ceiling are kept"),
		Display->NumbersWaiting(), Ceiling);

	// AND WHICH ONES SURVIVED, which is the whole rule. Dropping the newest
	// leaves exactly the same count behind, so a test that only counts would
	// pass with the behaviour reversed. The ten numbered 0 to 9 are the ones
	// that should have gone, leaving 10 at the front and the last one at the
	// back.
	const TArray<FCataclysmDamageNumber>& Kept = Display->NumbersWaitingList();
	if (TestEqual(TEXT("the list really holds the ceiling"), Kept.Num(),
				  Ceiling))
	{
		TestEqual(TEXT("the oldest survivor is the eleventh number added"),
			Kept[0].Text, FString(TEXT("10")));
		TestEqual(TEXT("and the newest number added is still there"),
			Kept.Last().Text,
			FString::Printf(TEXT("%d"), Ceiling + 9));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayDropsFadedNumbers,
	"Cataclysm.Overlay.ANumberIsForgottenOnceItHasFinishedFading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayDropsFadedNumbers::RunTest(const FString&)
{
	UWorld* World = CataclysmOverlayTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmHUD* Display = World->SpawnActor<ACataclysmHUD>();
	if (!TestNotNull(TEXT("a heads-up display"), Display))
	{
		return false;
	}

	const float Lifetime = UCataclysmCombatOverlay::NumberLifetimeSeconds;

	FCataclysmDamageNumber Old;
	Old.Text = TEXT("old");
	Old.StartedAt = 0.0f;

	FCataclysmDamageNumber Fresh;
	Fresh.Text = TEXT("fresh");
	Fresh.StartedAt = Lifetime;

	Display->AddDamageNumber(Old);
	Display->AddDamageNumber(Fresh);
	TestEqual(TEXT("both are waiting to begin with"),
		Display->NumbersWaiting(), 2);

	// A moment after the old one's lifetime is up and well within the fresh
	// one's. Without this the list grows for as long as the session runs.
	Display->DropExpired(Lifetime + 0.01f);
	TestEqual(TEXT("the expired one is forgotten and the live one is not"),
		Display->NumbersWaiting(), 1);

	Display->DropExpired(Lifetime * 3.0f);
	TestEqual(TEXT("and eventually both are"), Display->NumbersWaiting(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayRecordSurvivesNoDisplay,
	"Cataclysm.Overlay.RecordingAHitInAWorldWithNoDisplayDoesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayRecordSurvivesNoDisplay::RunTest(const FString&)
{
	// EVERY AUTOMATION TEST AND EVERY DEDICATED SERVER TAKES THIS PATH. A world
	// built by UWorld::CreateWorld has no local player controller, so there is
	// no heads-up display to hand the number to. Every hit resolved in a test
	// world reaches this, so if it were not safe the whole damage suite would
	// crash rather than one test.
	UWorld* World = CataclysmOverlayTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Enemy =
		CataclysmOverlayTest::SpawnEnemy(World, 549.0f);
	if (!TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	UCataclysmCombatOverlay::Record(Enemy, CataclysmOverlayTest::OrdinaryHit(),
									CataclysmOverlayTest::Landed(42.0f));
	UCataclysmCombatOverlay::Record(nullptr,
									CataclysmOverlayTest::OrdinaryHit(),
									CataclysmOverlayTest::Landed(42.0f));

	TestTrue(TEXT("recording a hit with nowhere to draw it is safe"), true);

	return true;
}

// --------------------------------------------------------------------------
// Marking a critical strike
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayMarksACriticalStrike,
	"Cataclysm.Overlay.ACriticalStrikeIsDrawnLargerAndItsFigureIsMarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayMarksACriticalStrike::RunTest(const FString&)
{
	// COLOUR AND SIZE, AND NOT THE TEXT. Issue #649 marked a critical strike by
	// size and by an exclamation mark on the figure, deliberately leaving colour
	// alone because colour said where the damage went. The project owner played
	// that on 2026-08-17 and reported "you really can't tell the difference
	// between a crit and a normal hit even though it's slightly bigger and has an
	// exclamation point". The mark is gone and colour took the job. Issue #668.
	FCataclysmDamageResult Crit = CataclysmOverlayTest::Landed(1'234.0f);
	Crit.bWasCritical = true;

	const FCataclysmDamageResult Ordinary =
		CataclysmOverlayTest::Landed(1'234.0f);

	// THE FIGURE READS THE SAME EITHER WAY. A number that says 1234 for an
	// ordinary hit must say 1234 for a critical one, so the only thing a player
	// compares between two numbers is how much damage each did.
	TestEqual(TEXT("an ordinary hit prints its figure alone"),
		UCataclysmCombatOverlay::TextFor(Ordinary), FString(TEXT("1234")));
	TestEqual(TEXT("and a critical strike prints exactly the same figure"),
		UCataclysmCombatOverlay::TextFor(Crit), FString(TEXT("1234")));

	const FLinearColor CritColour = UCataclysmCombatOverlay::ColourFor(Crit);
	const FLinearColor OrdinaryColour =
		UCataclysmCombatOverlay::ColourFor(Ordinary);

	TestFalse(TEXT("a critical strike is a different colour from an ordinary hit"),
		CritColour.Equals(OrdinaryColour));
	TestTrue(TEXT("and it is the critical strike colour"),
		CritColour.Equals(UCataclysmCombatOverlay::ColourFromHex(
			UCataclysmCombatOverlay::CriticalStrikeHex)));

	// IT IS WARM, which is the whole point of the choice. A critical strike that
	// came out cool or grey would read as one of the other two outcomes.
	TestTrue(TEXT("the critical strike colour is warm rather than cool"),
		CritColour.R > CritColour.B);

	const float CritScale = UCataclysmCombatOverlay::ScaleFor(
		CataclysmOverlayTest::OrdinaryHit(), Crit);
	const float OrdinaryScale = UCataclysmCombatOverlay::ScaleFor(
		CataclysmOverlayTest::OrdinaryHit(), Ordinary);

	TestTrue(TEXT("and it is still drawn larger"), CritScale > OrdinaryScale);
	TestEqual(TEXT("by the stated multiple"),
		CritScale,
		OrdinaryScale * UCataclysmCombatOverlay::CriticalStrikeScale, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayTextCarriesNoMark,
	"Cataclysm.Overlay.NoNumberCarriesAnExclamationMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayTextCarriesNoMark::RunTest(const FString&)
{
	// A GUARD AGAINST PUTTING IT BACK. The exclamation mark was built under
	// issue #649, played, and reported as not readable. Anything that reintroduces
	// punctuation into a damage figure should have to delete this test and say
	// why, rather than doing it by accident while editing the printf formats.
	FCataclysmDamageResult Crit;
	Crit.DealtToHealth = 12.0f;
	Crit.AbsorbedByShield = 30.0f;
	Crit.bWasCritical = true;

	TestEqual(TEXT("a critical strike through a shield prints both figures only"),
		UCataclysmCombatOverlay::TextFor(Crit), FString(TEXT("12 (+30)")));

	FCataclysmDamageResult ShieldOnly;
	ShieldOnly.AbsorbedByShield = 30.0f;
	ShieldOnly.bWasCritical = true;

	TestEqual(TEXT("and one a shield swallowed prints its figure only"),
		UCataclysmCombatOverlay::TextFor(ShieldOnly), FString(TEXT("30")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayMarksBothFigures,
	"Cataclysm.Overlay.ACriticalStrikeThroughAShieldIsMarkedOnceAtTheEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayMarksBothFigures::RunTest(const FString&)
{
	// A blow that stripped a shield and still reached health prints both figures.
	// THE MARK GOES ON THE END, because the whole blow was the critical strike
	// rather than either half of it.
	FCataclysmDamageResult Both;
	Both.DealtToHealth = 12.0f;
	Both.AbsorbedByShield = 30.0f;
	Both.bWasCritical = true;

	TestEqual(TEXT("both figures, health first"),
		UCataclysmCombatOverlay::TextFor(Both), FString(TEXT("12 (+30)")));

	// THESE TWO FIGURES ARE WHAT PAYS FOR THE CRITICAL STRIKE COLOUR. Colour used
	// to separate a hit that reached health from one a shield absorbed; for a
	// critical strike it now says "critical strike" instead, so the text is the
	// only thing left that separates them. Issue #668.
	FCataclysmDamageResult ShieldOnly;
	ShieldOnly.AbsorbedByShield = 30.0f;
	ShieldOnly.bWasCritical = true;

	TestEqual(TEXT("a critical strike a shield swallowed prints one figure"),
		UCataclysmCombatOverlay::TextFor(ShieldOnly), FString(TEXT("30")));

	TestTrue(TEXT("and the two are told apart by the text, not the colour"),
		UCataclysmCombatOverlay::TextFor(Both)
			!= UCataclysmCombatOverlay::TextFor(ShieldOnly));
	TestTrue(TEXT("because both are drawn in the critical strike colour"),
		UCataclysmCombatOverlay::ColourFor(Both).Equals(
			UCataclysmCombatOverlay::ColourFor(ShieldOnly)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmOverlayNeverMarksAHitThatDidNothing,
	"Cataclysm.Overlay.ACriticalStrikeThatGotThroughNothingIsNotDrawnAsOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmOverlayNeverMarksAHitThatDidNothing::RunTest(const FString&)
{
	// THE ROLL HAPPENS BEFORE BLOCK, ARMOUR AND RESISTANCE, so a critical strike
	// can be stopped dead by a well-defended target. Three words say a hit did
	// nothing and none of them should be dressed up as a hit that did something:
	// an orange, oversized "Evaded" says the opposite of what happened.
	FCataclysmDamageResult Stopped;
	Stopped.bWasCritical = true;

	TestEqual(TEXT("a wholly mitigated critical strike is a plain zero"),
		UCataclysmCombatOverlay::TextFor(Stopped), FString(TEXT("0")));

	FCataclysmDamageResult Evaded;
	Evaded.bEvaded = true;
	Evaded.bWasCritical = true;

	TestEqual(TEXT("an evaded one says only that it was evaded"),
		UCataclysmCombatOverlay::TextFor(Evaded), FString(TEXT("Evaded")));

	FCataclysmDamageResult BlockedToNothing;
	BlockedToNothing.bBlocked = true;
	BlockedToNothing.bWasCritical = true;

	TestEqual(TEXT("and one blocked to nothing says only that"),
		UCataclysmCombatOverlay::TextFor(BlockedToNothing),
		FString(TEXT("Blocked")));

	// AND NONE OF THE THREE IS DRAWN LARGER OR ORANGE. The size and the colour
	// ask the same question, so they can never disagree on the same number.
	const FCataclysmIncomingHit Hit = CataclysmOverlayTest::OrdinaryHit();
	const FLinearColor CritColour = UCataclysmCombatOverlay::ColourFromHex(
		UCataclysmCombatOverlay::CriticalStrikeHex);

	TestEqual(TEXT("a stopped critical strike is drawn at ordinary size"),
		UCataclysmCombatOverlay::ScaleFor(Hit, Stopped), 1.0f, 0.001f);
	TestEqual(TEXT("and so is an evaded one"),
		UCataclysmCombatOverlay::ScaleFor(Hit, Evaded), 1.0f, 0.001f);

	TestFalse(TEXT("a stopped critical strike is not drawn in the crit colour"),
		UCataclysmCombatOverlay::ColourFor(Stopped).Equals(CritColour));
	TestFalse(TEXT("nor is an evaded one"),
		UCataclysmCombatOverlay::ColourFor(Evaded).Equals(CritColour));
	TestFalse(TEXT("nor is one blocked to nothing"),
		UCataclysmCombatOverlay::ColourFor(BlockedToNothing).Equals(CritColour));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
