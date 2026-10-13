// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the real minion that takes the retaliation its own blow provokes.
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
// For the collision an area search needs to find anything at all. Issue #1047.
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
// For the leech a retaliating character may take from what it sent. Issue #1048.
#include "AbilitySystem/CataclysmLeech.h"
#include "Misc/ScopeExit.h"
// For retaliation the struck character cannot mitigate, ruled 2026-10-12: the payment itself, the calculation
// called with its rolls pinned, a creature that takes no damage, and the console variables that pin a roll.
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "HAL/IConsoleManager.h"

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
		explicit FRetaliator(UWorld* World,
							 const FVector& Where = FVector::ZeroVector)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
			check(Actor);

			// A SPHERE ON THE PAWN CHANNEL, SO AN AREA SEARCH CAN FIND IT.
			// Issue #1047. A bare AActor has no collision at all, so
			// `OverlapMultiByObjectType` returns nothing for it however close it
			// stands, and Reprisal Wave would look like it struck nobody when
			// what really happened is that nobody was there to be found. The
			// Pawn channel is what `UCataclysmTargeting::Gather` queries.
			//
			// EVERY COMBATANT HERE GETS ONE, INCLUDING THE ONES IN THE OLDER
			// TESTS, and none of those behave differently for it: no area search
			// runs at all unless the radius stat is above zero, and it is zero
			// for every character in the game without one capstone option.
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Where);

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

	/** Centimetres in a metre, so the tests read like the node text does. */
	constexpr float RetaliationMetre = 100.0f;

	/** How much health one combatant lost since a figure was taken. */
	float RetaliationHealthLostSince(const FRetaliator& Who, float Before)
	{
		return Before - Who.Vitals->GetHealth();
	}
}

