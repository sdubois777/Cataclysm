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
// For the collision an area search needs to find anything at all. Issue #1047.
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
// For the leech a retaliating character may take from what it sent. Issue #1048.
#include "AbilitySystem/CataclysmLeech.h"
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
	Defender.Combat->SetRetaliation(158.0f);

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
		RetaliationHealthLostSince(Attacker, AttackerBefore), 158.0f, 0.01f);
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
		RetaliationHealthLostSince(Near, NearBefore), 158.0f, 0.01f);

	// AND THE ATTACKER TAKES ONE LOT AND NOT TWO. It is standing one metre away,
	// so the search returns it as well as it being the thing that hit; without
	// the check that drops it from the search's results it would pay twice.
	TestEqual(TEXT("and the attacker still takes exactly one payment"),
		RetaliationHealthLostSince(Attacker, AttackerBefore), 158.0f, 0.01f);

	// AND SIX METRES IS OUTSIDE FOUR.
	TestEqual(TEXT("and one six metres away still takes nothing"),
		RetaliationHealthLostSince(Far, FarBefore), 0.0f, 0.001f);

	// AND THE WAVE IS NOT ITSELF A HIT, so a bystander that also retaliates
	// sends nothing back. Without that, two retaliating characters standing near
	// one another would reflect without end.
	Near.Combat->SetRetaliation(158.0f);
	const float DefenderBefore = Defender.Vitals->GetHealth();
	NearBefore = Near.Vitals->GetHealth();

	if (!TestTrue(TEXT("the third blow landed"), StrikeTheDefender() > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("a bystander the wave struck takes it"),
		RetaliationHealthLostSince(Near, NearBefore), 158.0f, 0.01f);

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
	Defender.Combat->SetRetaliation(158.0f);
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
		RetaliationHealthLostSince(Attacker, AttackerBefore), 158.0f, 0.01f);
	TestEqual(TEXT("and the enemy standing beside the defender takes it too"),
		RetaliationHealthLostSince(Near, NearBefore), 158.0f, 0.01f);

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
	Defender.Combat->SetRetaliation(158.0f);
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
	// retaliation damage as well as to your attacks." Ten per cent of the 158
	// the attacker took is 15.8.
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
		Promised[0].Remaining, 15.8f, 0.01f);

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
	Defender.Combat->SetRetaliation(158.0f);
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

	TestEqual(TEXT("the enemy with 25 health left loses 25 and not 158"),
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
		Promised[0].Remaining, 18.3f, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
