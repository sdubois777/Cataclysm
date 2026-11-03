// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Issue #783: an ordinary attack's damage lands when its swing connects.
 *
 * WHAT WAS WRONG. Four creatures' `AttackTarget` dealt the blow at once and then
 * started the swing clip, so the player was hurt 0.17 to 0.33 seconds before the
 * weapon arrived -- measured 2026-08-21 under issue #526. Since this issue the
 * clip starts when the attack is ordered and the damage waits for the clip's
 * measured strike divided by the rate it is played at.
 *
 * WHAT THESE GUARD, ruled 2026-11-02 by the coordinating session:
 *
 *   - nothing is dealt before the strike moment, and the whole blow after it,
 *     for the Brute, the Imp and the Abyssal Warden;
 *   - the Hellhound, whose bite could not be measured, still lands at once;
 *   - the Imp's two unmeasured swipes wait for the mean of its three measured;
 *   - a creature that dies, or is stunned, in the gap lands nothing;
 *   - a target that walks out of reach in the gap is missed, and a strike
 *     ordered at a target already beyond reach -- the Staff's Compel -- is not,
 *     nor is any strike a rule orders (Echoes of the Past, Compel);
 *   - the creature's own Tick is what lands the blow in play.
 *
 * NO WORLD IS TICKED. A world built by `UWorld::CreateWorld` never is, so each
 * test moves the world clock by hand and calls `LandPendingBasicAttack`, which
 * is what `ACataclysmEnemyCharacter::Tick` calls every frame. One test calls
 * Tick directly, the way `Cataclysm.EnemyDeath` already does, so the hook is
 * reached as well as the function.
 *
 * WITHOUT THE PARAGON ART THE TIMING IS THE SAME. No clip plays, which is read
 * as the authored speed, and every one of these clips plays at exactly 1 at its
 * designed interval. Only the Warden test that waits for a swing still playing
 * needs the art, and it reports the half it could not check.
 */

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

namespace CataclysmEnemyHitTimingTest
{
	/** What every swinger is given, so a blow is unmistakable. */
	constexpr float SwingerDamage = 100.0f;

	/** How far either side of a strike moment the clock is put. */
	constexpr double OneHundredth = 0.01;

	/** Inside every one of these creatures' 90 cm reach, and the Imp's 132. */
	constexpr float InReachCm = 80.0f;

	template <typename TCreature>
	TCreature* SpawnSwinger(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TCreature* Creature = World->SpawnActor<TCreature>(
			TCreature::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator,
			Params);
		if (Creature)
		{
			Creature->SetAttackDamage(SwingerDamage);
		}
		return Creature;
	}

