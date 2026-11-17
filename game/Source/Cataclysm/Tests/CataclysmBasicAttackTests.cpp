// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmBasicAttack.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
// For the stack count the swing rate can now grow with. Issue #1002.
#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"

/**
 * The weapon's attack speed, and the basic attack that fires at it.
 *
 * TWO ISSUES, AND THE FIRST BLOCKED THE SECOND. Issue #647: every weapon in
 * `game/Data/ItemBases.csv` states an attack speed, the row struct carries the
 * column, and nothing ever wrote it onto a character -- the attribute sat at
 * zero for every character in the game, so every increased attack speed affix
 * multiplied zero. Issue #36: the basic attack is granted and nothing ever
 * activated it, and a rate of zero is why nobody could build the automatic
 * firing that the design requires.
 *
 * THE DESIGN LEAVES ALMOST NOTHING TO CHOOSE HERE, so most of what these guard
 * is a quotation rather than a decision:
 *
 *   "The basic attack is on no key. It fires automatically."
 *   "The Basic Attack is automatic, so the weapon's attack speed sets its rate."
 *   "It is income for being in a fight rather than a filler action."
 *   "The automatic basic attack returns 6 mana each time it lands."
 *
 * WHAT IS NOT COVERED: the timer itself. A world built by UWorld::CreateWorld is
 * never ticked, so no test here waits for a swing to happen on its own. What is
 * tested is every decision the timer makes when it fires -- the interval, whether
 * a character may swing, how far it reaches, and whether anything is in range.
 * Actors in this world DO receive BeginPlay as they spawn, since issue #654.
 * That is why the basic attack timer exists on a spawned player here at all.
 */

namespace CataclysmBasicAttackTest
{
	/**
	 * NAMED `MakeWorld`, AND IT IS THE BEGUN-PLAY ONE. This file carried the
	 * same body as the twenty files whose copy was called
	 * MakeWorldThatHasBegunPlay, under the shorter name -- the naming confusion
	 * of issue #654 pointing the other way. The name is left alone so this stays
	 * a harness fix rather than a rename across one file's call sites.
	 */
	static UWorld* MakeWorld()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/** A bare actor carrying an ability system with the combat attributes. */
	struct FArmedActor
	{
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
	};

	static FArmedActor MakeArmedActor(UWorld* World)
	{
		FArmedActor Made;
		Made.Actor = World->SpawnActor<AActor>();
		if (!Made.Actor)
		{
			return Made;
		}

		Made.AbilitySystem =
			NewObject<UCataclysmAbilitySystemComponent>(Made.Actor);
		Made.AbilitySystem->RegisterComponent();

		UCataclysmCombatAttributeSet* Combat =
			NewObject<UCataclysmCombatAttributeSet>(Made.Actor);
		Made.AbilitySystem->AddAttributeSetSubobject(Combat);

		UCataclysmVitalAttributeSet* Vitals =
			NewObject<UCataclysmVitalAttributeSet>(Made.Actor);
		Made.AbilitySystem->AddAttributeSetSubobject(Vitals);

		Made.AbilitySystem->InitAbilityActorInfo(Made.Actor, Made.Actor);

		Made.Slots = NewObject<UCataclysmWeaponSlotsComponent>(Made.Actor);
		Made.Slots->RegisterComponent();
		Made.Slots->SetDamageType(TEXT("Demonic"));

		return Made;
	}

	static ACataclysmEnemyCharacter* SpawnEnemyAt(UWorld* World,
												  const FVector& Where)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where,
														FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(549.0f);
		}
		return Spawned;
	}

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
			Pawn->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		}
		return Pawn;
	}
}

