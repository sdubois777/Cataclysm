// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Retaliation: what a defender sends back to whoever hit it. Issue #895.
 *
 * WHAT WAS WRONG, AND IT WAS WORSE THAN AN UNREAD ATTRIBUTE. `Retaliation`
 * existed, was clamped, was replicated, was given to the Masochist by its class
 * line at 158 at level 100, and no code in the project read it. The design
 * listed it among the Defence stats and NEVER SAID WHAT IT DOES, so there was
 * nothing to check the code against either.
 *
 * THE RULE IS NOW WRITTEN DOWN, in the Retaliation section of
 * docs/Cataclysm_GDD_v2.md, after the project owner settled the one question the
 * genre disagrees about. docs/DECISIONS.md carries the research: Path of Exile
 * fires reflection on melee attacks only, Diablo IV on any direct attack, and
 * the owner chose any direct attack because this game has a lot of ranged
 * creatures and the Masochist is the one class built around the stat.
 */
namespace CataclysmRetaliationTest
{
	/**
	 * A combatant that can be given retaliation.
	 *
	 * Named apart from the harnesses in the neighbouring test files on purpose:
	 * the Unreal unity build concatenates these translation units, so two
	 * structs of one name in two files compile until both are clean and then
	 * collide.
	 */
	struct FRetaliator
	{
		explicit FRetaliator(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor));

			Vitals = NewVitals;
			Combat = NewCombat;
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Vitals->SetMaxHealth(10'000.0f);
			Vitals->SetHealth(10'000.0f);
		}

		~FRetaliator()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
	};
}