	/**
	 * What is struck: a plain creature on the players' side, so a creature's blow
	 * may land on it, with nothing that could make a blow miss or be soaked.
	 */
	static ACataclysmEnemyCharacter* SpawnStruck(UWorld* World, float DistanceCm)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmEnemyCharacter* Struck = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(DistanceCm, 0.0f, 0.0f),
			FRotator::ZeroRotator, Params);
		if (!Struck)
		{
			return nullptr;
		}
		Struck->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Struck->SetHealth(1000000.0f);
		if (UAbilitySystemComponent* System = Struck->GetAbilitySystemComponent())
		{
			System->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			System->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			System->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
		}
		return Struck;
	}

	static float StruckHealth(const ACataclysmEnemyCharacter* Struck)
	{
		const UAbilitySystemComponent* System =
			Struck ? Struck->GetAbilitySystemComponent() : nullptr;
		return System
			? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
			: 0.0f;
	}

	static void MoveTheClock(UWorld* World, double Seconds)
	{
		World->TimeSeconds += Seconds;
	}

	/** How long from now until the creature's waiting blow is due. */
	static float SecondsUntilTheBlow(const UWorld* World,
									 const ACataclysmEnemyCharacter* Swinger)
	{
		return Swinger->PendingBasicAttackLandsAtSeconds
			- static_cast<float>(World->GetTimeSeconds());
	}

	/**
	 * The whole of one creature's timing: nothing before its strike, all of it
	 * after, and exactly the blow it used to deal at once.
	 *
	 * @param StrikeSeconds  what the blow should wait, worked out by the caller
	 *                       from the creature's own constants
	 */
	static void CheckTheBlowWaitsForTheStrike(FAutomationTestBase& Test,
											  UWorld* World,
											  ACataclysmEnemyCharacter* Swinger,
											  ACataclysmEnemyCharacter* Struck,
											  float Full, float StrikeSeconds)
	{
		Test.TestEqual(TEXT("the swing deals nothing when it starts"),
			StruckHealth(Struck), Full);
		Test.TestTrue(TEXT("and leaves its blow waiting"),
			Swinger->HasPendingBasicAttack());
		Test.TestEqual(TEXT("due when the clip strikes, divided by the rate it plays at"),
			SecondsUntilTheBlow(World, Swinger), StrikeSeconds, 0.001f);

		MoveTheClock(World, StrikeSeconds - OneHundredth);
		Test.TestFalse(TEXT("a hundredth of a second before it is due, nothing lands"),
			Swinger->LandPendingBasicAttack());
		Test.TestEqual(TEXT("and the target is untouched"), StruckHealth(Struck), Full);

		MoveTheClock(World, 2.0 * OneHundredth);
		Test.TestTrue(TEXT("a hundredth of a second after, it lands"),
			Swinger->LandPendingBasicAttack());
		const float Dealt = Full - StruckHealth(Struck);
		Test.TestTrue(FString::Printf(TEXT("and takes health (%.2f)"), Dealt), Dealt > 0.0f);
		Test.TestFalse(TEXT("and nothing waits any more"), Swinger->HasPendingBasicAttack());

		// THE SAME BLOW IT DEALT AT ONCE BEFORE ISSUE #783, called by name. Ruled:
		// only the moment changes, not the figure.
		const float BeforeAtOnce = StruckHealth(Struck);
		Swinger->ACataclysmEnemyCharacter::AttackTarget(Struck);
		const float AtOnce = BeforeAtOnce - StruckHealth(Struck);
		Test.TestEqual(TEXT("the blow is the one it used to deal at once"),
			Dealt, AtOnce, 0.01f);
	}
}