// --------------------------------------------------------------------------
// The weapon's attack speed reaches the character. Issue #647
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponStatesAnAttackSpeed,
	"Cataclysm.BasicAttack.AWeaponTypeStatesHowFastItSwings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponStatesAnAttackSpeed::RunTest(const FString&)
{
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("the item base table"), Bases))
	{
		return false;
	}

	// The figures are the Item Bases sheet's own, read back through the lookup.
	TestTrue(TEXT("a Greataxe swings 1.28 times a second"),
		FMath::IsNearlyEqual(
			UCataclysmItemModifiers::WeaponAttackSpeedForType(
				Bases, TEXT("Greataxe")), 1.28f, 0.005f));
	TestTrue(TEXT("a Dagger swings faster, at 1.5"),
		FMath::IsNearlyEqual(
			UCataclysmItemModifiers::WeaponAttackSpeedForType(
				Bases, TEXT("Dagger")), 1.5f, 0.005f));

	// NO GEAR LEVEL AND NO TWO-HANDED DOUBLING, unlike the damage. A Greataxe is
	// two-handed, so if the doubling had been copied across it would read 2.56.
	TestFalse(TEXT("a two-hander does not double its rate"),
		FMath::IsNearlyEqual(
			UCataclysmItemModifiers::WeaponAttackSpeedForType(
				Bases, TEXT("Greataxe")), 2.56f, 0.005f));

	TestEqual(TEXT("holding nothing states no rate"),
		UCataclysmItemModifiers::WeaponAttackSpeedForType(Bases, FString()),
		0.0f);
	TestEqual(TEXT("and neither does something that is not a weapon type"),
		UCataclysmItemModifiers::WeaponAttackSpeedForType(
			Bases, TEXT("NotAWeapon")), 0.0f);

	return true;
}

// --------------------------------------------------------------------------
// The rate the basic attack fires at
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackRateComesFromTheWeapon,
	"Cataclysm.BasicAttack.TheGapBetweenSwingsIsOneOverTheAttackSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackRateComesFromTheWeapon::RunTest(const FString&)
{
	// "The Basic Attack is automatic, so the weapon's attack speed sets its
	// rate." A Greataxe's 1.28 swings a second is a swing every 0.781 seconds.
	TestTrue(FString::Printf(TEXT("1.28 a second is a swing every 0.781s, got %.4f"),
		UCataclysmBasicAttack::SecondsBetweenSwings(1.28f)),
		FMath::IsNearlyEqual(
			UCataclysmBasicAttack::SecondsBetweenSwings(1.28f), 0.78125f,
			0.0005f));

	TestTrue(TEXT("a faster weapon swings more often"),
		UCataclysmBasicAttack::SecondsBetweenSwings(1.5f)
			< UCataclysmBasicAttack::SecondsBetweenSwings(1.28f));

	// ZERO MEANS NEVER, NOT INSTANTLY. A character holding nothing has an attack
	// speed of zero, and reading that as an interval of zero would ask the timer
	// to fire as fast as it possibly could, forever.
	TestEqual(TEXT("no attack speed means no swinging at all"),
		UCataclysmBasicAttack::SecondsBetweenSwings(0.0f), 0.0f);
	TestEqual(TEXT("and a negative rate is not a rate either"),
		UCataclysmBasicAttack::SecondsBetweenSwings(-3.0f), 0.0f);

	// A floor rather than a cap on the attribute, so a data error putting a huge
	// number in the AttackSpeed column produces a fast character and not a
	// frozen one.
	TestEqual(TEXT("an absurd rate is floored rather than running away"),
		UCataclysmBasicAttack::SecondsBetweenSwings(100000.0f),
		UCataclysmBasicAttack::FastestSwingSeconds);

	return true;
}