#define CATACLYSM_RETALIATION_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationHitsBackTest,
	"Cataclysm.Retaliation.AHitThatGotThroughSendsAFlatAmountBack")
{
	using namespace CataclysmRetaliationTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRetaliator Attacker(World);
	FRetaliator Defender(World);

	Attacker.Combat->SetAttackDamage(500.0f);
	Defender.Combat->SetRetaliation(158.0f);

	// A CHARACTER WITH NONE OF IT SENDS NOTHING BACK, asserted first so the
	// figure below is evidence of the stat rather than of anything else the hit
	// does to the attacker.
	Defender.Combat->SetRetaliation(0.0f);
	float Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	TestEqual(TEXT("a defender with no retaliation sends nothing back"),
		Attacker.Vitals->GetHealth(), Before, 0.001f);

	// AND ONE WITH IT SENDS EXACTLY THAT MUCH. The Masochist's own figure at
	// level 100, taken from game/Data/ClassStats.csv.
	Defender.Combat->SetRetaliation(158.0f);
	Before = Attacker.Vitals->GetHealth();
	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());

	if (!TestTrue(FString::Printf(TEXT("the hit landed (%.1f)"), Dealt),
				  Dealt > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("and the attacker takes the defender's retaliation"),
		Before - Attacker.Vitals->GetHealth(), 158.0f, 0.01f);

	// A FLAT AMOUNT AND NOT A SHARE OF THE HIT, which is what the class stat
	// table already says by writing it as a bare 158 while writing damage
	// reduction as "8%". A hit five times the size sends back the same number.
	Attacker.Combat->SetAttackDamage(2'500.0f);
	Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	TestEqual(TEXT("a hit five times the size sends back the same amount"),
		Before - Attacker.Vitals->GetHealth(), 158.0f, 0.01f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationIsNotAHitTest,
	"Cataclysm.Retaliation.WhatComesBackIsNotItselfAHit")
{
	using namespace CataclysmRetaliationTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// BOTH SIDES RETALIATE, which is the case the rule exists for. If what comes
	// back were itself a hit, each would answer the other's answer and the two
	// would reflect without end.
	FRetaliator Attacker(World);
	FRetaliator Defender(World);

	Attacker.Combat->SetAttackDamage(500.0f);
	Attacker.Combat->SetRetaliation(158.0f);
	Defender.Combat->SetRetaliation(158.0f);

	const float AttackerBefore = Attacker.Vitals->GetHealth();
	const float DefenderBefore = Defender.Vitals->GetHealth();

	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());

	// THE ATTACKER TAKES ONE LOT AND NOT TWO. Its own retaliation must not
	// answer what the defender sent back.
	TestEqual(TEXT("the attacker takes the defender's retaliation once"),
		AttackerBefore - Attacker.Vitals->GetHealth(), 158.0f, 0.01f);

	// AND THE DEFENDER TOOK ONLY THE ORIGINAL BLOW. If retaliation were a hit,
	// the attacker's own would have come back to the defender on top of it.
	const float DefenderTook = DefenderBefore - Defender.Vitals->GetHealth();
	TestTrue(FString::Printf(
		TEXT("the defender took the blow and nothing more: %.1f"), DefenderTook),
		DefenderTook > 0.0f);
	TestTrue(TEXT("and not the attacker's retaliation on top of it"),
		!FMath::IsNearlyEqual(DefenderTook, 500.0f + 158.0f, 0.01f));

	// ARMOUR DOES NOT REDUCE IT, because it is not a hit. A blow of 158 against
	// this much armour would arrive far smaller.
	Attacker.Combat->SetArmor(5'000.0f);
	const float ArmouredBefore = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	TestEqual(TEXT("armour on the attacker does not reduce what comes back"),
		ArmouredBefore - Attacker.Vitals->GetHealth(), 158.0f, 0.01f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationExclusionsTest,
	"Cataclysm.Retaliation.NeitherADamageOverTimeTickNorAMinionsBlowProvokesIt")
{
	using namespace CataclysmRetaliationTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRetaliator Attacker(World);
	FRetaliator Defender(World);

	Attacker.Combat->SetAttackDamage(500.0f);
	Defender.Combat->SetRetaliation(158.0f);

	// A DAMAGE OVER TIME TICK PROVOKES NOTHING. All three games in the genre
	// agree that reflection answers a hit rather than a tick, and a burn ticking
	// once a second against a retaliating target would otherwise be a second and
	// silent source of damage.
	FCataclysmHitDelivery OverTime;
	OverTime.bIsDamageOverTime = true;

	float Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), OverTime);
	TestEqual(TEXT("a damage over time tick provokes no retaliation"),
		Attacker.Vitals->GetHealth(), Before, 0.001f);

	// AND A MINION'S BLOW PROVOKES NONE EITHER. It is credited to its summoner,
	// so without this a Ritualist standing at range would take retaliation every
	// time one of its imps struck a retaliating enemy.
	FCataclysmHitDelivery MinionBlow;
	MinionBlow.bCannotCriticallyStrike = true;
	MinionBlow.bCannotPenetrate = true;
	MinionBlow.bCarriesNoWeaponSubType = true;
	MinionBlow.bCannotLeech = true;
	MinionBlow.bCannotBeRetaliatedAgainst = true;

	Before = Attacker.Vitals->GetHealth();
	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), MinionBlow);

	if (!TestTrue(FString::Printf(
			TEXT("the minion's blow landed (%.1f)"), Dealt), Dealt > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("a minion's blow provokes no retaliation on its summoner"),
		Attacker.Vitals->GetHealth(), Before, 0.001f);

	// AND THE SAME BLOW WITHOUT THAT ONE FLAG DOES, which is what makes the
	// check above evidence of the flag rather than of retaliation being broken.
	FCataclysmHitDelivery OwnBlow = MinionBlow;
	OwnBlow.bCannotBeRetaliatedAgainst = false;

	Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), OwnBlow);
	TestEqual(TEXT("the same blow struck in the attacker's own name does"),
		Before - Attacker.Vitals->GetHealth(), 158.0f, 0.01f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationEvadedTest,
	"Cataclysm.Retaliation.AHitThatGotThroughNothingProvokesNothing")
{
	using namespace CataclysmRetaliationTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRetaliator Attacker(World);
	FRetaliator Defender(World);

	Attacker.Combat->SetAttackDamage(500.0f);
	Defender.Combat->SetRetaliation(158.0f);

	// EVERY HIT IS EVADED, so nothing gets through and nothing comes back. The
	// design: only a hit that got through provokes it.
	Defender.Combat->SetEvasion(100.0f);

	const float Before = Attacker.Vitals->GetHealth();
	const float DefenderBefore = Defender.Vitals->GetHealth();

	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());

	TestEqual(TEXT("an evaded hit took nothing from the defender"),
		Defender.Vitals->GetHealth(), DefenderBefore, 0.001f);
	TestEqual(TEXT("and provoked no retaliation"),
		Attacker.Vitals->GetHealth(), Before, 0.001f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationGrowsWithTheResourceTest,
	"Cataclysm.Retaliation.ABonusThatGrowsWithTheClassResourceReachesTheBlowSentBack")
{
	using namespace CataclysmRetaliationTest;

	// THE MASOCHIST'S RECIPROCITY KEYSTONE, END TO END: "Your Retaliation damage
	// is increased by 1% for each point of Fervour you currently hold."
	// Issue #980.
	//
	// WHAT THIS CATCHES THAT NOTHING ELSE COULD. The blow sent back used to be
	// read straight off the `Retaliation` gameplay attribute, and a bonus whose
	// SIZE grows with a state is deliberately never written onto an attribute --
	// it would be stale the moment the state moved. So the node would have been
	// authored, the modifier would have been built correctly, and the blow would
	// have been exactly what it was before, in silence.
	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRetaliator Attacker(World);
	FRetaliator Defender(World);

	Attacker.Combat->SetAttackDamage(500.0f);
	Defender.Combat->SetRetaliation(100.0f);

	// THE POOL IS ADDED HERE RATHER THAN IN THE SHARED HARNESS, so the other
	// four tests in this file keep asking with no class resource at all, which
	// is the state every enemy in the game is in.
	UCataclysmClassResourceAttributeSet* Pool =
		NewObject<UCataclysmClassResourceAttributeSet>(Defender.Actor);
	Defender.AbilitySystem->AddAttributeSetSubobject(Pool);
	Pool->SetMaxClassResource(100.0f);

	// WHAT THE PASSIVE TREE WOULD HAVE BUILT, written here by hand. A keystone
	// worth one percentage point per point of the pool held, over a base equal
	// to the attribute, so a character holding nothing gets exactly what it got
	// before this existed.
	FCataclysmStatModifier Reciprocity;
	Reciprocity.Bucket = ECataclysmStatBucket::Increased;
	Reciprocity.Source = ECataclysmModifierSource::PassiveKeystone;
	Reciprocity.Value = 1.0f;
	Reciprocity.Scale = ECataclysmStatScale::PerPointOfClassResourceHeld;
	Reciprocity.ScaleStep = 1.0f;

	FCataclysmStatInputs Inputs;
	Inputs.Base = 100.0f;
	Inputs.Modifiers = {Reciprocity};

	TMap<FName, FCataclysmStatInputs> Recorded;
	Recorded.Add(FName(TEXT("retaliation")), Inputs);
	Defender.AbilitySystem->SetStatInputs(MoveTemp(Recorded));

	const auto SentBackHolding = [&](float Held) -> float
	{
		Pool->SetClassResource(Held);
		const float Before = Attacker.Vitals->GetHealth();
		const float Dealt = UCataclysmSkillEffects::ApplyHit(
			Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
			FGameplayTagContainer(), FCataclysmHitDelivery());
		if (Dealt <= 0.0f)
		{
			AddError(TEXT("The hit did not land, so nothing could come back."));
		}
		return Before - Attacker.Vitals->GetHealth();
	};

	// AN EMPTY BAR SENDS BACK THE PLAIN FIGURE, asserted first so the two below
	// are evidence of the bonus rather than of anything else about the hit.
	TestEqual(TEXT("holding nothing sends back the attribute's own figure"),
		SentBackHolding(0.0f), 100.0f, 0.01f);

	TestEqual(TEXT("holding fifty sends back half as much again"),
		SentBackHolding(50.0f), 150.0f, 0.01f);

	TestEqual(TEXT("and a full bar of a hundred doubles it"),
		SentBackHolding(100.0f), 200.0f, 0.01f);

	// AND A DEFENDER WITH NO SUCH BONUS IS UNCHANGED, which is every character
	// in the game that has not spent this point. Its ability system records no
	// inputs for the stat at all, so the attribute is the whole answer.
	FRetaliator Plain(World);
	Plain.Combat->SetRetaliation(100.0f);
	const float Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Plain.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	TestEqual(TEXT("a defender with nothing recorded sends back the attribute"),
		Before - Attacker.Vitals->GetHealth(), 100.0f, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