CATACLYSM_TEST(FCataclysmHitTimingDelayArithmeticTest,
	"Cataclysm.EnemyHitTiming.TheWaitIsTheStrikeDividedByTheRateTheClipPlaysAt")
{
	using Enemy = ACataclysmEnemyCharacter;

	TestEqual(TEXT("at the authored speed, the strike itself"),
		Enemy::BasicAttackStrikeDelaySeconds(0.331f, 1.0f), 0.331f, 0.0001f);
	TestEqual(TEXT("played twice as fast, half of it"),
		Enemy::BasicAttackStrikeDelaySeconds(0.331f, 2.0f), 0.1655f, 0.0001f);
	TestEqual(TEXT("no clip played reads as the authored speed, so no art keeps the moment"),
		Enemy::BasicAttackStrikeDelaySeconds(0.331f, 0.0f), 0.331f, 0.0001f);
	TestEqual(TEXT("a strike nobody measured waits for nothing"),
		Enemy::BasicAttackStrikeDelaySeconds(0.0f, 1.0f), 0.0f);
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingBruteTest,
	"Cataclysm.EnemyHitTiming.TheBrutesSwingDealsNothingUntilItConnectsAndAllOfItThen")
{
	using namespace CataclysmEnemyHitTimingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnSwinger<ACataclysmBruteCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Brute"), Brute) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Brute->AttackTarget(Struck);

	// THE SWING IS GIVEN NO WINDOW, so it plays at exactly 1 with the art and
	// nothing plays without it, which reads as 1.
	CheckTheBlowWaitsForTheStrike(*this, World, Brute, Struck, Full,
		ACataclysmBruteCharacter::SwingStrikeSeconds);
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingImpTest,
	"Cataclysm.EnemyHitTiming.TheImpsSwipeDealsNothingUntilItConnectsAndAllOfItThen")
{
	using namespace CataclysmEnemyHitTimingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmImpCharacter* Imp = SpawnSwinger<ACataclysmImpCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("an Imp"), Imp) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Imp->AttackTarget(Struck);

	// WHICH SWIPE IT DREW DECIDES THE WAIT, and it draws one with or without the
	// art. The rate is what the clip really played at, or 1 with nothing played.
	if (!TestTrue(FString::Printf(TEXT("a swipe was drawn (%d)"), Imp->LastRendDrawn),
			Imp->LastRendDrawn >= 0
				&& Imp->LastRendDrawn < ACataclysmImpCharacter::RendAnimationCount))
	{
		return false;
	}
	const float Rate = Imp->LastPlayedRate > 0.0f ? Imp->LastPlayedRate : 1.0f;
	CheckTheBlowWaitsForTheStrike(*this, World, Imp, Struck, Full,
		ACataclysmImpCharacter::RendStrikeSecondsFor(Imp->LastRendDrawn) / Rate);
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingWardenTest,
	"Cataclysm.EnemyHitTiming.TheWardensSwingDealsNothingUntilItsLeftSwingConnectsAndAllOfItThen")
{
	using namespace CataclysmEnemyHitTimingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnSwinger<ACataclysmAbyssalWardenCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Warden"), Warden) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Warden->AttackTarget(Struck);

	CheckTheBlowWaitsForTheStrike(*this, World, Warden, Struck, Full,
		ACataclysmAbyssalWardenCharacter::LeftSwingStrikeSeconds);
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingHellhoundTest,
	"Cataclysm.EnemyHitTiming.TheHellhoundsUnmeasuredBiteStillLandsAtOnce")
{
	using namespace CataclysmEnemyHitTimingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmHellhoundCharacter* Hellhound =
		SpawnSwinger<ACataclysmHellhoundCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Hellhound"), Hellhound) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	// RULED 2026-11-02: no figure is invented for a clip that could not be read.
	TestEqual(TEXT("its bite's strike is recorded as unmeasured"),
		ACataclysmHellhoundCharacter::MaulStrikeSeconds, 0.0f);

	const float Full = StruckHealth(Struck);
	Hellhound->AttackTarget(Struck);
	TestTrue(FString::Printf(TEXT("the bite takes health in the same call (%.2f to %.2f)"),
		Full, StruckHealth(Struck)), StruckHealth(Struck) < Full);
	TestFalse(TEXT("and leaves nothing waiting"), Hellhound->HasPendingBasicAttack());
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingImpMeanTest,
	"Cataclysm.EnemyHitTiming.TheImpsUnmeasuredSwipesWaitForTheMeanOfTheMeasuredThree")
{
	using Imp = ACataclysmImpCharacter;

	// THE ORDER OF RendAnimationNames: A, B, C, D, E. B and C are not measured.
	TestEqual(TEXT("Attack_A_SetA waits for its own 0.242"),
		Imp::RendStrikeSecondsFor(0), Imp::RendAStrikeSeconds);
	TestEqual(TEXT("Attack_D_SetA waits for its own 0.278"),
		Imp::RendStrikeSecondsFor(3), Imp::RendDStrikeSeconds);
	TestEqual(TEXT("Attack_E_SetA waits for its own 0.308"),
		Imp::RendStrikeSecondsFor(4), Imp::RendEStrikeSeconds);

	const float Mean = (0.242f + 0.278f + 0.308f) / 3.0f;
	TestEqual(TEXT("the unmeasured figure is the mean of the measured three, 0.276"),
		Imp::RendUnmeasuredStrikeSeconds, Mean, 0.0001f);
	TestEqual(TEXT("Attack_B_SetA, unmeasured, waits for the mean"),
		Imp::RendStrikeSecondsFor(1), Imp::RendUnmeasuredStrikeSeconds);
	TestEqual(TEXT("Attack_C_SetA, unmeasured, waits for the mean"),
		Imp::RendStrikeSecondsFor(2), Imp::RendUnmeasuredStrikeSeconds);

	TestEqual(TEXT("an index that is no swipe waits for nothing"),
		Imp::RendStrikeSecondsFor(Imp::RendAnimationCount), 0.0f);
	TestEqual(TEXT("nor does one below the first"), Imp::RendStrikeSecondsFor(-1), 0.0f);
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingDiedFirstTest,
	"Cataclysm.EnemyHitTiming.ACreatureThatDiesBeforeItsSwingConnectsDealsNothing")
{
	using namespace CataclysmEnemyHitTimingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnSwinger<ACataclysmBruteCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Brute"), Brute) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Brute->AttackTarget(Struck);

	// MARKED DEAD RATHER THAN KILLED, so the only thing that can refuse the blow
	// is `LandPendingBasicAttack` asking. `HandleDeath` also forgets it, and that
	// second line is what a running game reaches first.
	if (!TestTrue(TEXT("set-up: it dies in the gap"), UCataclysmSkillEffects::MarkDead(Brute)))
	{
		return false;
	}
	MoveTheClock(World, ACataclysmBruteCharacter::SwingStrikeSeconds + OneHundredth);

	TestFalse(TEXT("its swing's moment comes and nothing is landed"),
		Brute->LandPendingBasicAttack());
	TestEqual(TEXT("and the target is untouched"), StruckHealth(Struck), Full);
	TestFalse(TEXT("and the blow is gone rather than held"), Brute->HasPendingBasicAttack());
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingStunnedFirstTest,
	"Cataclysm.EnemyHitTiming.ACreatureStunnedBeforeItsSwingConnectsDealsNothing")
{
	using namespace CataclysmEnemyHitTimingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnSwinger<ACataclysmBruteCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Brute"), Brute) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Brute->AttackTarget(Struck);

	if (!TestTrue(TEXT("set-up: it is stunned in the gap"),
			UCataclysmSkillEffects::ApplyStun(Struck, Brute, /*DurationSeconds=*/1.5f,
				/*DamageDealt=*/1000.0f, /*bStunIsDesigned=*/true)))
	{
		return false;
	}
	MoveTheClock(World, ACataclysmBruteCharacter::SwingStrikeSeconds + OneHundredth);

	TestFalse(TEXT("its swing's moment comes and nothing is landed"),
		Brute->LandPendingBasicAttack());
	TestEqual(TEXT("and the target is untouched"), StruckHealth(Struck), Full);
	TestFalse(TEXT("and the blow is gone rather than held for the stun to end"),
		Brute->HasPendingBasicAttack());
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingWalkedOutTest,
	"Cataclysm.EnemyHitTiming.ATargetThatWalksOutOfReachBeforeTheSwingConnectsIsMissed")
{
	using namespace CataclysmEnemyHitTimingTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnSwinger<ACataclysmBruteCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Brute"), Brute) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Brute->AttackTarget(Struck);

	// THREE METRES OUT, which a player walking at 3.5 m/s covers in well under the
	// swing's third of a second from where it stood. The Gatekeeper's sweep and
	// the Succubus's bolt land where they were aimed, and so does this.
	Struck->SetActorLocation(FVector(300.0f, 0.0f, Struck->GetActorLocation().Z));
	MoveTheClock(World, ACataclysmBruteCharacter::SwingStrikeSeconds + OneHundredth);

	TestFalse(TEXT("the swing connects with nothing"), Brute->LandPendingBasicAttack());
	TestEqual(TEXT("and the target that walked out is untouched"), StruckHealth(Struck), Full);
	TestFalse(TEXT("and the blow is spent, not held for the target's return"),
		Brute->HasPendingBasicAttack());
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingBeyondReachTest,
	"Cataclysm.EnemyHitTiming.AStrikeOrderedAtATargetBeyondReachStillLands")
{
	using namespace CataclysmEnemyHitTimingTest;

	// THE STAFF'S COMPEL: "everything you command strikes that same enemy at
	// once, wherever it happens to be standing." It orders `AttackTarget` from
	// across the room, so a reach the strike never had cannot be what it misses
	// by. A JUDGEMENT, labelled so in docs/DECISIONS.md.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnSwinger<ACataclysmBruteCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, 1000.0f);
	if (!TestNotNull(TEXT("a Brute"), Brute) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Brute->AttackTarget(Struck);
	TestEqual(TEXT("ordered from ten metres, it still waits for the swing"),
		StruckHealth(Struck), Full);

	MoveTheClock(World, ACataclysmBruteCharacter::SwingStrikeSeconds + OneHundredth);
	TestTrue(TEXT("and lands when the swing connects"), Brute->LandPendingBasicAttack());
	TestTrue(FString::Printf(TEXT("on the target ten metres away (%.2f to %.2f)"),
		Full, StruckHealth(Struck)), StruckHealth(Struck) < Full);
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingRuleStrikeTest,
	"Cataclysm.EnemyHitTiming.AStrikeARuleOrdersLandsOnATargetThatWalkedOutOfReach")
{
	using namespace CataclysmEnemyHitTimingTest;

	// ECHOES OF THE PAST AND THE STAFF'S COMPEL ORDER THIS, and both strike a
	// target wherever it stands. An echo stands at its reach from where the
	// player was when it appeared, a second before it strikes, so a reach test
	// would let a player who stood still dodge it and never one who had walked
	// away. A JUDGEMENT, labelled so in docs/DECISIONS.md. The same walk-out as
	// `ATargetThatWalksOutOfReachBeforeTheSwingConnectsIsMissed`, with the
	// opposite answer.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnSwinger<ACataclysmBruteCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Brute"), Brute) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Brute->StrikeAsARuleOrders(Struck);
	TestEqual(TEXT("a rule's strike still deals nothing when the swing starts"),
		StruckHealth(Struck), Full);
	TestTrue(TEXT("and waits for the swing"), Brute->HasPendingBasicAttack());

	Struck->SetActorLocation(FVector(300.0f, 0.0f, Struck->GetActorLocation().Z));
	MoveTheClock(World, ACataclysmBruteCharacter::SwingStrikeSeconds + OneHundredth);

	TestTrue(TEXT("and lands when the swing connects"), Brute->LandPendingBasicAttack());
	TestTrue(FString::Printf(TEXT("on a target that walked out of reach in the gap (%.2f to %.2f)"),
		Full, StruckHealth(Struck)), StruckHealth(Struck) < Full);
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingTickTest,
	"Cataclysm.EnemyHitTiming.TheCreaturesOwnFrameLandsTheBlow")
{
	using namespace CataclysmEnemyHitTimingTest;

	// THE ONE TEST THAT REACHES THE HOOK RATHER THAN THE FUNCTION. Calling Tick
	// by hand is what a running world would do; `Cataclysm.EnemyDeath` does it
	// on this same creature.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnSwinger<ACataclysmAbyssalWardenCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Warden"), Warden) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Warden->AttackTarget(Struck);

	Warden->Tick(0.016f);
	TestEqual(TEXT("a frame before the swing connects lands nothing"),
		StruckHealth(Struck), Full);

	MoveTheClock(World,
		ACataclysmAbyssalWardenCharacter::LeftSwingStrikeSeconds + OneHundredth);
	Warden->Tick(0.016f);
	TestTrue(FString::Printf(TEXT("the frame after it connects lands the blow (%.2f to %.2f)"),
		Full, StruckHealth(Struck)), StruckHealth(Struck) < Full);
	TestFalse(TEXT("and nothing waits any more"), Warden->HasPendingBasicAttack());
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingSecondOrderTest,
	"Cataclysm.EnemyHitTiming.ASecondSwingOrderedBeforeTheFirstConnectsLandsTheFirst")
{
	using namespace CataclysmEnemyHitTimingTest;

	// ONLY A BUFFED OR CONSOLE-SHORTENED INTERVAL GETS HERE: the Brute's 1.2
	// seconds is far longer than its 0.331 second strike. A blow landed a little
	// early is better than one the player was owed and never received.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmBruteCharacter* Brute = SpawnSwinger<ACataclysmBruteCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Brute"), Brute) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	const float Full = StruckHealth(Struck);
	Brute->AttackTarget(Struck);
	Brute->AttackTarget(Struck);
	const float AfterTheSecondOrder = StruckHealth(Struck);
	TestTrue(FString::Printf(TEXT("the second order lands the first blow (%.2f to %.2f)"),
		Full, AfterTheSecondOrder), AfterTheSecondOrder < Full);
	TestTrue(TEXT("and its own waits for its swing"), Brute->HasPendingBasicAttack());

	MoveTheClock(World, ACataclysmBruteCharacter::SwingStrikeSeconds + OneHundredth);
	TestTrue(TEXT("which lands when it connects"), Brute->LandPendingBasicAttack());
	TestTrue(TEXT("so both blows were dealt"), StruckHealth(Struck) < AfterTheSecondOrder);
	return true;
}

CATACLYSM_TEST(FCataclysmHitTimingWardenStillPlayingTest,
	"Cataclysm.EnemyHitTiming.TheWardensBlowWaitsForASwingThatHasNotStartedYet")
{
	using namespace CataclysmEnemyHitTimingTest;

	// A BUFFED WARDEN ATTACKS MORE OFTEN THAN ITS 2.1 SECOND COMBO TAKES, and
	// `UpdateLoopingAnimation` then starts the new left swing only when the clip
	// still playing ends. The damage waits for that as well.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmAbyssalWardenCharacter* Warden =
		SpawnSwinger<ACataclysmAbyssalWardenCharacter>(World);
	ACataclysmEnemyCharacter* Struck = SpawnStruck(World, InReachCm);
	if (!TestNotNull(TEXT("a Warden"), Warden) || !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}
	Warden->ResolveBody(/*bIncludeAnimation=*/true);

	// HALF A SECOND OF SOME EARLIER CLIP STILL TO RUN.
	constexpr float StillPlayingSeconds = 0.5f;
	Warden->OneShotEndsAtSeconds =
		static_cast<float>(World->GetTimeSeconds()) + StillPlayingSeconds;

	const bool bSwingWaits = Warden->LeftSwingAnimation && !Warden->bAnimationBlueprintBound;
	Warden->AttackTarget(Struck);

	const float Left = ACataclysmAbyssalWardenCharacter::LeftSwingStrikeSeconds;
	if (!bSwingWaits)
	{
		// WITHOUT THE ART NOTHING PLAYS, SO NOTHING IS WAITED FOR, and the blow
		// keeps the moment a checkout with the art gives an unbuffed Warden.
		TestEqual(TEXT("with no swing to wait for, the blow waits for the strike alone"),
			SecondsUntilTheBlow(World, Warden), Left, 0.001f);
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("The Paragon Grux pack is absent, or an animation Blueprint is bound, so "
				 "no left swing waited for a clip still playing. Install the pack to "
				 "check the blow waits for it too."));
		return true;
	}

	TestEqual(TEXT("the blow waits for the clip still playing, then for the left swing's strike"),
		SecondsUntilTheBlow(World, Warden), StillPlayingSeconds + Left, 0.001f);
	return true;
}

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