// --------------------------------------------------------------------------
// Whether a character may swing, and whether there is anything to swing at
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackStopsWhenItMust,
	"Cataclysm.BasicAttack.ADeadOrStunnedCharacterDoesNotSwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackStopsWhenItMust::RunTest(const FString&)
{
	UWorld* World = CataclysmBasicAttackTest::MakeWorld();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmBasicAttackTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	TestTrue(TEXT("an ordinary living character may swing"),
		UCataclysmBasicAttack::MaySwing(Player));

	// THE SAME TEST THE PLAYER CONTROLLER USES TO REFUSE A SKILL AND A STEP. The
	// design defines a stun as being unable to act at all, so an automatic
	// attack running through one would leave a stunned character with exactly
	// one thing it could still do.
	UCataclysmSkillEffects::MarkDead(Player);
	TestFalse(TEXT("a dead character does not swing"),
		UCataclysmBasicAttack::MaySwing(Player));

	UCataclysmSkillEffects::ClearDead(Player);
	TestTrue(TEXT("and swings again once it stands back up"),
		UCataclysmBasicAttack::MaySwing(Player));

	TestFalse(TEXT("nothing at all does not swing"),
		UCataclysmBasicAttack::MaySwing(nullptr));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackNeedsSomethingToHit,
	"Cataclysm.BasicAttack.NothingIsSwungAtEmptyAir",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackNeedsSomethingToHit::RunTest(const FString&)
{
	// "It is income for being in a fight rather than a filler action." A
	// character swinging at nothing between fights is the filler action that
	// sentence rules out, and the mana the design pays on hit would not be
	// earned by it either.
	UWorld* World = CataclysmBasicAttackTest::MakeWorld();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmBasicAttackTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	// A Greataxe's basic attack has a radius of 2.4 metres.
	constexpr float GreataxeReachCm = 240.0f;

	TestFalse(TEXT("with nothing nearby, it does not swing"),
		UCataclysmBasicAttack::SomethingInReach(Player, GreataxeReachCm));

	ACataclysmEnemyCharacter* Close = CataclysmBasicAttackTest::SpawnEnemyAt(
		World, FVector(100.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an enemy one metre away"), Close))
	{
		return false;
	}

	TestTrue(TEXT("with an enemy inside the reach, it swings"),
		UCataclysmBasicAttack::SomethingInReach(Player, GreataxeReachCm));

	// AND THE REACH IS A REAL BOUNDARY rather than a number nothing reads: the
	// same enemy is out of reach for a shorter weapon.
	TestFalse(TEXT("that enemy is out of reach of a half-metre weapon"),
		UCataclysmBasicAttack::SomethingInReach(Player, 50.0f));

	// A character with no basic attack at all -- the Shield's case -- reaches
	// nothing, whatever is standing next to it.
	TestFalse(TEXT("no reach means nothing is ever in reach"),
		UCataclysmBasicAttack::SomethingInReach(Player, 0.0f));

	return true;
}

// --------------------------------------------------------------------------
// The mana a landed basic attack returns
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackReturnsMana,
	"Cataclysm.BasicAttack.OnlyTheBasicAttackReturnsManaOnHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackReturnsMana::RunTest(const FString&)
{
	// "The automatic basic attack returns 6 mana each time it lands." That
	// figure sat in game/Data/SkillSlots.csv and was loaded into a field no
	// running code read, which is half of why mana only ever went down --
	// issue #653.
	const UDataTable* Slots = UCataclysmSkillSlots::LoadGeneratedTable();
	if (!TestNotNull(TEXT("the skill slot table"), Slots))
	{
		return false;
	}

	const FCataclysmSkillSlotNumbers Basic = UCataclysmSkillSlots::NumbersFor(
		Slots, ECataclysmAbilitySlot::BasicAttack);
	const FCataclysmSkillSlotNumbers Heavy = UCataclysmSkillSlots::NumbersFor(
		Slots, ECataclysmAbilitySlot::Heavy);

	if (!TestTrue(TEXT("the basic attack has a row"), Basic.bFound)
		|| !TestTrue(TEXT("the heavy slot has a row"), Heavy.bFound))
	{
		return false;
	}

	TestTrue(TEXT("the basic attack returns 6 mana at level 100"),
		FMath::IsNearlyEqual(Basic.ManaOnHitAtLevel100, 6.0f, 0.005f));
	TestEqual(TEXT("and no other slot returns any"),
		Heavy.ManaOnHitAtLevel100, 0.0f);

	// AT LEVEL 100 THE SCALED FIGURE IS THE STATED ONE, which is what makes the
	// design's arithmetic checkable: 6 a swing at 1.3 swings a second is about 8
	// mana a second, against a Heavy attack costing 10 a second.
	TestTrue(TEXT("at level 100 it is the stated 6"),
		FMath::IsNearlyEqual(
			UCataclysmSkillSlots::ManaOnHitAtLevel(
				Basic.ManaOnHitAtLevel100,
				UCataclysmSkillSlots::ManaCostReferenceLevel), 6.0f, 0.005f));

	// IT RIDES THE MANA POOL THE SAME WAY A COST DOES, so the relationship the
	// design states holds at every level rather than at exactly one.
	const float OnHitAtOne = UCataclysmSkillSlots::ManaOnHitAtLevel(
		Basic.ManaOnHitAtLevel100, 1);
	const float CostAtOne = UCataclysmSkillSlots::ManaCostAtLevel(
		Heavy.ManaCostAtLevel100, 1);
	const float OnHitAtHundred = UCataclysmSkillSlots::ManaOnHitAtLevel(
		Basic.ManaOnHitAtLevel100, 100);
	const float CostAtHundred = UCataclysmSkillSlots::ManaCostAtLevel(
		Heavy.ManaCostAtLevel100, 100);

	TestTrue(TEXT("a level 1 character earns less per swing than a level 100 one"),
		OnHitAtOne < OnHitAtHundred);
	TestTrue(TEXT("but the return buys the same share of a Heavy attack at both"),
		FMath::IsNearlyEqual(OnHitAtOne / CostAtOne,
							 OnHitAtHundred / CostAtHundred, 0.001f));

	TestEqual(TEXT("a slot with no mana on hit returns none at any level"),
		UCataclysmSkillSlots::ManaOnHitAtLevel(0.0f, 50), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackManaActuallyArrives,
	"Cataclysm.BasicAttack.ALandedBasicAttackPutsTheManaOnTheCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackManaActuallyArrives::RunTest(const FString&)
{
	// THE FIGURE BEING RIGHT IS NOT THE SAME AS THE MANA ARRIVING, and the
	// difference between those two is the whole of issue #653: ManaOnHit was
	// loaded out of the table into a field correctly, and no running code ever
	// read it. So this activates a real basic attack against a real enemy and
	// watches the caster's mana, rather than checking arithmetic.
	UWorld* World = CataclysmBasicAttackTest::MakeWorld();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	CataclysmBasicAttackTest::FArmedActor Armed =
		CataclysmBasicAttackTest::MakeArmedActor(World);
	if (!TestNotNull(TEXT("an actor with an ability system"), Armed.Actor))
	{
		return false;
	}
	Armed.Actor->SetActorLocation(FVector::ZeroVector);

	// A side, so the enemy below counts as an enemy, and a weapon's worth of
	// damage, so the swing lands for something rather than for nothing.
	if (IGenericTeamAgentInterface* Sided =
			Cast<IGenericTeamAgentInterface>(Armed.Actor))
	{
		Sided->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	}
	Armed.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	// GRANTED AT LEVEL 100 so the return is the 6 the sheet states, rather than
	// the level 1 share of it. That is the same thing CataclysmSkillTemplateTests
	// does, and for the same reason.
	const FGameplayAbilitySpecHandle Handle =
		Armed.AbilitySystem->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(),
			ECataclysmAbilitySlot::BasicAttack, /*Level=*/100, Armed.Actor);
	FGameplayAbilitySpec* Spec =
		Armed.AbilitySystem->FindAbilitySpecFromHandle(Handle);
	UCataclysmStrikeSkill* Swing =
		Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("a granted basic attack"), Swing))
	{
		return false;
	}

	// The Greataxe's own basic attack shape, from the Item Bases sheet.
	Swing->SkillName = TEXT("Basic Attack");
	Swing->Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Radius=2.4; Angle=120; MaxTargets=1"));

	// AND THE REACH IS READ BACK THROUGH THE REAL LOOKUP, so this also proves
	// UCataclysmBasicAttack finds the granted ability by its slot tag rather
	// than by guessing.
	TestTrue(FString::Printf(
		TEXT("the basic attack reaches 240cm, got %.1f"),
		UCataclysmBasicAttack::ReachCmOf(Armed.AbilitySystem)),
		FMath::IsNearlyEqual(
			UCataclysmBasicAttack::ReachCmOf(Armed.AbilitySystem), 240.0f,
			0.5f));

	ACataclysmEnemyCharacter* Target = CataclysmBasicAttackTest::SpawnEnemyAt(
		World, FVector(100.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an enemy one metre away"), Target))
	{
		return false;
	}

	const FGameplayAttribute Mana =
		UCataclysmVitalAttributeSet::GetManaAttribute();
	Armed.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 644.0f);
	Armed.AbilitySystem->SetNumericAttributeBase(Mana, 0.0f);

	const bool bStarted = Armed.AbilitySystem->TryActivateAbility(
		Handle, /*bAllowRemoteActivation=*/false);
	if (!TestTrue(TEXT("the basic attack activated"), bStarted))
	{
		return false;
	}

	const float After = Armed.AbilitySystem->GetNumericAttribute(Mana);
	TestTrue(FString::Printf(
		TEXT("a landed basic attack returns 6 mana, got %.2f"), After),
		FMath::IsNearlyEqual(After, 6.0f, 0.05f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackPaysNothingForAMiss,
	"Cataclysm.BasicAttack.ASwingThatHitsNothingReturnsNoMana",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackPaysNothingForAMiss::RunTest(const FString&)
{
	// "returns 6 mana EACH TIME IT LANDS". The other half of the test above.
	//
	// TWO CASES, AND THE SECOND IS THE ONE THAT MATTERS. Swinging at empty air
	// is the obvious case and it is nearly free: UCataclysmSkillTemplate::
	// HitTargets returns before it decides anything when the target list is
	// empty, so that case never reaches the "did this land" test at all. This
	// was written with only that case first, and breaking the rule on purpose
	// showed the test could not fail -- it passed because HitTargets returned
	// early, not because of the rule it claimed to guard.
	//
	// The case that reaches the rule is a swing that FINDS a target and deals it
	// nothing, which is what an evaded or wholly mitigated hit is. It is
	// produced below by holding a weapon worth no damage.
	UWorld* World = CataclysmBasicAttackTest::MakeWorld();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	CataclysmBasicAttackTest::FArmedActor Armed =
		CataclysmBasicAttackTest::MakeArmedActor(World);
	if (!TestNotNull(TEXT("an actor with an ability system"), Armed.Actor))
	{
		return false;
	}
	Armed.Actor->SetActorLocation(FVector::ZeroVector);
	if (IGenericTeamAgentInterface* Sided =
			Cast<IGenericTeamAgentInterface>(Armed.Actor))
	{
		Sided->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	}
	Armed.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);

	const FGameplayAbilitySpecHandle Handle =
		Armed.AbilitySystem->GiveAbilityInSlot(
			UCataclysmStrikeSkill::StaticClass(),
			ECataclysmAbilitySlot::BasicAttack, /*Level=*/100, Armed.Actor);
	FGameplayAbilitySpec* Spec =
		Armed.AbilitySystem->FindAbilitySpecFromHandle(Handle);
	UCataclysmStrikeSkill* Swing =
		Spec ? Cast<UCataclysmStrikeSkill>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("a granted basic attack"), Swing))
	{
		return false;
	}
	Swing->SkillName = TEXT("Basic Attack");
	Swing->Params = UCataclysmSkillShapes::ParseParams(
		TEXT("Radius=2.4; Angle=120; MaxTargets=1"));

	const FGameplayAttribute Mana =
		UCataclysmVitalAttributeSet::GetManaAttribute();
	Armed.AbilitySystem->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 644.0f);
	Armed.AbilitySystem->SetNumericAttributeBase(Mana, 0.0f);

	// CASE ONE: no enemy anywhere. The swing happens and finds nothing.
	Armed.AbilitySystem->TryActivateAbility(Handle,
											/*bAllowRemoteActivation=*/false);

	TestEqual(TEXT("a swing that found nothing returns nothing"),
		Armed.AbilitySystem->GetNumericAttribute(Mana), 0.0f);

	// CASE TWO: an enemy well inside the reach, and a swing worth no damage.
	// This is the case that actually reaches the rule, because the target list
	// is not empty. A hit that deals nothing is what an evaded or wholly
	// mitigated blow is, and the design pays on a hit LANDING rather than on a
	// swing happening.
	ACataclysmEnemyCharacter* Target = CataclysmBasicAttackTest::SpawnEnemyAt(
		World, FVector(100.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an enemy one metre away"), Target))
	{
		return false;
	}

	Armed.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 0.0f);
	Armed.AbilitySystem->SetNumericAttributeBase(Mana, 0.0f);

	Armed.AbilitySystem->TryActivateAbility(Handle,
											/*bAllowRemoteActivation=*/false);

	TestEqual(TEXT("a swing that reached an enemy and dealt it nothing "
				   "returns nothing"),
		Armed.AbilitySystem->GetNumericAttribute(Mana), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSwingRateIsAskedForTest,
	"Cataclysm.BasicAttack.TheSwingRateIsAskedForRatherThanReadOffTheAttribute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSwingRateIsAskedForTest::RunTest(const FString&)
{
	using namespace CataclysmBasicAttackTest;

	// WHAT THIS IS FOR. Issue #1002. The swing rate used to be read straight off
	// the attack speed attribute, and an attribute holds what a stat is worth
	// with no state taken into account -- a bonus that depends on the
	// character's state is never folded into one, because it would be stale the
	// moment the state moved. So the Masochist's Sanguine Momentum node, whose
	// attack speed grows with a stack count that expires, was invisible to the
	// basic attack, and nothing at run time would have said so.
	UWorld* World = MakeWorld();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FArmedActor Armed = MakeArmedActor(World);
	if (!TestNotNull(TEXT("an armed actor"), Armed.Actor))
	{
		return false;
	}

	const FGameplayAttribute Speed =
		UCataclysmCombatAttributeSet::GetAttackSpeedAttribute();

	// A PLAIN CHARACTER ANSWERS EXACTLY WHAT THE ATTRIBUTE SAYS, which is every
	// character in the game with no such node. Without this, the change could
	// have altered every swing rate in the game and the checks below would still
	// pass.
	Armed.AbilitySystem->SetNumericAttributeBase(Speed, 2.0f);
	TestEqual(TEXT("with nothing recorded, the attribute is the answer"),
		UCataclysmBasicAttack::SecondsBetweenSwingsFor(Armed.AbilitySystem),
		UCataclysmBasicAttack::SecondsBetweenSwings(2.0f), 0.0001f);

	// NOW GIVE IT SANGUINE MOMENTUM AT ITS FULL SIX POINTS: six percentage
	// points of increased attack speed a stack, with the attribute untouched.
	FCataclysmStatModifier Momentum;
	Momentum.Bucket = ECataclysmStatBucket::Increased;
	Momentum.Source = ECataclysmModifierSource::PassiveKeystone;
	Momentum.Value = 6.0f;
	Momentum.Scale = ECataclysmStatScale::PerStackOfSanguineMomentum;
	Momentum.ScaleStep = 1.0f;

	FCataclysmStatInputs Inputs;
	Inputs.Base = 2.0f;
	Inputs.Modifiers.Add(Momentum);

	TMap<FName, FCataclysmStatInputs> Stats;
	Stats.Add(FName(TEXT("attack_speed")), Inputs);
	Armed.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	// WITH NO STACKS IT IS STILL THE PLAIN RATE.
	const float Plain =
		UCataclysmBasicAttack::SecondsBetweenSwingsFor(Armed.AbilitySystem);
	TestEqual(TEXT("holding no stacks, the rate is unchanged"),
		Plain, UCataclysmBasicAttack::SecondsBetweenSwings(2.0f), 0.0001f);

	// AND FIVE STACKS MAKE IT FASTER. Two swings a second times 1.30 is 2.6,
	// which is a shorter interval between swings.
	for (int32 Granted = 0; Granted < 5; ++Granted)
	{
		Armed.AbilitySystem->GrantStack(
			ECataclysmStackKind::SanguineMomentum,
			UCataclysmStacks::WindowSecondsFor(
				ECataclysmStackKind::SanguineMomentum),
			UCataclysmStacks::CapFor(ECataclysmStackKind::SanguineMomentum));
	}

	const float Stacked =
		UCataclysmBasicAttack::SecondsBetweenSwingsFor(Armed.AbilitySystem);
	TestEqual(TEXT("five stacks of six points is 2.6 swings a second"),
		Stacked, UCataclysmBasicAttack::SecondsBetweenSwings(2.6f), 0.0001f);
	TestTrue(FString::Printf(TEXT("which is faster than the plain rate "
								  "(%.4f against %.4f)"), Stacked, Plain),
			 Stacked < Plain);

	// AND THE ATTRIBUTE ITSELF NEVER MOVED, which is the point: a state that
	// expires must not be written onto one.
	TestEqual(TEXT("and the attribute was never written"),
		Armed.AbilitySystem->GetNumericAttribute(Speed), 2.0f, 0.0001f);

	// AND THE STACKS LAPSING PUTS THE RATE BACK, with nothing else happening.
	World->TimeSeconds += UCataclysmStacks::WindowSecondsFor(
		ECataclysmStackKind::SanguineMomentum) + 0.1f;
	TestEqual(TEXT("once the stacks lapse the rate is the plain one again"),
		UCataclysmBasicAttack::SecondsBetweenSwingsFor(Armed.AbilitySystem),
		Plain, 0.0001f);

	return true;
}

// --------------------------------------------------------------------------
// Swinging at one chosen target, because the player clicked it. Issue #1187
// --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackOneTargetInReach,
	"Cataclysm.BasicAttack.OnlyALivingEnemyInsideTheReachCanBeSwungAt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackOneTargetInReach::RunTest(const FString&)
{
	/**
	 * THE NARROW FORM OF THE QUESTION ABOVE. `SomethingInReach` asks whether a
	 * fight is happening, which is what an automatic attack needed to know.
	 * Issue #1187 put the basic attack on the left mouse button, so the question
	 * became "is the thing the player pointed at close enough to hit".
	 *
	 * IT MUST REFUSE THE SAME THINGS THE SPHERE SEARCH REFUSES -- an ally and a
	 * corpse -- or a click would start a swing the automatic search never would.
	 */
	UWorld* World = CataclysmBasicAttackTest::MakeWorld();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player =
		CataclysmBasicAttackTest::SpawnPlayer(World);
	if (!TestNotNull(TEXT("a player pawn"), Player))
	{
		return false;
	}

	// A Greataxe's basic attack has a radius of 2.4 metres.
	constexpr float GreataxeReachCm = 240.0f;

	ACataclysmEnemyCharacter* Close = CataclysmBasicAttackTest::SpawnEnemyAt(
		World, FVector(100.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = CataclysmBasicAttackTest::SpawnEnemyAt(
		World, FVector(900.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an enemy one metre away"), Close)
		|| !TestNotNull(TEXT("an enemy nine metres away"), Far))
	{
		return false;
	}

	TestTrue(TEXT("the near enemy can be swung at"),
		UCataclysmBasicAttack::TargetIsInReach(Player, Close, GreataxeReachCm));
	TestFalse(TEXT("the far one cannot, and distance is why"),
		UCataclysmBasicAttack::TargetIsInReach(Player, Far, GreataxeReachCm));

	// AN ALLY IS NOT A TARGET. A second player-team character standing in the
	// same place as the enemy above is refused, so the answer is about sides and
	// not only about distance.
	ACataclysmPlayerCharacter* Ally =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector(100.0f, 0.0f, 0.0f),
													 FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an ally one metre away"), Ally))
	{
		Ally->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		TestFalse(TEXT("an ally in reach is not a target"),
			UCataclysmBasicAttack::TargetIsInReach(Player, Ally, GreataxeReachCm));
	}

	// A CORPSE IS NOT A TARGET EITHER, and it is an actor for as long as its
	// death clip runs, so this is a real case rather than a defensive one.
	UCataclysmSkillEffects::MarkDead(Close);
	TestFalse(TEXT("a dead enemy in reach is not a target"),
		UCataclysmBasicAttack::TargetIsInReach(Player, Close, GreataxeReachCm));

	// AND THE DEGENERATE ANSWERS, which a Shield produces: it grants no basic
	// attack, so it reaches nothing whatever is standing next to it.
	TestFalse(TEXT("no reach means nothing is ever in reach"),
		UCataclysmBasicAttack::TargetIsInReach(Player, Far, 0.0f));
	TestFalse(TEXT("no target is not a target"),
		UCataclysmBasicAttack::TargetIsInReach(Player, nullptr, GreataxeReachCm));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackRateLimit,
	"Cataclysm.BasicAttack.ClickingFasterThanTheWeaponSwingsDoesNotSwingFaster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackRateLimit::RunTest(const FString&)
{
	/**
	 * THE RATE LIMIT THAT USED TO BE THE TIMER ITSELF. While the basic attack
	 * fired from a repeating timer, the weapon's attack speed WAS the interval
	 * and nothing else had to enforce it. A button can be pressed faster than
	 * any weapon swings, so removing the timer in issue #1187 removed the rate
	 * limit with it, and this is what puts it back.
	 *
	 * `Swing` REFUSING A SECOND COPY IS NOT THIS. That asks whether the previous
	 * activation is still running, which is about the animation; a weapon whose
	 * animation is shorter than its interval would pass it and still swing too
	 * often.
	 */

	// An Axe swings 1.25 times a second, so 0.8 seconds between swings.
	constexpr float AxeInterval = 0.8f;

	// A FIRST SWING IS ALWAYS ALLOWED. The controller starts with a last-swing
	// time far in the past so that a character does not have to wait one
	// interval before its first hit.
	TestTrue(TEXT("a character that has never swung may swing"),
		UCataclysmBasicAttack::IntervalHasPassed(-1000.0f, 0.0f, AxeInterval));

	TestFalse(TEXT("pressing again immediately does not swing"),
		UCataclysmBasicAttack::IntervalHasPassed(10.0f, 10.0f, AxeInterval));
	TestFalse(TEXT("nor does pressing again halfway through the interval"),
		UCataclysmBasicAttack::IntervalHasPassed(10.0f, 10.4f, AxeInterval));

	// EXACTLY ONE INTERVAL IS ENOUGH, rather than needing to be exceeded. A
	// weapon that states 1.25 swings a second must actually reach 1.25.
	TestTrue(TEXT("a full interval later, it swings"),
		UCataclysmBasicAttack::IntervalHasPassed(10.0f, 10.8f, AxeInterval));
	TestTrue(TEXT("and longer than that certainly does"),
		UCataclysmBasicAttack::IntervalHasPassed(10.0f, 30.0f, AxeInterval));

	// NO RATE MEANS NEVER SWING, NOT SWING CONTINUOUSLY. A character holding
	// nothing, or holding something that states no attack speed, reads zero
	// here, and `SecondsBetweenSwings` says that means "never".
	TestFalse(TEXT("no rate at all means it never swings"),
		UCataclysmBasicAttack::IntervalHasPassed(-1000.0f, 500.0f, 0.0f));
	TestFalse(TEXT("and a negative rate is the same answer"),
		UCataclysmBasicAttack::IntervalHasPassed(-1000.0f, 500.0f, -1.0f));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