#define CATACLYSM_RETALIATION_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationHitsBackTest,
	"Cataclysm.Retaliation.AHitThatGotThroughSendsAShareOfItBack")
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
	Defender.Combat->SetRetaliation(20.0f);

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
	Defender.Combat->SetRetaliation(20.0f);
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
		Before - Attacker.Vitals->GetHealth(), 100.0f, 0.01f);

	// A SHARE OF THE HIT AND NOT A FLAT AMOUNT, WHICH IS ISSUE #1227 AND IS
	// THE OPPOSITE OF WHAT THIS TEST USED TO ASSERT. It read "a hit five
	// times the size sends back the same amount", because the class stat
	// table wrote retaliation as a bare 158 while writing damage reduction as
	// "8%". A flat number could not follow enemy health upwards: at difficulty
	// tier 8 a Common enemy has 3,238 health and a Boss 40,048, and a
	// character wearing the affix on every piece that takes it reached 291 a
	// blow -- 137 blows taken to kill one Boss.
	Attacker.Combat->SetAttackDamage(2'500.0f);
	Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	TestEqual(TEXT("a hit five times the size sends back five times as much"),
		Before - Attacker.Vitals->GetHealth(), 500.0f, 0.01f);

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
	Attacker.Combat->SetRetaliation(20.0f);
	Defender.Combat->SetRetaliation(20.0f);

	const float AttackerBefore = Attacker.Vitals->GetHealth();
	const float DefenderBefore = Defender.Vitals->GetHealth();

	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());

	// THE ATTACKER TAKES ONE LOT AND NOT TWO. Its own retaliation must not
	// answer what the defender sent back.
	TestEqual(TEXT("the attacker takes the defender's retaliation once"),
		AttackerBefore - Attacker.Vitals->GetHealth(), 100.0f, 0.01f);

	// AND THE DEFENDER TOOK ONLY THE ORIGINAL BLOW. If retaliation were a hit,
	// the attacker's own would have come back to the defender on top of it.
	const float DefenderTook = DefenderBefore - Defender.Vitals->GetHealth();
	TestTrue(FString::Printf(
		TEXT("the defender took the blow and nothing more: %.1f"), DefenderTook),
		DefenderTook > 0.0f);
	TestTrue(TEXT("and not the attacker's retaliation on top of it"),
		!FMath::IsNearlyEqual(DefenderTook, 500.0f + 100.0f, 0.01f));

	// ARMOUR REDUCES IT NOW, AND THIS ASSERTION IS THE REVERSE OF WHAT IT WAS.
	// Issue #1227. Retaliation used to be written straight to the target's
	// health, so nothing the target wore touched it; it goes through the
	// ordinary damage formula now, so armour, resistance, evasion and block all
	// apply. What it still cannot do is critically strike, apply an ailment,
	// carry the weapon's sub-type, or provoke retaliation back -- the four flags
	// UCataclysmRetaliation::Pay sets on the blow, and the rest of this test is
	// what checks the last of them.
	//
	// A RELATIONSHIP AND NOT A CONSTANT. What 5,000 armour leaves of a 100
	// point blow is the armour curve's answer, and writing that number here
	// would make this test a copy of the curve rather than a check that armour
	// reached the blow at all. It would also have to be rewritten every time the
	// curve moved, which is how a test stops being read.
	Attacker.Combat->SetArmor(5'000.0f);
	const float ArmouredBefore = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	const float ThroughArmour = ArmouredBefore - Attacker.Vitals->GetHealth();

	TestTrue(FString::Printf(
		TEXT("armour on the attacker reduces what comes back: %.1f of 100"),
		ThroughArmour), ThroughArmour < 100.0f);

	// AND DOES NOT STOP IT ALTOGETHER, which is the half a one-sided assertion
	// would miss: a change that made retaliation deal nothing at all would pass
	// the line above and fail here.
	TestTrue(FString::Printf(
		TEXT("but does not stop it altogether: %.1f"), ThroughArmour),
		ThroughArmour > 0.0f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationExclusionsTest,
	"Cataclysm.Retaliation.ADamageOverTimeTickProvokesNoneWhileAnOrdinaryBlowDoes")
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
	Defender.Combat->SetRetaliation(20.0f);

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

	// AND AN ORDINARY BLOW FROM THE SAME ATTACKER DOES PROVOKE ONE, which is what
	// makes the reading above evidence about ticks rather than about retaliation
	// being broken for this pair of characters.
	Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	TestEqual(TEXT("while the same blow that is not a tick does"),
		Before - Attacker.Vitals->GetHealth(), 100.0f, 0.01f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationMinionTest,
	"Cataclysm.Retaliation.AMinionTakesTheRetaliationItsOwnBlowProvokes")
{
	using namespace CataclysmRetaliationTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	// THE MINION TAKES IT, NOT THE SUMMONER, and this case replaces one that
	// asserted the opposite. Until 2026-09-17 a minion struck in its summoner's
	// name, retaliation is paid back to whoever dealt the blow, and a summoner
	// standing well away from the fight would have taken damage for every blow
	// its minions landed -- so a minion's blow was excluded from provoking any.
	// The minion deals its own blow now, and the project owner decided on
	// 2026-09-18 that it takes what comes back, as the thing that swung would in
	// any other case. Issue #1515.
	//
	// THE OLD CASE BUILT A DELIVERY BY HAND. It set the five flags a minion's
	// blow used to carry and struck with the summoner as the attacker, so it
	// measured a shape rather than a minion. This one summons a real Imp and
	// tells it to attack, so it measures what the game does.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRetaliator Summoner(World);
	FRetaliator Defender(World, FVector(300.0f, 0.0f, 0.0f));

	Summoner.Combat->SetAttackDamage(500.0f);
	Defender.Combat->SetRetaliation(20.0f);

	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
		Summoner.Actor, FVector(200.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f,
		/*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
	if (!TestNotNull(TEXT("a minion"), Imp))
	{
		return false;
	}
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };

	UCataclysmAbilitySystemComponent* ImpSystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Imp));
	if (!TestNotNull(TEXT("with an ability system of its own"), ImpSystem))
	{
		return false;
	}

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float ImpBefore = ImpSystem->GetNumericAttribute(Health);
	const float SummonerBefore = Summoner.Vitals->GetHealth();
	const float DefenderBefore = Defender.Vitals->GetHealth();

	Imp->AttackTarget(Defender.Actor);

	// THE BLOW HAS TO HAVE LANDED FIRST, or a minion that hurt nothing would
	// pass every reading below for the wrong reason.
	if (!TestTrue(FString::Printf(TEXT("the imp's blow landed: %.2f"),
								  DefenderBefore - Defender.Vitals->GetHealth()),
				  Defender.Vitals->GetHealth() < DefenderBefore))
	{
		return false;
	}

	TestTrue(FString::Printf(TEXT("and the imp took what came back: %.2f"),
							 ImpBefore - ImpSystem->GetNumericAttribute(Health)),
			 ImpSystem->GetNumericAttribute(Health) < ImpBefore);

	TestEqual(TEXT("while its summoner, standing apart from the fight, took nothing"),
			  Summoner.Vitals->GetHealth(), SummonerBefore, 0.001f);

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
	Defender.Combat->SetRetaliation(20.0f);

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
	Defender.Combat->SetRetaliation(20.0f);

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
	// TWENTY, NOT A HUNDRED, SINCE ISSUE #1227. Retaliation is percentage
	// points now, so the three figures below are 20, 30 and 40 per cent of the
	// 500 blow rather than three flat amounts. The expected 100, 150 and 200 are
	// unchanged, because the bonus is a ratio and ratios do not care.
	Inputs.Base = 20.0f;
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
	Plain.Combat->SetRetaliation(20.0f);
	const float Before = Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Plain.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	TestEqual(TEXT("a defender with nothing recorded sends back the attribute"),
		Before - Attacker.Vitals->GetHealth(), 100.0f, 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// Reprisal Wave: The First Vow's second option. Issue #1047.
// --------------------------------------------------------------------------

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationWaveTest,
	"Cataclysm.Retaliation.AWaveStrikesEveryEnemyWithinItsRadius")
{
	using namespace CataclysmRetaliationTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	constexpr float M = RetaliationMetre;

	// THE DEFENDER STANDS AT THE ORIGIN, because the sphere is centred on the
	// retaliating character rather than on whatever hit it.
	FRetaliator Defender(World, FVector::ZeroVector);
	FRetaliator Attacker(World, FVector(1 * M, 0.0f, 0.0f));
	FRetaliator Near(World, FVector(2 * M, 0.0f, 0.0f));
	FRetaliator Far(World, FVector(6 * M, 0.0f, 0.0f));

	Attacker.Combat->SetAttackDamage(500.0f);
	Defender.Combat->SetRetaliation(20.0f);

	const auto StrikeTheDefender = [&]
	{
		return UCataclysmSkillEffects::ApplyHit(
			Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
			FGameplayTagContainer(), FCataclysmHitDelivery());
	};

	// WITHOUT THE OPTION, NOBODY BUT THE ATTACKER TAKES ANYTHING, asserted first
	// so the readings below are evidence of the option rather than of two
	// bystanders happening to stand near a fight.
	float AttackerBefore = Attacker.Vitals->GetHealth();
	float NearBefore = Near.Vitals->GetHealth();
	float FarBefore = Far.Vitals->GetHealth();

	if (!TestTrue(TEXT("the first blow landed"), StrikeTheDefender() > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("the attacker takes the retaliation"),
		RetaliationHealthLostSince(Attacker, AttackerBefore), 100.0f, 0.01f);
	TestEqual(TEXT("and a bystander two metres away takes nothing"),
		RetaliationHealthLostSince(Near, NearBefore), 0.0f, 0.001f);
	TestEqual(TEXT("and one six metres away takes nothing"),
		RetaliationHealthLostSince(Far, FarBefore), 0.0f, 0.001f);

	// AND WITH IT, EVERY ENEMY INSIDE FOUR METRES TAKES IT TOO. "Your
	// retaliation damage strikes every enemy within 4 metres, not only the one
	// that hit you."
	Defender.Combat->SetRetaliationRadiusMetres(4.0f);

	AttackerBefore = Attacker.Vitals->GetHealth();
	NearBefore = Near.Vitals->GetHealth();
	FarBefore = Far.Vitals->GetHealth();

	if (!TestTrue(TEXT("the second blow landed"), StrikeTheDefender() > 0.0f))
	{
		return false;
	}

	// THE WHOLE AMOUNT AND NOT A SHARE OF IT. Retaliation is a flat amount by
	// design and nothing in the option's sentence divides it; splitting it among
	// the targets would leave the total unchanged and make the option worth
	// nothing at all.
	TestEqual(TEXT("a bystander two metres away now takes the whole amount"),
		RetaliationHealthLostSince(Near, NearBefore), 100.0f, 0.01f);

	// AND THE ATTACKER TAKES ONE LOT AND NOT TWO. It is standing one metre away,
	// so the search returns it as well as it being the thing that hit; without
	// the check that drops it from the search's results it would pay twice.
	TestEqual(TEXT("and the attacker still takes exactly one payment"),
		RetaliationHealthLostSince(Attacker, AttackerBefore), 100.0f, 0.01f);

	// AND SIX METRES IS OUTSIDE FOUR.
	TestEqual(TEXT("and one six metres away still takes nothing"),
		RetaliationHealthLostSince(Far, FarBefore), 0.0f, 0.001f);

	// AND THE WAVE IS NOT ITSELF A HIT, so a bystander that also retaliates
	// sends nothing back. Without that, two retaliating characters standing near
	// one another would reflect without end.
	Near.Combat->SetRetaliation(20.0f);
	const float DefenderBefore = Defender.Vitals->GetHealth();
	NearBefore = Near.Vitals->GetHealth();

	if (!TestTrue(TEXT("the third blow landed"), StrikeTheDefender() > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("a bystander the wave struck takes it"),
		RetaliationHealthLostSince(Near, NearBefore), 100.0f, 0.01f);

	// The defender took the attacker's blow and NOT the bystander's retaliation
	// on top of it. 500 attack damage against no armour or resistance.
	TestEqual(TEXT("and the wave provokes no retaliation of its own"),
		RetaliationHealthLostSince(Defender, DefenderBefore), 500.0f, 0.01f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationWaveKeepsTheAttackerTest,
	"Cataclysm.Retaliation.AWaveStillStrikesAnAttackerStandingOutsideIt")
{
	using namespace CataclysmRetaliationTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	constexpr float M = RetaliationMetre;

	// A RANGED ATTACKER TWENTY METRES AWAY, which is the case the word "only"
	// decides. "strikes every enemy within 4 metres, NOT ONLY the one that hit
	// you" adds to that target rather than replacing it, so an attacker outside
	// the radius still takes retaliation exactly as it did before the option
	// existed. Reading it the other way would make the option a DOWNGRADE
	// against anything at range, and all twelve capstone options were rewritten
	// on 2026-08-27 as pure upgrades with no drawbacks.
	FRetaliator Defender(World, FVector::ZeroVector);
	FRetaliator Attacker(World, FVector(20 * M, 0.0f, 0.0f));
	FRetaliator Near(World, FVector(2 * M, 0.0f, 0.0f));

	Attacker.Combat->SetAttackDamage(500.0f);
	Defender.Combat->SetRetaliation(20.0f);
	Defender.Combat->SetRetaliationRadiusMetres(4.0f);

	const float AttackerBefore = Attacker.Vitals->GetHealth();
	const float NearBefore = Near.Vitals->GetHealth();

	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	if (!TestTrue(FString::Printf(TEXT("the blow landed (%.1f)"), Dealt),
				  Dealt > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("an attacker twenty metres away still takes retaliation"),
		RetaliationHealthLostSince(Attacker, AttackerBefore), 100.0f, 0.01f);
	TestEqual(TEXT("and the enemy standing beside the defender takes it too"),
		RetaliationHealthLostSince(Near, NearBefore), 100.0f, 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// Feeding Wound: The Second Vow's second option. Issue #1048.
// --------------------------------------------------------------------------

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationLeechTest,
	"Cataclysm.Retaliation.LifeLeechReachesItOnlyForACharacterThatBoughtThat")
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
	Defender.Combat->SetRetaliation(20.0f);
	Defender.Vitals->SetLifeLeech(10.0f);

	const auto StrikeTheDefender = [&]
	{
		return UCataclysmSkillEffects::ApplyHit(
			Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
			FGameplayTagContainer(), FCataclysmHitDelivery());
	};

	// LEECH REACHES RETALIATION FOR NOBODY BY DEFAULT, however much of it the
	// character has. Retaliation is deliberately not a hit -- it writes health
	// directly so that it cannot critically strike, cannot apply an ailment and
	// cannot be retaliated against -- and leech is worked out where a hit lands.
	// Path of Exile and Last Epoch both take that position for reflected damage.
	if (!TestTrue(TEXT("the first blow landed"), StrikeTheDefender() > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("a defender without the option is promised no leech"),
		Defender.AbilitySystem->GetLeechPayments().Num(), 0);

	// AND THE ATTACKER'S OWN LEECH STILL WORKS, which is what says the check
	// above is about retaliation rather than about leech being broken outright.
	Attacker.Vitals->SetLifeLeech(10.0f);
	if (!TestTrue(TEXT("the second blow landed"), StrikeTheDefender() > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the attacker is promised leech for the blow it landed"),
		Attacker.AbilitySystem->GetLeechPayments().Num(), 1);

	// AND WITH THE OPTION, RETALIATION LEECHES. "Your life leech applies to your
	// retaliation damage as well as to your attacks." Ten per cent of the 100
	// the attacker took is 10.
	Defender.Combat->SetRetaliationLeeches(1.0f);
	if (!TestTrue(TEXT("the third blow landed"), StrikeTheDefender() > 0.0f))
	{
		return false;
	}

	const TArray<FCataclysmLeechPayment>& Promised =
		Defender.AbilitySystem->GetLeechPayments();
	if (!TestEqual(TEXT("the defender is promised exactly one payment"),
				   Promised.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("and it is ten per cent of what the retaliation took"),
		Promised[0].Remaining, 10.0f, 0.01f);

	// HEALTH AND NOT ONE OF THE OTHER TWO POOLS. The node names LIFE leech, so
	// mana leech and energy shield leech stay on attacks alone.
	TestTrue(TEXT("and it fills health"),
		Promised[0].Pool == ECataclysmLeechPool::Health);

	// AND IT ARRIVES OVER THREE SECONDS LIKE ANY OTHER LEECH, rather than at
	// once. The design: "Instant leech makes a character that is winning
	// unkillable and does nothing for one that is losing."
	TestEqual(TEXT("and it is paid out over three seconds"),
		Promised[0].SecondsLeft, UCataclysmLeech::PayoutSeconds, 0.001f);

	// AND MANA LEECH DOES NOT COME WITH IT. The defender has plenty and is
	// promised nothing more than the one health payment above.
	Defender.Vitals->SetManaLeech(50.0f);
	Defender.Vitals->SetEnergyShieldLeech(50.0f);
	Defender.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>());
	if (!TestTrue(TEXT("the fourth blow landed"), StrikeTheDefender() > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("mana leech and energy shield leech do not follow it"),
		Defender.AbilitySystem->GetLeechPayments().Num(), 1);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationLeechCountsWhatLandedTest,
	"Cataclysm.Retaliation.LeechFromAWaveCountsWhatEachEnemyActuallyLost")
{
	using namespace CataclysmRetaliationTest;

	CataclysmTestWorld::SilenceCriticalStrikes();

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	constexpr float M = RetaliationMetre;

	// BOTH OPTIONS AT ONCE, which a character may hold because they belong to
	// different capstones: Reprisal Wave is The First Vow's second option and
	// Feeding Wound is The Second Vow's.
	FRetaliator Defender(World, FVector::ZeroVector);
	FRetaliator Attacker(World, FVector(1 * M, 0.0f, 0.0f));
	FRetaliator Dying(World, FVector(2 * M, 0.0f, 0.0f));

	Attacker.Combat->SetAttackDamage(500.0f);
	Defender.Combat->SetRetaliation(20.0f);
	Defender.Combat->SetRetaliationRadiusMetres(4.0f);
	Defender.Combat->SetRetaliationLeeches(1.0f);
	Defender.Vitals->SetLifeLeech(10.0f);

	// AND ONE OF THE TWO HAS ALMOST NOTHING LEFT, which is the overkill rule:
	// "An enemy with 25 health left, hit for 400, contributes 25 to the leech
	// calculation and not 400."
	Dying.Vitals->SetHealth(25.0f);

	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	if (!TestTrue(FString::Printf(TEXT("the blow landed (%.1f)"), Dealt),
				  Dealt > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("the enemy with 25 health left loses 25 and not 100"),
		Dying.Vitals->GetHealth(), 0.0f, 0.001f);

	const TArray<FCataclysmLeechPayment>& Promised =
		Defender.AbilitySystem->GetLeechPayments();
	if (!TestEqual(TEXT("one payment covers the whole wave"), Promised.Num(), 1))
	{
		return false;
	}

	// EVERY ENEMY THE WAVE STRUCK CONTRIBUTES, because leech is a percentage of
	// the damage actually dealt and each of them took some. Ten per cent of
	// 158 + 25.
	TestEqual(TEXT("and it is ten per cent of what the two of them really lost"),
		Promised[0].Remaining, 12.5f, 0.01f);

	return true;
}

// --------------------------------------------------------------------------
// Retaliation the struck character cannot reduce or mitigate. Ruled 2026-10-12
// under the owner's delegation, for "Retaliation damage cannot be reduced or
// mitigated by the attacker".
// --------------------------------------------------------------------------

/**
 * WHAT THESE SIX TESTS HOLD. A retaliating character whose rows hold the flag
 * `retaliation_cannot_be_mitigated` sends a blow that skips six steps of the
 * struck character's: evasion, block, armour, resistance, damage reduction and
 * `damage_taken`. THE FLAG NEVER LOWERS A PAYMENT: a resistance below nought
 * and a net increase in `damage_taken` raise a holder's blow as they raise any
 * other. It still meets the temporary absorb and the energy shield,
 * does nothing to a creature that takes no damage or inside a window that
 * refuses damage, and still cannot critically strike or leech.
 *
 * EVERY FIGURE IS READ AGAINST THE SAME ATTACKER STRUCK BY A RETALIATOR WITHOUT
 * THE FLAG, AND THAT CONTROL IS READ FIRST. Two retaliators stand in each test,
 * `Plain` and `Holder`, alike but for the flag.
 *
 * MOST PAYMENTS ARE MADE BY CALLING `UCataclysmRetaliation::Pay`, with a blow
 * of 500 stated, so the amount sent is 100 whatever the attacker's own hit
 * would have resolved to. The first test also provokes one payment with a real
 * hit, and the reflected hit is a real hit.
 *
 * STANDING: the two retaliators 3 m apart, the attacker 3 m from the first and
 * 4.2 m from the second. No area is searched, because no retaliator here has a
 * radius.
 *
 * NO TEST WAITS ON A TICK, and the two windows that refuse damage are opened
 * and read at one time of the world's clock.
 */
namespace CataclysmRetaliationUnmitigatedTest
{
	using CataclysmRetaliationTest::FRetaliator;

	/** The blow each payment is for, the retaliation both retaliators hold, and what that sends. */
	constexpr float BlowTaken = 500.0f;
	constexpr float RetaliationHeld = 20.0f;
	constexpr float Sent = 100.0f;

	/** Pins one console variable at the console's priority until it goes out of scope. */
	struct FUnmitigatedPinned
	{
		FUnmitigatedPinned(const TCHAR* Name, float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FUnmitigatedPinned()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/**
	 * Writes `Stats` as the WHOLE stat line of `Who`, each a flat figure on a base of nought, which is what a worn
	 * row's line on a stat with no attribute is. A stat not named here is then read off its gameplay attribute.
	 */
	void UnmitigatedHoldFlats(const FRetaliator& Who, const TMap<FName, float>& Stats)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		for (const TPair<FName, float>& Stat : Stats)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Stat.Value;
			FCataclysmStatInputs& Line = Lines.FindOrAdd(Stat.Key);
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		}
		Who.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	}

	FName UnmitigatedFlag()
	{
		return FName(UCataclysmRetaliation::CannotBeMitigatedStat);
	}

	/** Two retaliators alike but for the flag, and the attacker both are paid against. */
	struct FUnmitigatedThree
	{
		explicit FUnmitigatedThree(UWorld* World)
			: Plain(World, FVector(0.0f, 0.0f, 0.0f))
			, Holder(World, FVector(0.0f, 300.0f, 0.0f))
			, Attacker(World, FVector(300.0f, 0.0f, 0.0f))
		{
			Plain.Combat->SetRetaliation(RetaliationHeld);
			Holder.Combat->SetRetaliation(RetaliationHeld);
			UnmitigatedHoldFlats(Holder, {{UnmitigatedFlag(), 1.0f}});
		}

		FRetaliator Plain;
		FRetaliator Holder;
		FRetaliator Attacker;
	};

	float UnmitigatedHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** What one payment of `Who`'s retaliation, for a blow of 500 struck by `Struck`, takes from `Struck`'s health. */
	float HealthTakenByOnePayment(const FRetaliator& Who, AActor* Struck)
	{
		const float Before = UnmitigatedHealthOf(Struck);
		UCataclysmRetaliation::Pay(Who.AbilitySystem.Get(), Who.Actor.Get(), Struck, BlowTaken);
		return Before - UnmitigatedHealthOf(Struck);
	}

	/** A creature with 100,000 health and no armour, evasion, block or resistance, as `CreatureBeside` makes one. */
	ACataclysmEnemyCharacter* UnmitigatedCreatureAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetHealth(100000.0f);
			Made->SetArmour(0.0f);
			if (UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent())
			{
				Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
				Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
				Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
				Its->SetNumericAttributeBase(
					UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
			}
		}
		return Made;
	}
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationUnmitigatedSixTest,
	"Cataclysm.RetaliationUnmitigated.AHolderOfTheFlagSendsTheWholeAmountPastEachOfTheAttackersSixDefences")
{
	using namespace CataclysmRetaliationUnmitigatedTest;

	// THE TWO ROLLS THAT CAN BE PINNED HERE ARE PINNED. The block roll at 0 blocks
	// whenever the struck character has any chance to. THE EVASION ROLL CANNOT BE
	// PINNED THROUGH A REAL BLOW: `UCataclysmVitalAttributeSet` passes the
	// calculation no evasion roll. So the attacker below holds an evasion of 200,
	// which every roll from 0 to 100 is below: `PreAttributeChange` on the combat
	// set floors evasion at nought and does not cap it. The calculation's own
	// test further down pins that roll at 0 against an evasion of 50.
	const FUnmitigatedPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FUnmitigatedPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FUnmitigatedThree Three(World);
	UCataclysmAbilitySystemComponent* Struck = Three.Attacker.AbilitySystem.Get();
	AActor* StruckActor = Three.Attacker.Actor.Get();

	TestFalse(TEXT("set-up: the plain retaliator does not hold the flag"),
		UCataclysmRetaliation::CannotBeMitigatedFor(Three.Plain.AbilitySystem.Get()));
	if (!TestTrue(TEXT("set-up: the holder holds the flag"),
			UCataclysmRetaliation::CannotBeMitigatedFor(Three.Holder.AbilitySystem.Get())))
	{
		return false;
	}

	// THE CONTROL, READ FIRST: against an attacker with no defence at all the
	// flag changes nothing. Both payments take the whole 100.
	const float PlainOpen = HealthTakenByOnePayment(Three.Plain, StruckActor);
	const float HolderOpen = HealthTakenByOnePayment(Three.Holder, StruckActor);
	TestEqual(TEXT("control: an attacker with no defence takes the whole 100 from the plain retaliator"),
		PlainOpen, Sent, 0.01f);
	if (!TestEqual(TEXT("control: and the same from the holder, so the flag adds nothing of its own"),
			HolderOpen, PlainOpen, 0.01f))
	{
		return false;
	}

	// 1. EVASION THAT WOULD EVADE.
	const FGameplayAttribute Evasion = UCataclysmCombatAttributeSet::GetEvasionAttribute();
	Struck->SetNumericAttributeBase(Evasion, 200.0f);
	const float PlainPastEvasion = HealthTakenByOnePayment(Three.Plain, StruckActor);
	const bool bPlainWasEvaded = Struck->GetLastResolvedHit().bEvaded;
	const float HolderPastEvasion = HealthTakenByOnePayment(Three.Holder, StruckActor);
	const bool bHolderWasEvaded = Struck->GetLastResolvedHit().bEvaded;
	Struck->SetNumericAttributeBase(Evasion, 0.0f);

	TestTrue(TEXT("control: an attacker with an evasion of 200 evades the plain retaliator's payment"),
		bPlainWasEvaded);
	TestEqual(TEXT("control: and takes nothing from it"), PlainPastEvasion, 0.0f, 0.001f);
	TestFalse(TEXT("the holder's payment is not evaded"), bHolderWasEvaded);
	TestEqual(TEXT("and takes what the undefended attacker took"), HolderPastEvasion, HolderOpen, 0.01f);

	// 2. BLOCK THAT WOULD BLOCK. A chance of 50 with the roll pinned at 0.
	const FGameplayAttribute BlockChance = UCataclysmCombatAttributeSet::GetBlockChanceAttribute();
	Struck->SetNumericAttributeBase(BlockChance, 50.0f);
	const float PlainPastBlock = HealthTakenByOnePayment(Three.Plain, StruckActor);
	const bool bPlainWasBlocked = Struck->GetLastResolvedHit().bBlocked;
	const float HolderPastBlock = HealthTakenByOnePayment(Three.Holder, StruckActor);
	const bool bHolderWasBlocked = Struck->GetLastResolvedHit().bBlocked;
	Struck->SetNumericAttributeBase(BlockChance, 0.0f);

	TestTrue(TEXT("control: an attacker with a block chance blocks the plain retaliator's payment"),
		bPlainWasBlocked);
	TestEqual(TEXT("control: and the block removes its share, half"),
		PlainPastBlock,
		HolderOpen * (1.0f - UCataclysmDamageCalculation::BlockDamageReduction / 100.0f), 0.01f);
	TestFalse(TEXT("the holder's payment is not blocked"), bHolderWasBlocked);
	TestEqual(TEXT("and takes what the undefended attacker took"), HolderPastBlock, HolderOpen, 0.01f);

	// 3 TO 6. ARMOUR, RESISTANCE, BOTH LINES OF DAMAGE REDUCTION, AND DAMAGE
	// TAKEN. One at a time, each put back before the next. `PlainShare` is what
	// the plain payment is of the holder's; below nought means only "less, and
	// not nothing", because what armour removes is the armour curve's answer and
	// is not copied here.
	//
	// THE FLAG NEVER LOWERS A PAYMENT, ruled 2026-10-12, AND TWO LINES READ THAT
	// THROUGH A REAL PAYMENT. An attacker at a resistance of -50, and one whose
	// damage taken stands at 150, each take 150 from the plain retaliator and
	// the same 150 from the holder. `HolderShare` is what the holder's payment is
	// of the undefended one: 1 where the defence would lower a blow and is
	// skipped, and the plain payment's own share where the figure raises a blow
	// and is applied.
	struct FDefence
	{
		const TCHAR* Name;
		FGameplayAttribute Attribute;
		float Held;
		float PlainShare;
		float HolderShare;
	};
	const FDefence Defences[] = {
		{TEXT("5,000 armour"), UCataclysmCombatAttributeSet::GetArmorAttribute(), 5000.0f, -1.0f, 1.0f},
		{TEXT("a resistance of 50 to everything, which an untyped blow meets"),
		 UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 50.0f, 0.5f, 1.0f},
		{TEXT("a resistance of -50 to everything, which raises a blow"),
		 UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), -50.0f, 1.5f, 1.5f},
		{TEXT("a damage reduction of 50"),
		 UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 50.0f, 0.5f, 1.0f},
		{TEXT("a multiplicative damage reduction of 50"),
		 UCataclysmCombatAttributeSet::GetDamageReductionMoreAttribute(), 50.0f, 0.5f, 1.0f},
		{TEXT("damage taken at 50, a reduction"),
		 UCataclysmCombatAttributeSet::GetDamageTakenAttribute(), 50.0f, 0.5f, 1.0f},
		{TEXT("damage taken at 150, a net increase"),
		 UCataclysmCombatAttributeSet::GetDamageTakenAttribute(), 150.0f, 1.5f, 1.5f},
	};
	for (const FDefence& Defence : Defences)
	{
		const float Was = Struck->GetNumericAttribute(Defence.Attribute);
		Struck->SetNumericAttributeBase(Defence.Attribute, Defence.Held);
		const float PlainTook = HealthTakenByOnePayment(Three.Plain, StruckActor);
		const float HolderTook = HealthTakenByOnePayment(Three.Holder, StruckActor);
		Struck->SetNumericAttributeBase(Defence.Attribute, Was);

		if (Defence.PlainShare < 0.0f)
		{
			TestTrue(*FString::Printf(TEXT("control, %s: the plain payment is reduced and not stopped: %.2f"),
						 Defence.Name, PlainTook),
				PlainTook > 0.0f && PlainTook < HolderOpen - 1.0f);
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("control, %s: the plain payment is %.2f of the undefended one"),
						  Defence.Name, Defence.PlainShare),
				PlainTook, HolderOpen * Defence.PlainShare, 0.01f);
		}
		if (Defence.HolderShare > 1.0f)
		{
			TestEqual(*FString::Printf(TEXT("%s: the holder's payment takes the same as the plain one, so the "
											"flag does not lower it"), Defence.Name),
				HolderTook, PlainTook, 0.01f);
			TestEqual(*FString::Printf(TEXT("%s: and that is %.2f of the undefended one"),
						  Defence.Name, Defence.HolderShare),
				HolderTook, HolderOpen * Defence.HolderShare, 0.01f);
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("%s: the holder's payment takes what the undefended attacker took"),
						  Defence.Name),
				HolderTook, HolderOpen, 0.01f);
		}
		TestTrue(*FString::Printf(TEXT("%s: the holder's payment is never less than the plain one: %.2f and %.2f"),
					 Defence.Name, HolderTook, PlainTook),
			HolderTook >= PlainTook - 0.01f);
	}

	// AND A PAYMENT PROVOKED BY A REAL HIT, so the flag is shown to arrive where
	// `UCataclysmVitalAttributeSet` calls `Pay` and not only where a test does.
	// The attacker holds 500 attack damage and 5,000 armour and strikes each
	// retaliator once; neither retaliator has any defence, so each takes the
	// whole 500 and sends 100.
	Three.Attacker.Combat->SetAttackDamage(BlowTaken);
	Three.Attacker.Combat->SetArmor(5000.0f);
	float Before = Three.Attacker.Vitals->GetHealth();
	const float OnPlain = UCataclysmSkillEffects::ApplyHit(
		StruckActor, Three.Plain.Actor.Get(), /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	const float BackFromPlain = Before - Three.Attacker.Vitals->GetHealth();
	Before = Three.Attacker.Vitals->GetHealth();
	const float OnHolder = UCataclysmSkillEffects::ApplyHit(
		StruckActor, Three.Holder.Actor.Get(), /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	const float BackFromHolder = Before - Three.Attacker.Vitals->GetHealth();

	if (!TestTrue(*FString::Printf(TEXT("set-up: both real hits landed (%.1f and %.1f)"), OnPlain, OnHolder),
			OnPlain > 0.0f && OnHolder > 0.0f))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("control: the armoured attacker's own hit on the plain retaliator sends back "
								   "less than 100 and more than nothing: %.2f"), BackFromPlain),
		BackFromPlain > 0.0f && BackFromPlain < Sent - 1.0f);
	TestEqual(TEXT("and its hit on the holder sends back the whole 100"), BackFromHolder, Sent, 0.01f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationUnmitigatedCalculationTest,
	"Cataclysm.RetaliationUnmitigated.TheCalculationSkipsTheSixStepsForAFlaggedBlowAndKeepsTheRest")
{
	using namespace CataclysmRetaliationUnmitigatedTest;

	// THE CALCULATION CALLED DIRECTLY, WITH ALL THREE OF ITS ROLLS PASSED IN. A
	// roll of 0 succeeds against any chance above nought and a roll of 100 never
	// succeeds. Nothing here is written to the defender: `Resolve` only answers.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRetaliator Defender(World);
	UCataclysmAbilitySystemComponent* Its = Defender.AbilitySystem.Get();

	constexpr float Never = 100.0f;
	constexpr float Always = 0.0f;

	FCataclysmIncomingHit PlainHit;
	PlainHit.Damage = 100.0f;
	FCataclysmIncomingHit FlaggedHit = PlainHit;
	FlaggedHit.bCannotBeMitigated = true;

	const auto Resolved = [&](const FCataclysmIncomingHit& Hit, float EvasionRoll, float BlockRoll, float CritRoll)
	{
		return UCataclysmDamageCalculation::Resolve(Hit, Its, /*Tier=*/1, EvasionRoll, BlockRoll, CritRoll);
	};
	const auto Hold = [&](const FGameplayAttribute& Attribute, float Value)
	{
		Its->SetNumericAttributeBase(Attribute, Value);
	};

	// THE CONTROL, READ FIRST: with no defence the two blows resolve alike.
	const FCataclysmDamageResult PlainOpen = Resolved(PlainHit, Never, Never, Never);
	const FCataclysmDamageResult FlaggedOpen = Resolved(FlaggedHit, Never, Never, Never);
	TestEqual(TEXT("control: an undefended defender takes the whole 100 of the plain blow"),
		PlainOpen.DealtToHealth, 100.0f, 0.001f);
	if (!TestEqual(TEXT("control: and of the flagged blow"), FlaggedOpen.DealtToHealth, 100.0f, 0.001f))
	{
		return false;
	}

	// 1. EVASION, with the roll pinned at 0 against a chance of 50.
	Hold(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 50.0f);
	{
		const FCataclysmDamageResult Plain = Resolved(PlainHit, Always, Never, Never);
		const FCataclysmDamageResult Flagged = Resolved(FlaggedHit, Always, Never, Never);
		TestTrue(TEXT("control: a roll of 0 against an evasion of 50 evades the plain blow"), Plain.bEvaded);
		TestEqual(TEXT("control: which deals nothing"), Plain.DealtToHealth, 0.0f, 0.001f);
		TestFalse(TEXT("the flagged blow is not evaded on the same roll"), Flagged.bEvaded);
		TestEqual(TEXT("and deals the whole 100"), Flagged.DealtToHealth, 100.0f, 0.001f);
	}
	Hold(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);

	// 2. BLOCK, with the roll pinned at 0 against a chance of 50.
	Hold(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 50.0f);
	{
		const FCataclysmDamageResult Plain = Resolved(PlainHit, Never, Always, Never);
		const FCataclysmDamageResult Flagged = Resolved(FlaggedHit, Never, Always, Never);
		const float BlockShare = UCataclysmDamageCalculation::BlockDamageReduction;
		TestTrue(TEXT("control: a roll of 0 against a block chance of 50 blocks the plain blow"), Plain.bBlocked);
		TestEqual(TEXT("control: the block removes its share"), Plain.DamageBlocked, BlockShare, 0.001f);
		TestEqual(TEXT("control: and the rest reaches health"), Plain.DealtToHealth, 100.0f - BlockShare, 0.001f);
		TestFalse(TEXT("the flagged blow is not blocked on the same roll"), Flagged.bBlocked);
		TestEqual(TEXT("nothing is recorded as blocked"), Flagged.DamageBlocked, 0.0f, 0.001f);
		TestEqual(TEXT("and it deals the whole 100"), Flagged.DealtToHealth, 100.0f, 0.001f);
	}
	Hold(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);

	// 3. ARMOUR.
	Hold(UCataclysmCombatAttributeSet::GetArmorAttribute(), 5000.0f);
	{
		const FCataclysmDamageResult Plain = Resolved(PlainHit, Never, Never, Never);
		const FCataclysmDamageResult Flagged = Resolved(FlaggedHit, Never, Never, Never);
		TestTrue(*FString::Printf(TEXT("control: 5,000 armour removes some of the plain blow: %.2f"),
					 Plain.RemovedByArmour),
			Plain.RemovedByArmour > 1.0f);
		TestEqual(TEXT("control: and what is left reaches health"),
			Plain.DealtToHealth, 100.0f - Plain.RemovedByArmour, 0.001f);
		TestEqual(TEXT("armour removes nothing of the flagged blow"), Flagged.RemovedByArmour, 0.0f, 0.001f);
		TestEqual(TEXT("which deals the whole 100"), Flagged.DealtToHealth, 100.0f, 0.001f);
	}
	Hold(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);

	// 4. RESISTANCE, ABOVE NOUGHT AND BELOW IT. Ruled 2026-10-12: the flag never
	// lowers a blow, so the step is skipped only when the figure is above nought.
	// A defender whose resistance is negative takes the same raised blow from the
	// flagged one as from the plain.
	const FGameplayAttribute AllResistance = UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute();
	Hold(AllResistance, 50.0f);
	TestEqual(TEXT("control: a resistance of 50 halves the plain blow"),
		Resolved(PlainHit, Never, Never, Never).DealtToHealth, 50.0f, 0.001f);
	TestEqual(TEXT("and leaves the flagged blow whole"),
		Resolved(FlaggedHit, Never, Never, Never).DealtToHealth, 100.0f, 0.001f);
	Hold(AllResistance, -50.0f);
	TestEqual(TEXT("control: a resistance of -50 raises the plain blow by half"),
		Resolved(PlainHit, Never, Never, Never).DealtToHealth, 150.0f, 0.001f);
	TestEqual(TEXT("and raises the flagged blow by half too: a negative resistance is not skipped"),
		Resolved(FlaggedHit, Never, Never, Never).DealtToHealth, 150.0f, 0.001f);
	Hold(AllResistance, 0.0f);

	// 5. DAMAGE REDUCTION, THE CAPPED LINE AND THE MULTIPLICATIVE ONE TOGETHER.
	Hold(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 50.0f);
	Hold(UCataclysmCombatAttributeSet::GetDamageReductionMoreAttribute(), 50.0f);
	{
		const FCataclysmDamageResult Plain = Resolved(PlainHit, Never, Never, Never);
		const FCataclysmDamageResult Flagged = Resolved(FlaggedHit, Never, Never, Never);
		TestEqual(TEXT("control: 50 on each line of damage reduction leaves a quarter of the plain blow"),
			Plain.DealtToHealth, 25.0f, 0.001f);
		TestEqual(TEXT("control: and the rest is recorded as removed by damage reduction"),
			Plain.RemovedByDamageReduction, 75.0f, 0.001f);
		TestEqual(TEXT("the flagged blow is whole"), Flagged.DealtToHealth, 100.0f, 0.001f);
		TestEqual(TEXT("and nothing is recorded as removed by damage reduction"),
			Flagged.RemovedByDamageReduction, 0.0f, 0.001f);
	}
	Hold(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
	Hold(UCataclysmCombatAttributeSet::GetDamageReductionMoreAttribute(), 0.0f);

	// 6. DAMAGE TAKEN, A REDUCTION AND THEN AN INCREASE.
	const FGameplayAttribute DamageTaken = UCataclysmCombatAttributeSet::GetDamageTakenAttribute();
	Hold(DamageTaken, 50.0f);
	TestEqual(TEXT("control: damage taken at 50 halves the plain blow"),
		Resolved(PlainHit, Never, Never, Never).DealtToHealth, 50.0f, 0.001f);
	TestEqual(TEXT("and leaves the flagged blow whole"),
		Resolved(FlaggedHit, Never, Never, Never).DealtToHealth, 100.0f, 0.001f);
	Hold(DamageTaken, 150.0f);
	TestEqual(TEXT("control: damage taken at 150 raises the plain blow by half"),
		Resolved(PlainHit, Never, Never, Never).DealtToHealth, 150.0f, 0.001f);
	TestEqual(TEXT("and raises the flagged blow by half too: a net increase is not skipped"),
		Resolved(FlaggedHit, Never, Never, Never).DealtToHealth, 150.0f, 0.001f);

	// AND DAMAGE REDUCTION IS STILL SKIPPED WHOLE BESIDE AN INCREASE THAT IS KEPT.
	// Damage taken at 150 with a damage reduction of 50: the plain blow is halved
	// and then raised by half, 75; the flagged blow skips the reduction and keeps
	// the increase, 150. With a resistance of -50 as well, the flagged blow meets
	// both increases, 225, and the plain one 112.5.
	Hold(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 50.0f);
	TestEqual(TEXT("control: damage taken at 150 under a damage reduction of 50 leaves 75 of the plain blow"),
		Resolved(PlainHit, Never, Never, Never).DealtToHealth, 75.0f, 0.001f);
	TestEqual(TEXT("the flagged blow skips the reduction and keeps the increase: 150"),
		Resolved(FlaggedHit, Never, Never, Never).DealtToHealth, 150.0f, 0.001f);
	Hold(AllResistance, -50.0f);
	TestEqual(TEXT("control: with a resistance of -50 as well the plain blow is 112.5"),
		Resolved(PlainHit, Never, Never, Never).DealtToHealth, 112.5f, 0.001f);
	TestEqual(TEXT("and the flagged blow meets both increases and no reduction: 225"),
		Resolved(FlaggedHit, Never, Never, Never).DealtToHealth, 225.0f, 0.001f);
	Hold(AllResistance, 0.0f);
	Hold(UCataclysmCombatAttributeSet::GetDamageReductionAttribute(), 0.0f);
	Hold(DamageTaken, 100.0f);

	// WHAT IS NOT ONE OF THE SIX IS KEPT. The Nth hit's share on top, which the
	// defender's own worn row states and which raises a blow.
	{
		FCataclysmIncomingHit PlainNth = PlainHit;
		PlainNth.BonusDamagePercent = 50.0f;
		FCataclysmIncomingHit FlaggedNth = FlaggedHit;
		FlaggedNth.BonusDamagePercent = 50.0f;
		TestEqual(TEXT("control: the Nth hit's share of 50 raises the plain blow to 150"),
			Resolved(PlainNth, Never, Never, Never).DealtToHealth, 150.0f, 0.001f);
		TestEqual(TEXT("and raises the flagged blow to 150 as well"),
			Resolved(FlaggedNth, Never, Never, Never).DealtToHealth, 150.0f, 0.001f);
	}

	// AND THE CALCULATION ITSELF DOES NOT FORBID A CRITICAL STRIKE FOR THE FLAG.
	// That belongs to whoever is swinging. Retaliation is forbidden one by its
	// own flag, which the last test in this group reads.
	{
		FCataclysmIncomingHit PlainCritical = PlainHit;
		PlainCritical.CritChance = 100.0f;
		PlainCritical.CritMultiplier = 200.0f;
		FCataclysmIncomingHit FlaggedCritical = PlainCritical;
		FlaggedCritical.bCannotBeMitigated = true;
		const FCataclysmDamageResult Plain = Resolved(PlainCritical, Never, Never, Always);
		const FCataclysmDamageResult Flagged = Resolved(FlaggedCritical, Never, Never, Always);
		TestTrue(TEXT("control: a plain blow with a critical strike chance critically strikes on a roll of 0"),
			Plain.bWasCritical);
		TestEqual(TEXT("control: for twice the blow"), Plain.DealtToHealth, 200.0f, 0.001f);
		TestTrue(TEXT("a flagged blow with the same chance does too"), Flagged.bWasCritical);
		TestEqual(TEXT("for the same figure"), Flagged.DealtToHealth, 200.0f, 0.001f);
	}

	// AND THE POOLS STILL TAKE IT, AFTER THE SKIPPED STEPS. 5,000 armour, a
	// temporary absorb of 40 and an energy shield of 30: the flagged blow of 100
	// puts 40 to the absorb, 30 to the shield and 30 to health. The plain blow is
	// cut by the armour first, so the three figures come to less than 100.
	Hold(UCataclysmCombatAttributeSet::GetArmorAttribute(), 5000.0f);
	Hold(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 1000.0f);
	Hold(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 30.0f);
	Its->GrantTemporaryAbsorb(40.0f);
	if (!TestEqual(TEXT("set-up: the defender holds a temporary absorb of 40"),
			Its->TemporaryAbsorbHeld(), 40.0f, 0.001f)
		|| !TestEqual(TEXT("set-up: and an energy shield of 30"),
			Defender.Vitals->GetEnergyShield(), 30.0f, 0.001f))
	{
		return false;
	}
	{
		const FCataclysmDamageResult Plain = Resolved(PlainHit, Never, Never, Never);
		const FCataclysmDamageResult Flagged = Resolved(FlaggedHit, Never, Never, Never);
		const float PlainAltogether = Plain.AbsorbedByTemporary + Plain.AbsorbedByShield + Plain.DealtToHealth;
		TestTrue(*FString::Printf(TEXT("control: the armour cuts the plain blow before any pool: %.2f in all"),
					 PlainAltogether),
			PlainAltogether > 0.0f && PlainAltogether < 99.0f);
		TestEqual(TEXT("the flagged blow puts 40 to the temporary absorb"),
			Flagged.AbsorbedByTemporary, 40.0f, 0.001f);
		TestEqual(TEXT("30 to the energy shield"), Flagged.AbsorbedByShield, 30.0f, 0.001f);
		TestEqual(TEXT("and the 30 left to health"), Flagged.DealtToHealth, 30.0f, 0.001f);
	}

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationUnmitigatedPoolsTest,
	"Cataclysm.RetaliationUnmitigated.AHoldersPaymentStillMeetsTheTemporaryAbsorbAndTheEnergyShieldAndManaTakesOnlyATick")
{
	using namespace CataclysmRetaliationUnmitigatedTest;

	const FUnmitigatedPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FUnmitigatedPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FUnmitigatedThree Three(World);
	UCataclysmAbilitySystemComponent* Struck = Three.Attacker.AbilitySystem.Get();
	AActor* StruckActor = Three.Attacker.Actor.Get();
	const FGameplayAttribute Shield = UCataclysmVitalAttributeSet::GetEnergyShieldAttribute();
	const FGameplayAttribute Mana = UCataclysmVitalAttributeSet::GetManaAttribute();

	// THE ATTACKER HOLDS 5,000 ARMOUR THROUGHOUT, so the flag has a step to skip
	// in every reading below.
	Three.Attacker.Combat->SetArmor(5000.0f);

	// THE TEMPORARY ABSORB. 40 held before each payment. The plain payment is cut
	// by the armour first; the holder's 100 puts 40 to the absorb and 60 to health.
	Struck->GrantTemporaryAbsorb(40.0f);
	const float PlainPastAbsorb = HealthTakenByOnePayment(Three.Plain, StruckActor);
	const float AbsorbLeftByPlain = Struck->TemporaryAbsorbHeld();
	Struck->GrantTemporaryAbsorb(40.0f);
	if (!TestEqual(TEXT("set-up: the attacker holds a temporary absorb of 40 again"),
			Struck->TemporaryAbsorbHeld(), 40.0f, 0.001f))
	{
		return false;
	}
	const float HolderPastAbsorb = HealthTakenByOnePayment(Three.Holder, StruckActor);
	const float AbsorbLeftByHolder = Struck->TemporaryAbsorbHeld();

	TestTrue(*FString::Printf(TEXT("control: the plain payment spends some of the absorb, and the absorb and "
								   "health together take less than 100: %.2f spent, %.2f to health"),
				 40.0f - AbsorbLeftByPlain, PlainPastAbsorb),
		AbsorbLeftByPlain < 40.0f && (40.0f - AbsorbLeftByPlain) + PlainPastAbsorb < Sent - 1.0f);
	TestEqual(TEXT("the holder's payment empties the temporary absorb of its 40"),
		AbsorbLeftByHolder, 0.0f, 0.001f);
	TestEqual(TEXT("and the 60 left reaches health"), HolderPastAbsorb, Sent - 40.0f, 0.01f);

	// THE ENERGY SHIELD. 500 held, of a maximum of 1,000. Each payment is taken
	// whole by the shield; the plain one is smaller by what the armour removed.
	Struck->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 1000.0f);
	Struck->SetNumericAttributeBase(Shield, 500.0f);
	if (!TestEqual(TEXT("set-up: the attacker holds an energy shield of 500, and no temporary absorb"),
			Struck->GetNumericAttribute(Shield) + Struck->TemporaryAbsorbHeld(), 500.0f, 0.001f))
	{
		return false;
	}
	float ShieldBefore = Struck->GetNumericAttribute(Shield);
	const float PlainPastShield = HealthTakenByOnePayment(Three.Plain, StruckActor);
	const float ShieldTookOfPlain = ShieldBefore - Struck->GetNumericAttribute(Shield);
	ShieldBefore = Struck->GetNumericAttribute(Shield);
	const float HolderPastShield = HealthTakenByOnePayment(Three.Holder, StruckActor);
	const float ShieldTookOfHolder = ShieldBefore - Struck->GetNumericAttribute(Shield);

	TestTrue(*FString::Printf(TEXT("control: the shield takes the plain payment after the armour: %.2f"),
				 ShieldTookOfPlain),
		ShieldTookOfPlain > 0.0f && ShieldTookOfPlain < Sent - 1.0f);
	TestEqual(TEXT("control: and none of it reaches health"), PlainPastShield, 0.0f, 0.001f);
	TestEqual(TEXT("the shield takes the whole 100 of the holder's payment"), ShieldTookOfHolder, Sent, 0.01f);
	TestEqual(TEXT("and none of it reaches health"), HolderPastShield, 0.0f, 0.001f);

	// MANA. The one damage a mana pool takes is a tick of damage over time on a
	// character holding `damage_over_time_taken_from_mana_first`, and retaliation
	// is never a tick. So mana takes none of a payment, with the flag or without.
	// The shield is emptied and the armour taken off first, so nothing stands
	// between the blow and the mana step.
	Struck->SetNumericAttributeBase(Shield, 0.0f);
	Three.Attacker.Combat->SetArmor(0.0f);
	Struck->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
	Struck->SetNumericAttributeBase(Mana, 1000.0f);
	UnmitigatedHoldFlats(Three.Attacker,
		{{FName(UCataclysmDamageCalculation::DamageOverTimeTakenFromManaFirstStat), 1.0f}});

	// THE CONTROL: THE STAT IS READ. A tick of 100 takes 100 mana and no health,
	// and so does a tick that carries the flag.
	FCataclysmHitDelivery PlainTick;
	PlainTick.bIsDamageOverTime = true;
	FCataclysmHitDelivery FlaggedTick = PlainTick;
	FlaggedTick.bCannotBeMitigated = true;

	float ManaBefore = Struck->GetNumericAttribute(Mana);
	float HealthBefore = UnmitigatedHealthOf(StruckActor);
	UCataclysmSkillEffects::ApplyDirectDamage(Three.Holder.Actor.Get(), StruckActor, 100.0f, PlainTick);
	if (!TestEqual(TEXT("control: a tick of 100 takes 100 of the attacker's mana"),
			ManaBefore - Struck->GetNumericAttribute(Mana), 100.0f, 0.01f))
	{
		return false;
	}
	TestEqual(TEXT("control: and no health"), HealthBefore - UnmitigatedHealthOf(StruckActor), 0.0f, 0.001f);

	ManaBefore = Struck->GetNumericAttribute(Mana);
	HealthBefore = UnmitigatedHealthOf(StruckActor);
	UCataclysmSkillEffects::ApplyDirectDamage(Three.Holder.Actor.Get(), StruckActor, 100.0f, FlaggedTick);
	TestEqual(TEXT("a tick carrying the flag still takes 100 of the mana"),
		ManaBefore - Struck->GetNumericAttribute(Mana), 100.0f, 0.01f);
	TestEqual(TEXT("and no health"), HealthBefore - UnmitigatedHealthOf(StruckActor), 0.0f, 0.001f);

	ManaBefore = Struck->GetNumericAttribute(Mana);
	const float PlainPastMana = HealthTakenByOnePayment(Three.Plain, StruckActor);
	TestEqual(TEXT("control: a plain payment takes no mana, because it is not a tick"),
		ManaBefore - Struck->GetNumericAttribute(Mana), 0.0f, 0.001f);
	TestEqual(TEXT("control: and the whole 100 reaches health"), PlainPastMana, Sent, 0.01f);
	const float HolderPastMana = HealthTakenByOnePayment(Three.Holder, StruckActor);
	TestEqual(TEXT("the holder's payment takes no mana either"),
		ManaBefore - Struck->GetNumericAttribute(Mana), 0.0f, 0.001f);
	TestEqual(TEXT("and the whole 100 reaches health"), HolderPastMana, Sent, 0.01f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationUnmitigatedImmunitiesTest,
	"Cataclysm.RetaliationUnmitigated.AHoldersPaymentDoesNothingToWhatTakesNoDamageOrStandsInAWindowThatRefusesIt")
{
	using namespace CataclysmRetaliationUnmitigatedTest;

	const FUnmitigatedPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FUnmitigatedPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FUnmitigatedThree Three(World);

	// A CREATURE THAT TAKES NO DAMAGE, and one beside it that does. Each 3 m or
	// more from everything else.
	ACataclysmEnemyCharacter* Hurt = UnmitigatedCreatureAt(World, FVector(600.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Unhurt = UnmitigatedCreatureAt(World, FVector(600.0f, 300.0f, 0.0f));
	if (!TestNotNull(TEXT("a creature that can be hurt"), Hurt)
		|| !TestNotNull(TEXT("a creature that cannot"), Unhurt))
	{
		return false;
	}
	Unhurt->bCannotBeHurt = true;

	const float PlainOnHurt = HealthTakenByOnePayment(Three.Plain, Hurt);
	const float HolderOnHurt = HealthTakenByOnePayment(Three.Holder, Hurt);
	if (!TestEqual(TEXT("control: a creature that can be hurt takes the plain payment whole"),
			PlainOnHurt, Sent, 0.5f)
		|| !TestEqual(TEXT("control: and the holder's"), HolderOnHurt, Sent, 0.5f))
	{
		return false;
	}
	TestEqual(TEXT("control: a creature that takes no damage takes nothing from the plain payment"),
		HealthTakenByOnePayment(Three.Plain, Unhurt), 0.0f, 0.001f);
	TestEqual(TEXT("and nothing from the holder's"),
		HealthTakenByOnePayment(Three.Holder, Unhurt), 0.0f, 0.001f);

	// THE NO-DAMAGE WINDOW. Read before it opens and after, at one time of the
	// world's clock.
	AActor* StruckActor = Three.Attacker.Actor.Get();
	if (!TestEqual(TEXT("control: before the window opens the holder's payment takes the whole 100"),
			HealthTakenByOnePayment(Three.Holder, StruckActor), Sent, 0.01f))
	{
		return false;
	}
	Three.Attacker.AbilitySystem->GrantDamageImmunity(5.0f);
	if (!TestTrue(TEXT("set-up: the attacker's no-damage window is open"),
			Three.Attacker.AbilitySystem->IsDamageImmune()))
	{
		return false;
	}
	TestEqual(TEXT("control: inside the window the plain payment takes nothing"),
		HealthTakenByOnePayment(Three.Plain, StruckActor), 0.0f, 0.001f);
	TestEqual(TEXT("and the holder's takes nothing"),
		HealthTakenByOnePayment(Three.Holder, StruckActor), 0.0f, 0.001f);

	// THE WINDOW AGAINST ONE DAMAGE TYPE, on a fourth character with no other
	// window open. A retaliation payment from a character that is not a creature
	// carries no damage type, so this window never refuses one, with the flag or
	// without: both payments take the whole 100 inside it. What the flag must not
	// do is carry a TYPED blow past the window, and that is read with a direct
	// blow of the type that carries the flag.
	FRetaliator Typed(World, FVector(300.0f, 300.0f, 0.0f));
	const FName War(TEXT("War"));
	FCataclysmHitDelivery FlaggedWar;
	FlaggedWar.DamageType = War;
	FlaggedWar.bCannotBeMitigated = true;

	float Before = Typed.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyDirectDamage(Three.Holder.Actor.Get(), Typed.Actor.Get(), 100.0f, FlaggedWar);
	if (!TestEqual(TEXT("control: before the window opens a flagged War blow of 100 takes 100"),
			Before - Typed.Vitals->GetHealth(), 100.0f, 0.01f))
	{
		return false;
	}
	Typed.AbilitySystem->GrantDamageTypeImmunity(War, 5.0f);
	if (!TestTrue(TEXT("set-up: the window against War is open"),
			Typed.AbilitySystem->IsImmuneToDamageType(War)))
	{
		return false;
	}
	Before = Typed.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyDirectDamage(Three.Holder.Actor.Get(), Typed.Actor.Get(), 100.0f, FlaggedWar);
	TestEqual(TEXT("inside the window the flagged War blow takes nothing"),
		Before - Typed.Vitals->GetHealth(), 0.0f, 0.001f);
	TestEqual(TEXT("a plain retaliation payment has no type, and takes the whole 100 inside the War window"),
		HealthTakenByOnePayment(Three.Plain, Typed.Actor.Get()), Sent, 0.01f);
	TestEqual(TEXT("and so does the holder's"),
		HealthTakenByOnePayment(Three.Holder, Typed.Actor.Get()), Sent, 0.01f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationUnmitigatedReflectedTest,
	"Cataclysm.RetaliationUnmitigated.AReflectedMeleeHitFromAHolderIsPaidBackWholePastTheAttackersArmour")
{
	using namespace CataclysmRetaliationUnmitigatedTest;

	const FUnmitigatedPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FUnmitigatedPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// BOTH RETALIATORS REFLECT EVERY MELEE HIT. A chance of 100 is not rolled, so
	// no roll is pinned for it. The stat line is written whole, so the holder's
	// line names the flag again beside the chance.
	FUnmitigatedThree Three(World);
	const FName ReflectChance(UCataclysmDamageCalculation::MeleeReflectChanceStat);
	UnmitigatedHoldFlats(Three.Plain, {{ReflectChance, 100.0f}});
	UnmitigatedHoldFlats(Three.Holder, {{ReflectChance, 100.0f}, {UnmitigatedFlag(), 1.0f}});

	Three.Attacker.Combat->SetAttackDamage(BlowTaken);
	Three.Attacker.Combat->SetArmor(5000.0f);

	FCataclysmHitDelivery Melee;
	Melee.bIsMelee = true;

	const auto StrikeInMelee = [&](const FRetaliator& Whom, float& OutWhomLost) -> float
	{
		const float AttackerBefore = Three.Attacker.Vitals->GetHealth();
		const float WhomBefore = Whom.Vitals->GetHealth();
		UCataclysmSkillEffects::ApplyHit(
			Three.Attacker.Actor.Get(), Whom.Actor.Get(), /*DamagePercent=*/100.0f,
			FGameplayTagContainer(), Melee);
		OutWhomLost = WhomBefore - Whom.Vitals->GetHealth();
		return AttackerBefore - Three.Attacker.Vitals->GetHealth();
	};

	float PlainLost = -1.0f;
	const float BackFromPlain = StrikeInMelee(Three.Plain, PlainLost);
	if (!TestEqual(TEXT("set-up: the plain retaliator reflected the melee hit and took none of it"),
			PlainLost, 0.0f, 0.001f))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("control: the armoured attacker takes less than the 500 reflected at it, and "
								   "more than nothing: %.2f"), BackFromPlain),
		BackFromPlain > 0.0f && BackFromPlain < BlowTaken - 1.0f);

	float HolderLost = -1.0f;
	const float BackFromHolder = StrikeInMelee(Three.Holder, HolderLost);
	TestEqual(TEXT("the holder reflected the melee hit and took none of it"), HolderLost, 0.0f, 0.001f);
	TestEqual(TEXT("and the attacker takes the whole 500, past its armour"), BackFromHolder, BlowTaken, 0.05f);

	return true;
}

CATACLYSM_RETALIATION_TEST(FCataclysmRetaliationUnmitigatedStillNotAHitTest,
	"Cataclysm.RetaliationUnmitigated.AHoldersPaymentStillCannotCriticallyStrikeOrLeech")
{
	using namespace CataclysmRetaliationUnmitigatedTest;

	// THE CRITICAL STRIKE ROLL IS PINNED AT 0, so every blow that may critically
	// strike does. The holder holds a chance of 100, a multiplier of 300, an
	// attack of 100 and a life leech of 10.
	const FUnmitigatedPinned AlwaysCritical(TEXT("Cataclysm.CritRoll"), 0.0f);
	const FUnmitigatedPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to fight in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FUnmitigatedThree Three(World);
	UCataclysmAbilitySystemComponent* Struck = Three.Attacker.AbilitySystem.Get();
	AActor* StruckActor = Three.Attacker.Actor.Get();
	Three.Holder.Combat->SetCritChance(100.0f);
	Three.Holder.Combat->SetCritMultiplier(300.0f);
	Three.Holder.Combat->SetAttackDamage(100.0f);
	Three.Holder.Vitals->SetLifeLeech(10.0f);

	// THE CONTROL, READ FIRST: an ordinary hit of the holder's critically strikes
	// for three times its 100, and the holder is promised leech for it. So the
	// pinned roll, the chance, the multiplier and the leech are all in force.
	const float Before = Three.Attacker.Vitals->GetHealth();
	UCataclysmSkillEffects::ApplyHit(
		Three.Holder.Actor.Get(), StruckActor, /*DamagePercent=*/100.0f,
		FGameplayTagContainer(), FCataclysmHitDelivery());
	const bool bOrdinaryWasCritical = Struck->GetLastResolvedHit().bWasCritical;
	if (!TestTrue(TEXT("control: the holder's ordinary hit critically strikes"), bOrdinaryWasCritical)
		|| !TestEqual(TEXT("control: for three times its 100"),
			Before - Three.Attacker.Vitals->GetHealth(), 300.0f, 0.05f)
		|| !TestEqual(TEXT("control: and the holder is promised one leech payment for it"),
			Three.Holder.AbilitySystem->GetLeechPayments().Num(), 1))
	{
		return false;
	}
	Three.Holder.AbilitySystem->SetLeechPayments(TArray<FCataclysmLeechPayment>());

	// AND THE HOLDER'S RETALIATION DOES NEITHER.
	const float Taken = HealthTakenByOnePayment(Three.Holder, StruckActor);
	TestFalse(TEXT("the holder's payment does not critically strike"),
		Struck->GetLastResolvedHit().bWasCritical);
	TestEqual(TEXT("it takes the 100 sent and not three times that"), Taken, Sent, 0.01f);
	TestEqual(TEXT("and the holder is promised no leech for it"),
		Three.Holder.AbilitySystem->GetLeechPayments().Num(), 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
