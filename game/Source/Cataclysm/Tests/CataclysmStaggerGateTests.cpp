// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
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

#endif // WITH_AUTOMATION_TESTS
