// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For ECataclysmAbilitySlot, which says which slot a granted skill occupies.
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
 * What notices a health change that no gameplay effect caused.
 *
 * WHAT THESE GUARD. Issues #971 and #1072. Three routes lower a character's
 * health and only the first of them is a gameplay effect:
 *
 *   a blow                a gameplay effect on the Damage meta attribute
 *   a health cost         `UCataclysmSkillTemplate::PayHealthCost`
 *   a debt falling due    `UCataclysmHealthDebt::SettleIfDue`
 *
 * The two notifications that matter -- `NotifyIfHealthReachedZero`, which kills,
 * and `NotifyHealthChanged`, which is where a health threshold is noticed --
 * were reached only from `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`.
 * A blow reached them. The other two wrote the attribute directly and reached
 * nothing.
 *
 * WHAT THAT DID TO A REAL CHARACTER. The project owner played a Masochist on
 * 2026-08-31 holding Water to Blood, which makes every skill cost health, and
 * Rock Bottom, whose whole loop is falling below a fifth of maximum health. They
 * reported: "once I hit 0 hp it just lets me keep playing but without any
 * skills". That is exactly this: health emptied, no death, and every further
 * skill refused because it could not be paid for.
 *
 * WHY THIS FILE RATHER THAN THE THREE IT COULD HAVE BEEN SPLIT ACROSS. The
 * subject is one question -- which WRITES to health are noticed -- and the
 * answer has to be checked on all three routes or it is not checked at all. The
 * tests next door each cover one route's arithmetic and none of them can see
 * this: `Cataclysm.Skills.*` measures what a cost charged,
 * `Cataclysm.HealthDebt.*` measures what a debt did to two attributes, and
 * `Cataclysm.LowHealthRelief.*` measures a crossing it drives itself.
 *
 * A REAL PLAYER CHARACTER, WHICH IS WHY THESE ARE MORE EXPENSIVE THAN THEY
 * LOOK. `NotifyIfHealthReachedZero` casts the ability system's AVATAR to
 * `ACataclysmCharacterBase` and returns early for anything else, so the plain
 * actor every neighbouring fixture uses cannot observe a death at all. A
 * player's ability system also lives on its player state rather than on the
 * pawn, so owner and avatar differ, and that difference is itself a bug this
 * project has had once already -- issue #565.
 */
