// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The two stats a staggering character can carry. Issue #45.
 *
 * WHAT THESE ARE FOR. Two enchantment rows: "You cannot stagger enemies above
 * 50% HP", which lowers a health ceiling, and "Stagger effects you apply last
 * 50%-100% longer", which is a percentage of normal. Both are read off the
 * character APPLYING the stagger, inside `UCataclysmSkillEffects::ApplyStagger`.
 *
 * THE FIRST TEST IS THE ONE THAT LETS THIS MERGE WITHOUT RE-VERIFYING EVERY
 * EXISTING CALLER. Both stats default to a value that changes nothing -- the
 * ceiling reduction to 0, which leaves the ceiling at 100 when no living target
 * is above 100 per cent, and the duration to 100, which is a multiplier of
 * exactly one. A character carrying neither must behave exactly as it did
 * before. That is also the property a later change is most likely to break
 * without noticing, which is why it is asserted rather than argued.
 *
 * NOTHING HERE WRITES THE STAGGERED TAG BY HAND. Every test drives
 * `ApplyStagger`, so a build that stopped reading either stat fails here rather
 * than passing against a tag the test wrote itself.
 */
namespace CataclysmStaggerGateTest
{
	/** A creature with a full health pool, on the monsters' side. */
	ACataclysmEnemyCharacter* SpawnGateCreature(UWorld* World, const FVector& Where,
												float MaxHealth = 1000.0f)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(MaxHealth);
		}
		return Spawned;
	}

	UCataclysmAbilitySystemComponent* GateSystemOf(AActor* Actor)
	{
		const ACataclysmEnemyCharacter* Creature =
			Cast<ACataclysmEnemyCharacter>(Actor);
		return Creature
			? Cast<UCataclysmAbilitySystemComponent>(
				  Creature->GetAbilitySystemComponent())
			: nullptr;
	}

	/** Put this character at a share of its own maximum health. */
	void SetHealthPercent(AActor* Actor, float Percent)
	{
		if (UCataclysmAbilitySystemComponent* System = GateSystemOf(Actor))
		{
			const float MaxHealth = System->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
			System->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(),
				MaxHealth * Percent / 100.0f);
		}
	}

	void SetStat(AActor* Actor, const FGameplayAttribute& Attribute, float Value)
	{
		if (UCataclysmAbilitySystemComponent* System = GateSystemOf(Actor))
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}
	}

	/**
	 * How long the longest effect running on this character lasts.
	 *
	 * READ OFF THE ACTIVE EFFECT RATHER THAN WAITED OUT. A world built for a test
	 * is never ticked, so no duration can expire here however far a clock is
	 * pushed. `CataclysmDebuffTests.cpp` reads it the same way and for the same
	 * reason.
	 */
	float LongestEffectOn(AActor* Actor)
	{
		UCataclysmAbilitySystemComponent* System = GateSystemOf(Actor);
		if (!System)
		{
			return -1.0f;
		}
		float Longest = 0.0f;
		for (float Seconds : System->GetActiveEffectsDuration(FGameplayEffectQuery()))
		{
			Longest = FMath::Max(Longest, Seconds);
		}
		return Longest;
	}

	/**
	 * A character whose stats have been RESOLVED FROM THE CLASS TABLE, which is
	 * the path a player takes and a spawned creature does not.
	 *
	 * WHY THIS EXISTS AND WHY THE OTHER TESTS HERE CANNOT REPLACE IT. Every other
	 * test in this file writes an attribute directly onto a spawned creature. A
	 * creature's attributes never go through `UCataclysmPlayerClassStats::ApplyTo`,
	 * so they keep whatever the attribute set's constructor stated, and
	 * `StatForSkill` answers with that value as its fallback. A player's do go
	 * through it, `ApplyTo` writes the RESOLVED value over the constructor's, and
	 * `StatForSkill` answers from the recorded stat line instead. Those are two
	 * different routes through `ApplyStagger`, and the tests above take only the
	 * first.
	 *
	 * A PLAIN ACTOR IS ENOUGH. `UCataclysmTargeting::AbilitySystemOf` goes through
	 * `UAbilitySystemGlobals`, which falls back to finding the component on the
	 * actor when the actor implements no interface.
	 *
	 * ONLY TWO ATTRIBUTE SETS, unlike the fuller fixture in
	 * `CataclysmPlayerClassStatsTests.cpp`. `ApplyTo` skips any attribute whose set
	 * the component does not hold, and a stagger reads combat stats off the
	 * applier and health off the target. A test asserting every mapped stat was
	 * written would need the others; this one does not.
	 */
	struct FResolvedApplier
	{
		explicit FResolvedApplier(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			// Raw pointers rather than TObjectPtr: `AddAttributeSetSubobject` is a
			// template and deduces its type from the argument, so a TObjectPtr
			// would deduce the wrapper instead of the attribute set. The fuller
			// fixture records the same reason.
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FResolvedApplier()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/**
		 * Resolve this character's stats the way a real player's are resolved.
		 *
		 * `StartingClassName` AND NOT `UCataclysmClassStats::DefaultClassName`.
		 * The header warns about exactly this pair: the second is the shared line
		 * a class inherits from when it states nothing of its own and carries no
		 * defensive layer, while the first is the class a player actually plays
		 * as. They were the same string until 2026-08-24, and every character
		 * played in that time had no armour, no resistance, no block and no
		 * leech. Issue #806. This test is about a player, so it takes the
		 * player's line.
		 *
		 * `DefaultLevel` RATHER THAN A TYPED 20, because it is a placeholder the
		 * console can change and a copied number here would outlive it.
		 */
		bool ResolveFromTheClassTable()
		{
			const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
			if (!Table)
			{
				return false;
			}
			UCataclysmPlayerClassStats::ApplyTo(
				AbilitySystem, Table,
				UCataclysmPlayerClassStats::StartingClassName,
				UCataclysmPlayerClassStats::DefaultLevel);
			return true;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is
// built as a unity blob and a `using namespace` at file scope reaches the other
// files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStaggerGateDefaultsTest,
	"Cataclysm.StaggerGate.NeitherStatSetLeavesEveryStaggerExactlyAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character carrying neither stat staggers what it always staggered.
 *
 * THIS IS THE TEST THAT MAKES THE CHANGE SAFE TO MERGE. Every existing caller of
 * `ApplyStagger` -- a knockback, a pull, a knockdown -- goes through the two new
 * readings now. If either default were wrong, every stagger in the game would
 * change and no other test here would say so, because the others all SET a stat.
 *
 * A FULL-HEALTH TARGET IS THE STRICT CASE for the ceiling. At the default
 * reduction of zero the ceiling is 100, and a target at exactly 100 per cent is
 * not ABOVE 100, so it must still be staggered. A ceiling written with the wrong
 * comparison fails here and nowhere else.
 */
bool FCataclysmStaggerGateDefaultsTest::RunTest(const FString&)
{
	using namespace CataclysmStaggerGateTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Striker = SpawnGateCreature(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Struck =
		SpawnGateCreature(World, FVector(200.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("a striker"), Striker)
		|| !TestNotNull(TEXT("a target"), Struck))
	{
		return false;
	}

	// NEITHER STAT IS TOUCHED. That is the whole point of this test.
	if (!TestTrue(TEXT("a target at full health is still staggered"),
				  UCataclysmSkillEffects::ApplyStagger(Striker, Struck))
		|| !TestTrue(TEXT("and carries the state"),
					 UCataclysmSkillEffects::IsStaggered(Struck)))
	{
		return false;
	}

	TestEqual(TEXT("for the one second it always lasted"),
			  LongestEffectOn(Struck), UCataclysmSkillEffects::StaggerSeconds,
			  0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStaggerGateCeilingTest,
	"Cataclysm.StaggerGate.AHealthyTargetIsRefusedAndAHurtOneIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The ceiling refuses a target above it and allows one at or below it.
 *
 * TWO TARGETS AND ONE STRIKER, because a refusal on its own would pass against a
 * build that refused everything. The boundary is checked as well: a target at
 * exactly the ceiling is not ABOVE it and must be staggered.
 */
bool FCataclysmStaggerGateCeilingTest::RunTest(const FString&)
{
	using namespace CataclysmStaggerGateTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Striker = SpawnGateCreature(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Healthy =
		SpawnGateCreature(World, FVector(200.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Hurt =
		SpawnGateCreature(World, FVector(0.0f, 200.0f, 0.0f));
	ACataclysmEnemyCharacter* Exactly =
		SpawnGateCreature(World, FVector(0.0f, 0.0f, 200.0f));
	if (!TestNotNull(TEXT("a striker"), Striker)
		|| !TestNotNull(TEXT("a healthy target"), Healthy)
		|| !TestNotNull(TEXT("a hurt one"), Hurt)
		|| !TestNotNull(TEXT("one at exactly the ceiling"), Exactly))
	{
		return false;
	}

	// THE ROW'S OWN NUMBER: "You cannot stagger enemies above 50% HP".
	SetStat(Striker,
			UCataclysmCombatAttributeSet::GetStaggerHealthCeilingReductionAttribute(),
			50.0f);
	SetHealthPercent(Healthy, 80.0f);
	SetHealthPercent(Hurt, 20.0f);
	SetHealthPercent(Exactly, 50.0f);

	TestFalse(TEXT("a target above the ceiling is not staggered"),
			  UCataclysmSkillEffects::ApplyStagger(Striker, Healthy));
	TestFalse(TEXT("and carries no state"),
			  UCataclysmSkillEffects::IsStaggered(Healthy));

	TestTrue(TEXT("a target below the ceiling is"),
			 UCataclysmSkillEffects::ApplyStagger(Striker, Hurt));
	TestTrue(TEXT("and carries it"), UCataclysmSkillEffects::IsStaggered(Hurt));

	// AT the ceiling is not ABOVE it. The row says "above 50%".
	TestTrue(TEXT("a target at exactly the ceiling is staggered"),
			 UCataclysmSkillEffects::ApplyStagger(Striker, Exactly));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStaggerGateShoveTest,
	"Cataclysm.StaggerGate.ARefusedStaggerStillKnocksTheTargetBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The ceiling withholds the MARKER, not the displacement.
 *
 * WHY THIS MATTERS AND IS NOT OBVIOUS FROM THE ROW. Being staggered does not stop
 * a target acting -- the header on `ApplyStagger` says so, and it is the owner's
 * ruling. The state is a marker other rows read. So "you cannot stagger enemies
 * above 50% HP" must not stop the knockback that would have staggered them; it
 * must only withhold the tag.
 *
 * A build that returned early from `ApplyKnockback` instead would move nothing,
 * and every test above would still pass.
 */
bool FCataclysmStaggerGateShoveTest::RunTest(const FString&)
{
	using namespace CataclysmStaggerGateTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Striker = SpawnGateCreature(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Healthy =
		SpawnGateCreature(World, FVector(200.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("a striker"), Striker)
		|| !TestNotNull(TEXT("a healthy target"), Healthy))
	{
		return false;
	}

	SetStat(Striker,
			UCataclysmCombatAttributeSet::GetStaggerHealthCeilingReductionAttribute(),
			50.0f);
	SetHealthPercent(Healthy, 100.0f);

	const FVector Before = Healthy->GetActorLocation();
	const bool bShoved =
		UCataclysmSkillEffects::ApplyKnockback(Striker, Healthy, 300.0f);
	const FVector After = Healthy->GetActorLocation();

	TestTrue(TEXT("the knockback still landed"), bShoved);
	TestTrue(*FString::Printf(TEXT("and the target actually moved: %s to %s"),
							  *Before.ToCompactString(), *After.ToCompactString()),
			 !After.Equals(Before, 1.0f));
	TestFalse(TEXT("but the marker was withheld"),
			  UCataclysmSkillEffects::IsStaggered(Healthy));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStaggerGateDurationTest,
	"Cataclysm.StaggerGate.TheApplierAndTheTargetBothLengthenItAndTheyMultiply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two characters reach one duration, and their scalars MULTIPLY.
 *
 * THIS TEST PINS A DESIGN DECISION, WHICH IS WHY THE NUMBERS ARE CHOSEN TO
 * SEPARATE THE TWO ANSWERS. The applier carries 200 (a doubling) and the target
 * 150 (half again). Multiplying gives 1 x 2.0 x 1.5 = 3.0 seconds. SUMMING the
 * two as increases would give 1 x (1 + 1.00 + 0.50) = 2.5. Both were defensible
 * from precedent alone; the code settled it, because the sum happens INSIDE one
 * character's attribute pipeline and a second character's stat cannot join it.
 *
 * Each is also checked alone, so a build that read one stat and not the other
 * fails on the half it dropped rather than passing on the product.
 */
bool FCataclysmStaggerGateDurationTest::RunTest(const FString&)
{
	using namespace CataclysmStaggerGateTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float Normal = UCataclysmSkillEffects::StaggerSeconds;

	const auto Struck = [&](float ApplierPercent, float TargetPercent) -> float
	{
		ACataclysmEnemyCharacter* Striker =
			SpawnGateCreature(World, FVector::ZeroVector);
		ACataclysmEnemyCharacter* Target =
			SpawnGateCreature(World, FVector(400.0f, 400.0f, 0.0f));
		if (!Striker || !Target)
		{
			return -1.0f;
		}
		SetStat(Striker,
				UCataclysmCombatAttributeSet::GetStaggerDurationAttribute(),
				ApplierPercent);
		SetStat(Target,
				UCataclysmCombatAttributeSet::GetDebuffDurationTakenAttribute(),
				TargetPercent);
		if (!UCataclysmSkillEffects::ApplyStagger(Striker, Target))
		{
			return -1.0f;
		}
		const float Lasted = LongestEffectOn(Target);
		Striker->Destroy();
		Target->Destroy();
		return Lasted;
	};

	const float Plain = Struck(100.0f, 100.0f);
	const float ApplierOnly = Struck(200.0f, 100.0f);
	const float TargetOnly = Struck(100.0f, 150.0f);
	const float Both = Struck(200.0f, 150.0f);

	if (!TestTrue(TEXT("every stagger landed"),
				  Plain > 0.0f && ApplierOnly > 0.0f && TargetOnly > 0.0f
					  && Both > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("neither stat set leaves the normal second"), Plain, Normal,
			  0.01f);
	TestEqual(TEXT("the applier's stat alone doubles it"), ApplierOnly,
			  Normal * 2.0f, 0.01f);
	TestEqual(TEXT("the target's stat alone gives half again"), TargetOnly,
			  Normal * 1.5f, 0.01f);

	// 3.0 AND NOT 2.5. See this test's own comment: summing the two as increases
	// would give 2.5, and that was the answer precedent alone suggested.
	TestEqual(*FString::Printf(
				  TEXT("and together they MULTIPLY: %.2f, where summing them "
					   "would give %.2f"),
				  Both, Normal * 2.5f),
			  Both, Normal * 3.0f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStaggerGateResolvedApplierTest,
	"Cataclysm.StaggerGate.ACharacterWhoseStatsCameFromTheClassTableStillStaggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character whose stats were resolved from the class table still staggers.
 *
 * THIS TEST EXISTS BECAUSE THE FOUR ABOVE IT ALL PASSED WHILE THE FEATURE WAS
 * BROKEN FOR EVERY PLAYER. `stagger_duration` has no line in
 * `game/Data/ClassStats.csv`, so `UCataclysmClassStats::BaseFor` answers zero
 * for it; `UCataclysmPlayerClassStats::ApplyTo` then wrote that zero over the
 * 100 the attribute set's constructor states, `ApplyStagger` scaled by zero and
 * refused, and no player staggered anything. The repair is an entry in
 * `UCataclysmPlayerClassStats::EngineSuppliedBases`.
 *
 * WHY THE EXISTING BASE TEST IS NOT ENOUGH ON ITS OWN.
 * `Cataclysm.PlayerStats.EveryEngineSuppliedBaseReachesACharacter` walks the
 * entries that ARE in that map. Delete the stagger entry and it walks one fewer
 * and passes, so it cannot see the entry go missing -- which is exactly how the
 * defect would come back. This test fails when the entry is absent, because it
 * asks for the behaviour rather than for the list.
 *
 * IT SETS NO STAT. Every other test in this file writes an attribute by hand,
 * which is the fallback route. This one takes the resolved route and asserts the
 * ordinary, unmodified outcome: a stagger of the normal length.
 */
bool FCataclysmStaggerGateResolvedApplierTest::RunTest(const FString&)
{
	using namespace CataclysmStaggerGateTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FResolvedApplier Applier(World);
	if (!TestTrue(TEXT("the class stats table loaded"),
				  Applier.ResolveFromTheClassTable()))
	{
		return false;
	}

	// THE BASE ARRIVED. Checked before the stagger so a failure says which half
	// broke: a wrong duration here means the base never reached the character,
	// and a wrong duration below means it reached it and `ApplyStagger` misread
	// it. Without the repair this reads 0 rather than 100.
	TestEqual(TEXT("the resolved character holds the normal stagger duration"),
			  Applier.AbilitySystem->GetNumericAttribute(
				  UCataclysmCombatAttributeSet::GetStaggerDurationAttribute()),
			  UCataclysmSkillEffects::NormalStaggerDuration, 0.01f);

	ACataclysmEnemyCharacter* Target =
		SpawnGateCreature(World, FVector(200.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("target"), Target))
	{
		return false;
	}

	if (!TestTrue(TEXT("the stagger landed"),
				  UCataclysmSkillEffects::ApplyStagger(Applier.Actor, Target))
		|| !TestTrue(TEXT("and the target carries the state"),
					 UCataclysmSkillEffects::IsStaggered(Target)))
	{
		return false;
	}

	TestEqual(TEXT("and it lasts the second it always lasted"),
			  LongestEffectOn(Target),
			  UCataclysmSkillEffects::StaggerSeconds, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
