// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * How many skills a real Masochist build can use before it kills itself.
 *
 * WHY THIS FILE EXISTS. The project owner played a Masochist on 2026-08-31,
 * spent 111 passive points, used two or three abilities and died, and said they
 * did not think that should have been possible given the passives they chose.
 * The play session log could not answer it, because every message about a health
 * cost, a debt and a death by debt is logged at Verbose and dropped.
 *
 * THE CHARACTER IS ON DISK and this reproduces it from the record rather than
 * from a description:
 * `game/Saved/SaveGames/Character_66636788450254F335DDC2B687084685.sav`, a
 * Masochist at level 100 with 29 nodes allocated and no attribute points spent.
 * The five that decide what happens below, with the attribute each writes read
 * off `game/Data/PassiveEffects.csv`:
 *
 *   Deeper Cuts, 4 points       added_health_cost                    1  % of max
 *   Exsanguinate, keystone      added_health_cost_of_current        15  % of current
 *   Deferred Payment, 5 points  deferred_health_cost_share          50  %
 *   The Reckoning, keystone     deferred_health_cost_share         100  %
 *                               health_debt_cleared_only_by_a_kill   1
 *   Rock Bottom, capstone 50    unpayable_health_cost_becomes_debt   1
 *                               debt_cleared_on_dropping_low         1
 *
 * WHAT THOSE COMBINE INTO. The deferred share sums to 150 and clamps at 100, so
 * NONE of a skill's cost is ever taken from health; all of it becomes debt. The
 * Reckoning then stops that debt ever falling due -- it is "cleared only by
 * killing an enemy" -- so it only accumulates. And The Reckoning's last sentence
 * is "If your debt ever exceeds your current health, you die."
 *
 * WHAT THESE TESTS ARE FOR. Counting the casts, so the number is a measured fact
 * rather than an estimate, and pinning what Rock Bottom does about it. Until
 * issue #1120 the answer to the second was "nothing"; it now buys the character
 * one escape, once every thirty seconds, and the third test below says so. They
 * assert what the game does today. Whether the design should allow this build at
 * all is issue #1098 and is the project owner's to decide.
 *
 * `DrainWhileDebtExceedsHealth` IS CALLED BY HAND. In the running game the
 * regeneration timer calls it; a world built by `UWorld::CreateWorld` is never
 * ticked, so no timer in it fires. `CataclysmHealthDebtTests.cpp` does the same.
 *
 * AND IT IS CALLED IN A LOOP, SINCE ISSUE #1120. A debt past current health
 * used to kill on the spot, so one call settled the matter; it now drains
 * health across five seconds, so the death arrives twenty quarter-second steps
 * later at the outside. `BleedUntilItStops` runs them.
 */
namespace CataclysmMasochistBuildTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Resource = UCataclysmClassResourceAttributeSet;
	using Debt = UCataclysmHealthDebt;

	/**
	 * The Masochist's maximum health at level 100, near enough for counting.
	 *
	 * `game/Data/ClassStats.csv` gives the Masochist 150 health at level 1 and
	 * 24 per level, and no maximum mana of its own, so it takes the Default
	 * line's 50 and 6. Water to Blood turns that whole mana pool into health.
	 * A round 3000 stands in for the sum here: what these tests count is how
	 * many casts a POOL survives, and that count is a ratio, so the exact pool
	 * changes nothing. Written as a round number so every share below is exact.
	 */
	constexpr float MaximumHealth = 3'000.0f;

	/** A real player character, possessed, the way the running game wires one. */
	struct FScopedMasochist
	{
		explicit FScopedMasochist(UWorld* World)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);

			if (!State || !Controller || !Character)
			{
				return;
			}

			Controller->SetPlayerState(State);
			Controller->Possess(Character);

			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(
				Character->GetAbilitySystemComponent());
			if (!AbilitySystem)
			{
				return;
			}

			// MAXIMUM BEFORE CURRENT, because the vital set clamps health to the
			// maximum, so raising the current value first would clamp it back
			// down to the placeholder the set starts at.
			Set(Vital::GetMaxHealthAttribute(), MaximumHealth);
			Set(Vital::GetHealthAttribute(), MaximumHealth);

			// NO MANA COST STANDS IN THE WAY. `CommitAbility` runs before
			// `PayHealthCost`, so a skill refused for want of mana would never
			// reach the health cost and these tests would count nothing.
			Set(Vital::GetMaxManaAttribute(), 100'000.0f);
			Set(Vital::GetManaAttribute(), 100'000.0f);

			Set(Resource::GetMaxClassResourceAttribute(), 100.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		bool IsUsable() const { return Character && AbilitySystem; }

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Get(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		float Health() const { return Get(Vital::GetHealthAttribute()); }
		float Owed() const { return Get(Resource::GetHealthOwedAttribute()); }
		bool IsDead() const { return UCataclysmSkillEffects::IsDead(Character); }

		/** Deeper Cuts at 4 points and Exsanguinate, the two cost nodes. */
		void TakeTheCostNodes() const
		{
			Set(Resource::GetAddedHealthCostAttribute(), 1.0f);
			Set(Resource::GetAddedHealthCostOfCurrentAttribute(), 15.0f);
		}

		/** Deferred Payment at 5 points and The Reckoning, which together defer
		 *  the whole cost and stop it ever falling due. */
		void TakeTheReckoning() const
		{
			Set(Resource::GetDeferredHealthCostShareAttribute(), 100.0f);
			Set(Resource::GetHealthDebtClearedOnlyByAKillAttribute(), 1.0f);
		}

		/** Rock Bottom's three rows. */
		void TakeRockBottom() const
		{
			Set(Resource::GetUnpayableHealthCostBecomesDebtAttribute(), 1.0f);
			Set(Resource::GetDebtClearedOnDroppingLowAttribute(), 1.0f);
			Set(Resource::GetFervourOnDroppingLowAttribute(), 50.0f);
		}

		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A skill with no cost of its own, so what it charges is the character's. */
	UCataclysmProjectileSkill* GrantAPlainSkill(const FScopedMasochist& Caster)
	{
		const FGameplayAbilitySpecHandle Handle =
			Caster.AbilitySystem->GiveAbilityInSlot(
				UCataclysmProjectileSkill::StaticClass(),
				ECataclysmAbilitySlot::Special, /*Level=*/100, Caster.Character);
		if (!Handle.IsValid())
		{
			return nullptr;
		}

		FGameplayAbilitySpec* Spec =
			Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmProjectileSkill* Instance =
			Spec ? Cast<UCataclysmProjectileSkill>(Spec->GetPrimaryInstance())
				 : nullptr;
		if (Instance)
		{
			Instance->SkillName = TEXT("A skill with no cost of its own");
			Instance->Params =
				UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Speed=0"));
		}
		return Instance;
	}

	/**
	 * Charge one skill's cost without going through an activation.
	 *
	 * WHY NOT ACTIVATE THE SKILL. A skill commits a cooldown, so a second
	 * activation in the same test is refused and the failure names the
	 * activation rather than the health. Counting casts needs many in a row.
	 * `PayHealthCost` is `protected`, so what a cast does to health is
	 * reproduced here from the same three pieces the real one uses, in the same
	 * order: the share of CURRENT health, the share of MAXIMUM health, and the
	 * whole lot deferred into debt.
	 *
	 * THIS IS A REPRODUCTION AND NOT THE REAL PATH, and that is stated rather
	 * than hidden. What it cannot catch is a mistake in `PayHealthCost` itself;
	 * `Cataclysm.Skills.*` and `Cataclysm.HealthWrite.*` cover that. What it is
	 * for is the arithmetic of a build over many casts, which no other test
	 * reaches.
	 */
	float ChargeOneCast(const FScopedMasochist& Caster)
	{
		const float Current = Caster.Health();
		const float Maximum = Caster.Get(Vital::GetMaxHealthAttribute());

		const float FromCurrent =
			Current * Caster.Get(Resource::GetAddedHealthCostOfCurrentAttribute())
			/ 100.0f;
		const float Added =
			Maximum * Caster.Get(Resource::GetAddedHealthCostAttribute()) / 100.0f;
		const float Cost = FromCurrent + Added;

		Debt::Defer(Caster.AbilitySystem, Cost);
		return Cost;
	}

	/**
	 * Run the regeneration step until the bleeding stops, and say whether it
	 * killed the character.
	 *
	 * WHY A LOOP AND NOT ONE CALL. Until issue #1120 a debt past current
	 * health killed on the spot, so one call was the whole story. It now
	 * drains health across `DrainSeconds` and the death arrives several steps
	 * later, so counting casts means running the steps between them.
	 *
	 * IT STOPS WHEN A STEP TAKES NOTHING, which is either that the debt no
	 * longer passes current health -- Rock Bottom clearing it is the way that
	 * happens -- or that the character is dead.
	 *
	 * THE CAP IS A BACKSTOP AND NOT A RULE. Five seconds of quarter-second
	 * steps is twenty; two hundred is far past any real answer, and a run
	 * that reached it would mean the drain was not draining.
	 */
	bool BleedUntilItStops(const FScopedMasochist& Player)
	{
		for (int32 Step = 0; Step < 200; ++Step)
		{
			const float Taken = Debt::DrainWhileDebtExceedsHealth(
				Player.Character, UCataclysmRegeneration::StepSeconds);
			if (Taken <= 0.0f)
			{
				break;
			}
		}
		return Player.IsDead();
	}
}

#define CATACLYSM_MASOCHIST_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// How many casts the build survives
// ---------------------------------------------------------------------------

CATACLYSM_MASOCHIST_TEST(FCataclysmReckoningBuildKillsItsOwnerTest,
	"Cataclysm.MasochistBuild.TheReckoningWithExsanguinateKillsItsOwnerInTwelveCasts")
{
	using namespace CataclysmMasochistBuildTest;

	// THE NUMBER IN THIS TEST'S NAME WAS SEVEN UNTIL ISSUE #1120, and the reason
	// it moved is the point of that issue. The debt still passes the pool on the
	// seventh cast -- that is asserted below and has not changed. What changed is
	// what happens next: the debt used to kill on the spot, and it now drains
	// health, which carries the character across Rock Bottom's threshold. That
	// clears the debt and the character lives to cast again.
	//
	// SO THIS BUILD NOW GETS ONE RESCUE AND THEN DIES. Twelve casts rather than
	// seven, and the extra five are bought by a capstone option that was worth
	// almost nothing before -- issue #1119 measured how little.

	// THE PROJECT OWNER'S BUILD, AT FULL HEALTH AND UNHURT. Nothing hits this
	// character; the only thing that happens is that it uses its own skills.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedMasochist Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	Player.TakeTheCostNodes();
	Player.TakeTheReckoning();
	Player.TakeRockBottom();

	int32 CastsSurvived = 0;
	int32 CastTheBleedingStarted = 0;
	float HealthWhenItStarted = 0.0f;

	for (int32 Cast = 1; Cast <= 20; ++Cast)
	{
		const float Cost = ChargeOneCast(Player);

		// WHETHER THE DEBT HAS PASSED THE CHARACTER'S HEALTH IS ASKED ON THE
		// REGENERATION STEP, and since issue #1120 the answer is a drain rather
		// than a death, so the steps have to be run to the end of it.
		const float Before = Player.Health();
		const bool bDied = BleedUntilItStops(Player);
		if (CastTheBleedingStarted == 0 && Player.Health() < Before)
		{
			CastTheBleedingStarted = Cast;
			HealthWhenItStarted = Before;
		}

		AddInfo(FString::Printf(
			TEXT("cast %d cost %.0f, health %.0f, owed %.0f%s"),
			Cast, Cost, Player.Health(), Player.Owed(),
			bDied ? TEXT(" -- DIED") : TEXT("")));

		if (bDied || Player.IsDead())
		{
			break;
		}
		CastsSurvived = Cast;
	}

	// THE DEBT PASSES THE POOL ON THE SEVENTH CAST, AND THAT IS UNCHANGED BY
	// ISSUE #1120. It is asserted separately from the death precisely so the two
	// cannot be confused: 15% of current health plus 1% of maximum, with nothing
	// ever taken from health, is 16% of the pool owed per cast.
	//
	// IT WAS THE SIXTH UNTIL ISSUE #1107, when Deeper Cuts went from 1% of
	// maximum health a point to 0.25%, taking this character's four points from
	// 4% of the pool a cast to 1%. One extra cast is all that bought a character
	// who pays nothing out of health at all, which is the narrowest case the
	// change helps: with The Reckoning the cost never leaves the health bar, so
	// only the debt slows down.
	TestEqual(TEXT("the debt passes the pool on the seventh cast"),
			  CastTheBleedingStarted, 7);

	// AND NOT A POINT OF HEALTH HAD BEEN SPENT BEFORE THAT MOMENT, which is the
	// part that reads as a bug from inside the game: the health bar is full
	// right up to the cast that starts the bleeding.
	TestEqual(TEXT("with a full health bar until then"), HealthWhenItStarted,
			  MaximumHealth, 0.01f);

	// AND THE CHARACTER STILL DIES OF ITS OWN SKILLS IN THE END. Rock Bottom
	// delays that; it does not prevent it, because the rescue is once every
	// thirty seconds and this test's clock never advances.
	TestTrue(TEXT("the character died of its own health costs"),
			 Player.IsDead());
	TestEqual(TEXT("after surviving eleven casts rather than the six it "
				   "survived before issue #1120"),
			  CastsSurvived, 11);

	TestEqual(TEXT("and it was at zero health when it died"),
			  Player.Health(), 0.0f, 0.01f);

	return true;
}

CATACLYSM_MASOCHIST_TEST(FCataclysmReckoningBuildDiesSoonerWhenHurtTest,
	"Cataclysm.MasochistBuild.TakingDamageFirstStillBringsTheDeathForward")
{
	using namespace CataclysmMasochistBuildTest;

	// THE SAME BUILD ON A FLOOR WITH ENEMIES ON IT. The project owner's floor
	// had 211 creatures spawned on it, which is issue #806. A character that has
	// been hurt owes less per cast, because Exsanguinate's share is of CURRENT
	// health -- but it has far less health for the debt to pass, and that wins.
	//
	// THIS TEST'S NAME NAMED A CAST NUMBER UNTIL ISSUE #1120 AND NO LONGER DOES.
	// What it is for is the COMPARISON: a hurt character dies sooner than an
	// unhurt one. Both counts moved when the debt became a drain, because Rock
	// Bottom now rescues the character once, and pinning the difference rather
	// than the absolute number is what stops the two tests drifting apart.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedMasochist Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	Player.TakeTheCostNodes();
	Player.TakeTheReckoning();
	Player.TakeRockBottom();

	// HURT TO A THIRD OF THE POOL BEFORE CASTING ANYTHING, which is an ordinary
	// state on a floor with anything on it at all.
	Player.Set(Vital::GetHealthAttribute(), MaximumHealth / 3.0f);

	int32 CastsSurvived = 0;
	for (int32 Cast = 1; Cast <= 20; ++Cast)
	{
		const float Cost = ChargeOneCast(Player);
		const bool bDied = BleedUntilItStops(Player);

		AddInfo(FString::Printf(
			TEXT("cast %d cost %.0f, health %.0f, owed %.0f%s"),
			Cast, Cost, Player.Health(), Player.Owed(),
			bDied ? TEXT(" -- DIED") : TEXT("")));

		if (bDied || Player.IsDead())
		{
			break;
		}
		CastsSurvived = Cast;
	}

	TestTrue(TEXT("the character died of its own health costs"),
			 Player.IsDead());
	TestEqual(TEXT("after surviving ten casts rather than the five it survived "
				   "before issue #1120"),
			  CastsSurvived, 10);

	// AND IT IS ONE FEWER THAN THE UNHURT CHARACTER GETS, WHICH IS WHAT THIS
	// TEST IS ACTUALLY FOR. Asserted against the other test's number rather than
	// left as two separate figures a reader has to compare by hand: if a later
	// change moved both, this still says whether being hurt is worse.
	constexpr int32 CastsSurvivedUnhurt = 11;
	TestTrue(TEXT("which is fewer than an unhurt character survives"),
			 CastsSurvived < CastsSurvivedUnhurt);

	return true;
}

// ---------------------------------------------------------------------------
// What Rock Bottom does and does not protect against
// ---------------------------------------------------------------------------

CATACLYSM_MASOCHIST_TEST(FCataclysmRockBottomSavesAReckoningBuildTest,
	"Cataclysm.MasochistBuild.BleedingOutCarriesHealthPastRockBottomsThreshold")
{
	using namespace CataclysmMasochistBuildTest;

	// THIS TEST USED TO ASSERT THE OPPOSITE, AND THAT IS THE POINT OF ISSUE
	// #1120. It was called `RockBottomsRescueNeverFiresBecauseHealthNeverMoves`
	// and it was right: with The Reckoning no cost was taken from health, the
	// debt killed the moment it passed current health, and Rock Bottom's rescue
	// watched a line the character never crossed. Issue #1119 measured what that
	// left the option worth.
	//
	// WHAT CHANGED. A Reckoning debt past current health now drains health
	// instead of killing on the spot, so health falls steadily and DOES cross a
	// fifth on the way down. That is what the project owner asked for on
	// 2026-08-31 -- "hit the trigger that's already there" -- and this is the
	// test that says it happened.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedMasochist Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	Player.TakeTheCostNodes();
	Player.TakeTheReckoning();
	Player.TakeRockBottom();

	// SEVEN CASTS, WHICH IS WHAT IT TAKES FOR THE DEBT TO PASS THE POOL. The
	// two tests above count that; here it is the starting position rather than
	// the thing being measured.
	for (int32 Cast = 1; Cast <= 7; ++Cast)
	{
		ChargeOneCast(Player);
	}

	// STILL ON FULL HEALTH AND STILL ALIVE. Nothing has been taken yet, which
	// is the state the old version of this test ended at.
	TestTrue(TEXT("the debt has passed the pool"), Player.Owed() > MaximumHealth);
	TestEqual(TEXT("and health has not moved at all yet"), Player.Health(),
			  MaximumHealth, 0.01f);
	TestFalse(TEXT("and the character is alive"), Player.IsDead());

	const float OwedBefore = Player.Owed();

	// NOW RUN THE STEPS THE REGENERATION TIMER WOULD RUN.
	const bool bDied = BleedUntilItStops(Player);

	// AND THE CHARACTER LIVED, WHICH IS THE WHOLE CHANGE. Health drained, it
	// crossed a fifth of the pool, `UCataclysmLowHealthRelief` fired, the debt
	// was cleared, and the bleeding stopped for want of anything to bleed for.
	TestFalse(TEXT("the character survived, where before this it died"), bDied);
	TestFalse(TEXT("and is not marked dead"), Player.IsDead());

	TestTrue(TEXT("there was a debt to clear"), OwedBefore > 0.0f);
	TestEqual(TEXT("and dropping below a fifth cleared the whole of it"),
			  Player.Owed(), 0.0f, 0.01f);

	// IT REALLY BLED, rather than the debt vanishing some other way. Health has
	// to have moved, or this would pass for a rescue that fired with no drain
	// at all.
	TestTrue(TEXT("health really came out on the way down"),
			 Player.Health() < MaximumHealth);

	// AND IT STOPPED AT OR BELOW A FIFTH AND ABOVE ZERO, which is what a rescue
	// on that threshold looks like: the crossing is what fired it, so health
	// cannot still be above the line, and the character is alive, so it cannot
	// be at zero.
	TestTrue(TEXT("it stopped at or below a fifth of the pool"),
			 Player.Health() <= MaximumHealth * 0.2f);
	TestTrue(TEXT("and above zero"), Player.Health() > 0.0f);

	// THE RESCUE IS ONCE EVERY THIRTY SECONDS, SO A SECOND DEBT IS NOT SURVIVED.
	// Without this the test would read as "The Reckoning is now safe", which is
	// not what was built: the keystone still kills, and Rock Bottom buys one
	// escape at a time. The world's clock does not advance in this test, so the
	// cooldown is still running.
	for (int32 Cast = 1; Cast <= 7; ++Cast)
	{
		ChargeOneCast(Player);
	}
	TestTrue(TEXT("a second debt past what health is left"),
			 Player.Owed() > Player.Health());

	TestTrue(TEXT("kills, because the rescue is still on its cooldown"),
			 BleedUntilItStops(Player));

	return true;
}

#undef CATACLYSM_MASOCHIST_TEST

#endif // WITH_AUTOMATION_TESTS