namespace CataclysmHealthWriteTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Resource = UCataclysmClassResourceAttributeSet;
	using Debt = UCataclysmHealthDebt;

	/** A round number, so every share of it below is exact. */
	constexpr float FullHealth = 1'000.0f;

	/**
	 * A real player character, possessed, wired the way the running game wires
	 * one.
	 *
	 * POSSESSED RATHER THAN GIVEN A PLAYER STATE BY HAND. Both routes reach
	 * `InitAbilityActorInfo`; this one goes through `PossessedBy`, which is the
	 * server path, and it also gives the pawn a controller. `HandleDeath`
	 * releases input from that controller, so a fixture without one would leave
	 * a line of the death untested.
	 */
	struct FScopedPlayer
	{
		explicit FScopedPlayer(UWorld* World)
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
			// down to the placeholder 100 the set starts at.
			Set(Vital::GetMaxHealthAttribute(), FullHealth);
			Set(Vital::GetHealthAttribute(), FullHealth);

			// ENOUGH MANA THAT NO SKILL BELOW IS REFUSED FOR WANT OF IT.
			// `CommitAbility` runs before `PayHealthCost`, so a skill that
			// cannot afford its mana never reaches the health cost at all and
			// the test would pass having measured nothing.
			Set(Vital::GetMaxManaAttribute(), 10'000.0f);
			Set(Vital::GetManaAttribute(), 10'000.0f);

			Set(Resource::GetMaxClassResourceAttribute(), 100.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		// NO DESTRUCTOR, UNLIKE THE PLAIN-ACTOR FIXTURES NEXT DOOR. Destroying
		// the world takes all three actors with it, and that is what
		// `CataclysmDeathTests.cpp` and `CataclysmHealthDebtTests.cpp` both do
		// with a possessed player. Destroying a possessed pawn by hand first is
		// a second teardown order nothing else in the suite exercises.

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
		float Fervour() const { return Get(Resource::GetClassResourceAttribute()); }

		bool IsDead() const
		{
			return UCataclysmSkillEffects::IsDead(Character);
		}

		/**
		 * Spend into Deeper Cuts, whose cost is a share of MAXIMUM health.
		 *
		 * A SHARE OF MAXIMUM AND NOT OF CURRENT, which is the whole reason a
		 * health cost can empty a character at all. `docs/DECISIONS.md` records
		 * the project owner making that distinction: a share of current health
		 * approaches zero without reaching it.
		 */
		void SetAddedHealthCostPercent(float Percent) const
		{
			Set(Resource::GetAddedHealthCostAttribute(), Percent);
		}

		/** Both of Rock Bottom's second-sentence rows. */
		void TakeRockBottomsRelief() const
		{
			Set(Resource::GetDebtClearedOnDroppingLowAttribute(), 1.0f);
			Set(Resource::GetFervourOnDroppingLowAttribute(), 50.0f);
		}

		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Grant a skill into a slot and hand back its live instance.
	 *
	 * A PROJECTILE WITH NO SPEED, the shape `Cataclysm.Skills` uses for a skill
	 * whose subject is its cost rather than what it hits. It commits, pays, and
	 * has nothing to travel to.
	 */
	UCataclysmProjectileSkill* GrantAProjectile(const FScopedPlayer& Caster)
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
			Instance->SkillName = TEXT("A skill that costs health");
			Instance->Params =
				UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Speed=0"));
		}
		return Instance;
	}

	/** Use a granted skill and report whether it started. */
	bool Activate(const FScopedPlayer& Caster, UGameplayAbility* Ability)
	{
		return Ability && Caster.AbilitySystem->TryActivateAbility(
			Ability->GetCurrentAbilitySpecHandle(),
			/*bAllowRemoteActivation=*/false);
	}
}

#define CATACLYSM_HEALTH_WRITE_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The hook itself
// ---------------------------------------------------------------------------

CATACLYSM_HEALTH_WRITE_TEST(FCataclysmDirectWriteToZeroKillsTest,
	"Cataclysm.HealthWrite.APlayerEmptiedByADirectWriteDies")
{
	using namespace CataclysmHealthWriteTest;

	// THE SMALLEST STATEMENT OF THE FAULT. No skill, no debt, no blow: the
	// health attribute is written to zero the way `ApplyModToAttribute` writes
	// it, and that must be enough to kill. Before issue #971 it was not, and
	// the character stood at zero health, alive, unable to pay for anything.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedPlayer Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	TestFalse(TEXT("it starts alive"), Player.IsDead());
	TestEqual(TEXT("on full health"), Player.Health(), FullHealth, 0.01f);

	Player.Set(Vital::GetHealthAttribute(), 0.0f);

	TestEqual(TEXT("the write landed"), Player.Health(), 0.0f, 0.01f);
	TestTrue(TEXT("and it killed"), Player.IsDead());
	TestTrue(TEXT("and the character says it is waiting to stand back up"),
		Player.Character->IsAwaitingRespawn());

	return true;
}

CATACLYSM_HEALTH_WRITE_TEST(FCataclysmMaximumHealthIsNotAHealthWriteTest,
	"Cataclysm.HealthWrite.RaisingMaximumHealthNoticesNothing")
{
	using namespace CataclysmHealthWriteTest;

	// THE FILTER IN `PostAttributeBaseChange` IS A REAL DECISION AND THIS IS
	// WHAT PINS IT. That hook fires for EVERY attribute, so it asks whether the
	// one that moved was health and returns otherwise. Without the question,
	// raising maximum health would fire both notifications: the ratio below
	// moves from a whole pool to a tenth of one without a point of health being
	// lost, and Rock Bottom would hand out its debt clearing and its Fervour for
	// it.
	//
	// WHETHER A CROSSING CAUSED BY A CHANGING MAXIMUM SHOULD COUNT is a design
	// question rather than a settled one, and issue #1095 asks it. This test
	// pins what the code does today, which is the same thing it did before
	// issue #1072: nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedPlayer Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	Player.TakeRockBottomsRelief();
	Debt::Defer(Player.AbilitySystem, 300.0f);
	TestEqual(TEXT("three hundred is owed"), Player.Owed(), 300.0f, 0.01f);

	// TEN TIMES THE MAXIMUM, SO THE CHARACTER IS ON A TENTH OF ITS POOL, well
	// below the fifth Rock Bottom watches.
	Player.Set(Vital::GetMaxHealthAttribute(), FullHealth * 10.0f);

	TestEqual(TEXT("health did not move"), Player.Health(), FullHealth, 0.01f);
	TestEqual(TEXT("the debt is untouched"), Player.Owed(), 300.0f, 0.01f);
	TestEqual(TEXT("and no Fervour arrived"), Player.Fervour(), 0.0f, 0.01f);
	TestFalse(TEXT("and nothing died"), Player.IsDead());

	return true;
}

// ---------------------------------------------------------------------------
// A health cost. Issue #971
// ---------------------------------------------------------------------------

CATACLYSM_HEALTH_WRITE_TEST(FCataclysmLethalHealthCostKillsTest,
	"Cataclysm.HealthWrite.AHealthCostThatEmptiesHealthKillsTheCaster")
{
	using namespace CataclysmHealthWriteTest;

	// THE PROJECT OWNER'S BUG, END TO END. A real skill charges a real health
	// cost through `UCataclysmSkillTemplate::PayHealthCost`, which writes the
	// attribute with `ApplyModToAttribute`. Issue #971.
	//
	// WHAT THE NEIGHBOURING TEST ALREADY COVERS AND THIS DOES NOT REPEAT.
	// `Cataclysm.Skills.AHealthCostLargerThanHealthLeavesTheCasterAtZeroNotBelow`
	// pins the FLOOR -- that health stops at zero rather than going negative --
	// on a plain actor, which cannot die. This is the other half.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedPlayer Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	// TWICE THE WHOLE POOL. No node reaches this -- Deeper Cuts stops at ten
	// percent over ten points -- but a cost that merely wounds would leave this
	// test unable to tell a death from a survival.
	Player.SetAddedHealthCostPercent(200.0f);

	UCataclysmProjectileSkill* Skill = GrantAProjectile(Player);
	if (!TestNotNull(TEXT("a granted skill"), Skill))
	{
		return false;
	}

	TestFalse(TEXT("it starts alive"), Player.IsDead());
	TestTrue(TEXT("the skill activates"), Activate(Player, Skill));

	TestEqual(TEXT("the cost emptied it"), Player.Health(), 0.0f, 0.01f);
	TestTrue(TEXT("and paying that cost killed it"), Player.IsDead());
	TestTrue(TEXT("and it is waiting to stand back up"),
		Player.Character->IsAwaitingRespawn());

	return true;
}

CATACLYSM_HEALTH_WRITE_TEST(FCataclysmOverspendLeavesAHoleTest,
	"Cataclysm.HealthWrite.AnOverspentCostLeavesTheBaseValueBelowZeroUntilTheRevival")
{
	using namespace CataclysmHealthWriteTest;

	// THIS TEST RECORDS A FAULT RATHER THAN A RULE. Issue #1096. Every gameplay
	// attribute has a BASE value and a CURRENT value. This set clamps health in
	// `PreAttributeChange`, which the engine applies to the CURRENT value, and
	// it does not override `PreAttributeBaseChange`, which is where the BASE
	// value would be clamped. A charge larger than the health left therefore
	// puts the base below zero while the current value reads a correct nought.
	//
	// AND `ApplyModToAttribute` WORKS FROM THE BASE, so healing afterwards
	// fills that hole before it gives the character anything.
	//
	// WHY IT IS RECORDED HERE INSTEAD OF FIXED HERE. Fixing it means clamping
	// the base value of every vital attribute, which is a different fault from
	// the one this file is about and touches what every direct write to health,
	// mana and energy shield may leave behind. Issue #1096 has the measurement
	// and what a fix looks like.
	//
	// AND WHY IT DOES NOT SHOW IN PLAY. The last two assertions are the point:
	// every route that overspends ends in a death, and `Revive` writes health
	// with `SetNumericAttributeBase`, which SETS the base rather than adding to
	// it. That is what wipes the hole. Before issue #971 the character did not
	// die, so the hole stayed and healing was swallowed for as long as it lived.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedPlayer Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	// TWO HUNDRED LEFT, AND A CHARGE OF FIVE HUNDRED. The charge is half of the
	// MAXIMUM, which is what makes it bigger than what is left.
	Player.Set(Vital::GetHealthAttribute(), 200.0f);
	Player.SetAddedHealthCostPercent(50.0f);

	UCataclysmProjectileSkill* Skill = GrantAProjectile(Player);
	if (!TestNotNull(TEXT("a granted skill"), Skill))
	{
		return false;
	}

	TestTrue(TEXT("the skill activates"), Activate(Player, Skill));

	const FGameplayAttribute Health = Vital::GetHealthAttribute();
	const float Base = Player.AbilitySystem->GetNumericAttributeBase(Health);

	// REPORTED AS WELL AS ASSERTED, so a run shows the number rather than only
	// whether it was the expected one.
	AddInfo(FString::Printf(
		TEXT("after overspending 500 against 200, the health attribute's base "
			 "value is %.2f and its current value is %.2f."),
		Base, Player.Health()));

	TestEqual(TEXT("the current value is a correct nought"), Player.Health(),
			  0.0f, 0.01f);
	TestEqual(TEXT("and the base value is three hundred below it, issue #1096"),
			  Base, -300.0f, 0.01f);

	// AND HEALING A HUNDRED GIVES NOTHING, which is what the hole costs.
	Player.AbilitySystem->ApplyModToAttribute(Health, EGameplayModOp::Additive,
											  100.0f);
	TestEqual(TEXT("so healing a hundred gives nothing, issue #1096"),
			  Player.Health(), 0.0f, 0.01f);

	// THE DEATH AND THE REVIVAL ARE WHAT MAKE IT HARMLESS, and that is worth
	// pinning: it is the reason issue #1096 is recorded rather than urgent.
	TestTrue(TEXT("but overspending killed the character"), Player.IsDead());

	Player.Character->Revive();

	TestEqual(TEXT("and standing back up refills health"), Player.Health(),
			  FullHealth, 0.01f);
	TestEqual(TEXT("and sets the base value with it, so the hole is gone"),
			  Player.AbilitySystem->GetNumericAttributeBase(Health), FullHealth,
			  0.01f);

	return true;
}

// ---------------------------------------------------------------------------
// A debt falling due. Issue #971
// ---------------------------------------------------------------------------

CATACLYSM_HEALTH_WRITE_TEST(FCataclysmDebtFallingDueKillsTest,
	"Cataclysm.HealthWrite.ADebtFallingDueThatEmptiesHealthKills")
{
	using namespace CataclysmHealthWriteTest;

	// THE THIRD ROUTE, AND THE ONE MOST LIKELY TO HAVE EMPTIED THE OWNER'S
	// CHARACTER. `UCataclysmHealthDebt::SettleIfDue` charges the whole debt with
	// no floor under it -- deliberately, and its own comment says so -- so a
	// character that owes more than it has left is emptied by a clock rather
	// than by anything it did that instant.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedPlayer Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	// A HUNDRED HEALTH LEFT AND A DEBT OF FIVE HUNDRED. Written rather than
	// spent down to, so the figures are exact.
	Player.Set(Vital::GetHealthAttribute(), 100.0f);
	Debt::Defer(Player.AbilitySystem, 500.0f);

	// BEFORE THE THREE SECONDS ARE UP NOTHING IS TAKEN, which says the death
	// below came from the debt falling due rather than from `Defer` itself.
	TestEqual(TEXT("nothing is taken before the debt is due"),
			  Debt::SettleIfDue(Player.Character), 0.0f, 0.01f);
	TestFalse(TEXT("and the character is alive"), Player.IsDead());

	World->TimeSeconds += 4.0f;

	TestEqual(TEXT("the whole debt is taken at once"),
			  Debt::SettleIfDue(Player.Character), 500.0f, 0.01f);
	TestEqual(TEXT("which emptied the character"), Player.Health(), 0.0f, 0.01f);
	TestTrue(TEXT("and killed it"), Player.IsDead());
	TestTrue(TEXT("and it is waiting to stand back up"),
		Player.Character->IsAwaitingRespawn());

	return true;
}

// ---------------------------------------------------------------------------
// A threshold crossed by a cost rather than by a blow. Issue #1072
// ---------------------------------------------------------------------------

CATACLYSM_HEALTH_WRITE_TEST(FCataclysmCostCrossesTheLowLineTest,
	"Cataclysm.HealthWrite.AHealthCostThatDropsTheCasterLowClearsItsDebt")
{
	using namespace CataclysmHealthWriteTest;

	// ROCK BOTTOM'S WHOLE LOOP, IN ONE CAST. Issue #1069's second sentence:
	// "Dropping below 20% health clears all outstanding debt and grants 50
	// Fervour, no more than once every 30 seconds." Issue #1072 is that a fall
	// caused by a health COST never reached the rule at all, which is the
	// only route this option's own first sentence produces.
	//
	// AND THE DEBT IS THE ONE THAT WAS OWED BEFORE THE CAST, which is the
	// project owner's answer to issue #1073 on 2026-08-31. `PayHealthCost`
	// writes health before it calls `Defer`, so the crossing sees the debt as it
	// stood beforehand. That ordering is what this test measures, not only that
	// something happened.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedPlayer Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	Player.TakeRockBottomsRelief();

	// OWED BEFORE THE CAST, so the assertion below can tell "the debt that was
	// there" from "any debt at all".
	Debt::Defer(Player.AbilitySystem, 300.0f);
	TestEqual(TEXT("three hundred is owed going in"), Player.Owed(), 300.0f,
			  0.01f);

	// A COST OF NINE TENTHS OF THE POOL, so one cast takes a character from full
	// health to a tenth of it, which is below the fifth the option watches.
	Player.SetAddedHealthCostPercent(90.0f);

	UCataclysmProjectileSkill* Skill = GrantAProjectile(Player);
	if (!TestNotNull(TEXT("a granted skill"), Skill))
	{
		return false;
	}

	TestTrue(TEXT("the skill activates"), Activate(Player, Skill));

	TestEqual(TEXT("the cost took nine tenths of the pool"), Player.Health(),
			  FullHealth * 0.1f, 1.0f);
	TestFalse(TEXT("which did not kill"), Player.IsDead());

	// THE TWO THINGS THE SENTENCE PROMISES.
	TestEqual(TEXT("the fall cleared the debt"), Player.Owed(), 0.0f, 0.01f);
	TestEqual(TEXT("and granted fifty Fervour"), Player.Fervour(), 50.0f, 0.01f);

	return true;
}

CATACLYSM_HEALTH_WRITE_TEST(FCataclysmCostCrossesHalfHealthTest,
	"Cataclysm.HealthWrite.AHealthCostThatDropsTheCasterBelowHalfConvertsDamage")
{
	using namespace CataclysmHealthWriteTest;

	// THE OTHER THRESHOLD, AND A DIFFERENT NODE. Issue #985, The Breaking
	// Point: "Dropping below 50% health converts all damage you take into
	// Bleeding". Issue #1072 called this a gap rather than a break, because a
	// blow is the node's natural trigger and a blow always worked. For a
	// Masochist holding Water to Blood, whose every skill costs health, it is
	// not a gap.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world with a clock"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedPlayer Player(World);
	if (!TestTrue(TEXT("a possessed player with an ability system"),
				  Player.IsUsable()))
	{
		return false;
	}

	// THE NODE'S TWO ROWS: the rule applies, and one turn of it lasts 5 seconds.
	Player.Set(Resource::GetDamageToBleedingOnLowHealthAttribute(), 1.0f);
	Player.Set(Resource::GetDamageToBleedingWindowAttribute(), 5.0f);

	TestFalse(TEXT("nothing is converting to start with"),
		Player.AbilitySystem->IsConvertingDamageToBleeding());

	// SIX TENTHS OF THE POOL, so one cast crosses the halfway line and stops
	// short of the fifth the other node watches.
	Player.SetAddedHealthCostPercent(60.0f);

	UCataclysmProjectileSkill* Skill = GrantAProjectile(Player);
	if (!TestNotNull(TEXT("a granted skill"), Skill))
	{
		return false;
	}

	TestTrue(TEXT("the skill activates"), Activate(Player, Skill));

	TestEqual(TEXT("the cost took six tenths of the pool"), Player.Health(),
			  FullHealth * 0.4f, 1.0f);
	TestTrue(TEXT("and the fall opened the conversion window"),
		Player.AbilitySystem->IsConvertingDamageToBleeding());

	return true;
}

#undef CATACLYSM_HEALTH_WRITE_TEST

#endif // WITH_AUTOMATION_TESTS
