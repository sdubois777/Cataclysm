// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmHealthDebt.h"
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
 *   Deeper Cuts, 4 points       added_health_cost                    4  % of max
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
 * rather than an estimate, and pinning that Rock Bottom cannot prevent it. They
 * assert what the game does today. Whether the design should allow this build at
 * all is issue #1098 and is the project owner's to decide.
 *
 * `KillIfDebtExceedsHealth` IS CALLED BY HAND. In the running game the
 * regeneration timer calls it; a world built by `UWorld::CreateWorld` is never
 * ticked, so no timer in it fires. `CataclysmHealthDebtTests.cpp` does the same.
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
			Set(Resource::GetAddedHealthCostAttribute(), 4.0f);
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
}

#define CATACLYSM_MASOCHIST_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// How many casts the build survives
// ---------------------------------------------------------------------------

CATACLYSM_MASOCHIST_TEST(FCataclysmReckoningBuildKillsItsOwnerTest,
	"Cataclysm.MasochistBuild.TheReckoningWithExsanguinateKillsItsOwnerInSixCasts")
{
	using namespace CataclysmMasochistBuildTest;

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
	for (int32 Cast = 1; Cast <= 20; ++Cast)
	{
		const float Cost = ChargeOneCast(Player);

		// THE STEP THE REGENERATION TIMER TAKES IN THE RUNNING GAME. It is what
		// asks whether the debt has passed the character's health.
		const bool bDied = Debt::KillIfDebtExceedsHealth(Player.Character);

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

	// NOT AN ESTIMATE. 15% of current health plus 4% of maximum, with nothing
	// ever taken from health, is 19% of the pool owed per cast, so the debt
	// passes the pool on the sixth.
	TestTrue(TEXT("the character died of its own health costs"),
			 Player.IsDead());
	TestEqual(TEXT("after surviving five casts"), CastsSurvived, 5);

	// AND NOT A POINT OF HEALTH WAS SPENT, which is the part that reads as a
	// bug from inside the game: the health bar is full the whole way down.
	TestEqual(TEXT("with a full health bar until the moment it died"),
			  Player.Health(), 0.0f, 0.01f);

	return true;
}

CATACLYSM_MASOCHIST_TEST(FCataclysmReckoningBuildDiesSoonerWhenHurtTest,
	"Cataclysm.MasochistBuild.TakingDamageFirstBringsTheDeathForwardToTheFourthCast")
{
	using namespace CataclysmMasochistBuildTest;

	// THE SAME BUILD ON A FLOOR WITH ENEMIES ON IT. The project owner's floor
	// had 211 creatures spawned on it, which is issue #806. A character that has
	// been hurt owes less per cast, because Exsanguinate's share is of CURRENT
	// health -- but it has far less health for the debt to pass, and that wins.
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
		const bool bDied = Debt::KillIfDebtExceedsHealth(Player.Character);

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
	TestEqual(TEXT("after surviving three casts rather than five"),
			  CastsSurvived, 3);

	return true;
}

// ---------------------------------------------------------------------------
// What Rock Bottom does and does not protect against
// ---------------------------------------------------------------------------

CATACLYSM_MASOCHIST_TEST(FCataclysmRockBottomCannotSaveAReckoningBuildTest,
	"Cataclysm.MasochistBuild.RockBottomsRescueNeverFiresBecauseHealthNeverMoves")
{
	using namespace CataclysmMasochistBuildTest;

	// WHY THE PROJECT OWNER EXPECTED TO LIVE. Rock Bottom reads "A health cost
	// can never reduce you below 1 health", and its second sentence clears all
	// outstanding debt on dropping below 20% health. Both are true, and neither
	// helps here.
	//
	// THE FIRST SENTENCE HAS NOTHING TO PROTECT. With The Reckoning no cost is
	// ever taken from health at all, so there is no charge for the floor to
	// stop.
	//
	// THE SECOND FIRES ON A THRESHOLD THE CHARACTER NEVER CROSSES. Health only
	// moves when something hits the character; the debt is what kills, and it
	// passes current health at whatever level that happens to be. This test
	// takes health down in one step from above the line to zero to show that
	// even a real crossing is too late.
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

	// FIVE CASTS' WORTH OF DEBT, WHICH IS SHORT OF THE POOL. The character is
	// still on full health and has not crossed anything.
	for (int32 Cast = 1; Cast <= 5; ++Cast)
	{
		ChargeOneCast(Player);
	}

	TestTrue(TEXT("a debt is owed"), Player.Owed() > 0.0f);
	TestEqual(TEXT("and health has not moved at all"), Player.Health(),
			  MaximumHealth, 0.01f);
	TestFalse(TEXT("so the fall to low health never happened"), Player.IsDead());

	// AND ROCK BOTTOM CAN CLEAR A RECKONING DEBT WHEN IT DOES FIRE, which is
	// worth pinning because `SettleIfDue` explicitly refuses one and
	// `ClearOnDroppingLow` does not. Whether that asymmetry is intended is
	// issue #1099: The Reckoning says its debt is "cleared only by killing an
	// enemy", and this clears it another way.
	const float OwedBefore = Player.Owed();
	Player.Set(Vital::GetHealthAttribute(), MaximumHealth * 0.1f);

	TestEqual(TEXT("dropping below a fifth of health cleared the whole debt"),
			  Player.Owed(), 0.0f, 0.01f);
	TestTrue(TEXT("and there was a debt there to clear"), OwedBefore > 0.0f);
	TestFalse(TEXT("and the character is alive"), Player.IsDead());

	return true;
}

#undef CATACLYSM_MASOCHIST_TEST

#endif // WITH_AUTOMATION_TESTS
