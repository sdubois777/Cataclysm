// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
// For the minion that must not take its summoner's penetration. Issue #659.
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
// For pinning the critical strike roll. See FScopedNoCriticalStrikes below.
#include "HAL/IConsoleManager.h"

/**
 * Tests for a hit carrying its damage type, and for the generic resistance an
 * untyped hit meets.
 *
 * WHAT THESE GUARD. Issue #486: every hit in the running game resolved as an
 * untyped direct hit, so `ResistanceFor` selected none of the defender's eight
 * resistances and returned zero. The Abyssal Warden's 35% -- the entire
 * mechanical content of "high damage resistance" in its design -- did nothing,
 * and so did the player's eight resistances and the whole Penetration stat.
 *
 * THE SHAPE THE PROJECT OWNER RULED FOR, on 2026-08-12: "enemies will have a
 * generic all res. The only damage that should actually be typed is enemy damage
 * so the player's resistances can take effect", and then, on the first attempt at
 * it: "noooo not a ninth resistance. Either remove all of the 8 resistance types
 * on enemies and give them an all res, or make all 8 values the same. The first
 * is probably the better option."
 *
 * So the two sides of a fight hold DIFFERENT ATTRIBUTE SETS and never both, and
 * one direction of damage carries a type:
 *
 *     an ENEMY holds UCataclysmAllResistanceAttributeSet, one figure, no types
 *     a PLAYER holds UCataclysmResistanceAttributeSet, eight figures, no generic
 *     an ENEMY'S hit says which of the player's eight applies
 *     a PLAYER'S hit says nothing, because there is nothing to choose
 *
 * TWO MORE PROPERTIES TRAVEL THE SAME WAY, added under issue #513: whether the
 * hit landed on ground rather than touching a target, which decides the evasion
 * step, and whether it is damage over time, which decides whether an energy
 * shield absorbs it.
 *
 * WHAT THESE DELIBERATELY DO NOT COVER: armour penetration, which no attribute
 * in the project holds, and the weapon sub-type, which decides the slashing and
 * magic bonuses. Both are in the issue filed alongside #513.
 */

namespace CataclysmDamageTypeTest
{
	static const TCHAR* const EveryDamageType[] = {
		TEXT("War"), TEXT("Demonic"), TEXT("Death"), TEXT("Pestilence"),
		TEXT("Famine"), TEXT("Celestial"), TEXT("Chaos"), TEXT("Void"),
	};

	/**
	 * Stops critical strikes for as long as it is in scope, then restores.
	 *
	 * WITHOUT THIS, ELEVEN TESTS IN THIS FILE FAIL AT RANDOM. Every test below
	 * that asserts an exact damage figure attacks with either a spawned
	 * `ACataclysmEnemyCharacter`, which takes 5% critical strike chance from its
	 * archetype, or an attacker holding a weapon, which takes the same 5% from
	 * the skills the weapon grants. A critical strike multiplies the hit by 1.5,
	 * so each assertion had a one in twenty chance of reading half as much again
	 * as it expected, and a test with three hits in it had roughly one in seven.
	 *
	 * THAT IS WORSE THAN A TEST THAT FAILS. Two of the eleven failed on the first
	 * run after the roll was added and nine passed, which would have left nine
	 * tests failing later for no reason anyone could connect to this change.
	 * Running the whole suite with `Cataclysm.CritRoll 0`, which makes every hit
	 * a critical strike, is what produced the list of eleven.
	 *
	 * 100 NEVER CRITICALLY STRIKES, because the roll is compared with strictly
	 * less than and a chance is capped at 100.
	 *
	 * These tests are about resistance, penetration, evasion and the weapon
	 * sub-types. A critical strike is not what any of them is measuring, so
	 * pinning it is removing noise rather than avoiding a case.
	 * `Cataclysm.Damage.*` and `Cataclysm.Overlay.*` are where the roll itself is
	 * tested.
	 */
	struct FScopedNoCriticalStrikes
	{
		FScopedNoCriticalStrikes()
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Cataclysm.CritRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();

				// SET AT THE CONSOLE'S OWN PRIORITY, and that is not a detail.
				// A console variable in Unreal remembers who set it, and a write
				// from code is silently discarded when the command line or a
				// console command has already set it. A plain `Set(100.0f)` did
				// nothing at all when the suite was run with
				// `Cataclysm.CritRoll 0` to find these eleven tests: the pin was
				// there in the source and absent from the run, and all eleven
				// still failed. Nothing reported that the write had been dropped.
				Variable->Set(100.0f, ECVF_SetByConsole);
			}
		}

		~FScopedNoCriticalStrikes()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	static UWorld* MakeWorldThatHasBegunPlay()
	{
		return CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	}

	/**
	 * A bare actor that can hold attributes and be an instigator.
	 *
	 * IT HOLDS BOTH KINDS OF RESISTANCE, which no real character does: an enemy
	 * holds the all-damage one and a player holds the eight. Holding both lets one
	 * helper stand in for either side, and it is also the only way to check that
	 * the two ADD rather than one silently winning.
	 */
	struct FScopedCombatant
	{
		explicit FScopedCombatant(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);

			Vitals = NewVitals;
			Combat = NewCombat;
			Resistances = NewResist;
			AllResistance = NewAllResist;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Vitals->SetMaxHealth(1'000'000.0f);
			Vitals->SetHealth(1'000'000.0f);
		}

		~FScopedCombatant()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Health lost since the pool was last read. */
		float TakeDamageReading()
		{
			const float Now = Vitals->GetHealth();
			const float Lost = LastHealth - Now;
			LastHealth = Now;
			return Lost;
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
		TObjectPtr<UCataclysmResistanceAttributeSet> Resistances = nullptr;
		TObjectPtr<UCataclysmAllResistanceAttributeSet> AllResistance = nullptr;
		float LastHealth = 1'000'000.0f;
	};

	/** Resolve a hit of this type against this defender, both rolls pinned off. */
	static FCataclysmDamageResult Resolve(const FScopedCombatant& Defender,
										  float Damage, FName DamageType,
										  float Penetration = 0.0f)
	{
		FCataclysmIncomingHit Incoming;
		Incoming.Damage = Damage;
		Incoming.DamageType = DamageType;
		Incoming.ResistancePenetration = Penetration;
		return UCataclysmDamageCalculation::Resolve(
			Incoming, Defender.AbilitySystem, /*Tier=*/1,
			/*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The vocabulary, and the encoding a damage type travels as
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmElementTagsExistTest,
	"Cataclysm.DamageType.AllEightElementTagsExistInTheVocabulary")
{
	// WITHOUT THIS THE WHOLE FEATURE FAILS SILENTLY. ElementTagFor requests the
	// tag by name with ErrorIfNotFound false, so a vocabulary that has lost one
	// returns an invalid tag, nothing is put on the effect, the hit arrives
	// untyped and every resistance quietly goes back to doing nothing -- which is
	// the exact state issue #486 describes. The tags come from the Tags sheet of
	// docs/All_Things_Cataclysm.xlsx by way of tools/generate_gameplay_tags.py,
	// so an edit to the workbook can remove one without touching any C++.
	for (const TCHAR* const Type : CataclysmDamageTypeTest::EveryDamageType)
	{
		TestTrue(*FString::Printf(TEXT("Element.%s is a known tag"), Type),
			UCataclysmDamageCalculation::ElementTagFor(FName(Type)).IsValid());
	}

	return true;
}

CATACLYSM_TEST(FCataclysmDamageTypeRoundTripsTest,
	"Cataclysm.DamageType.ADamageTypeSurvivesTheTripToATagAndBack")
{
	// ElementTagFor and DamageTypeFromTags are the only encoding and decoding of
	// a damage type in the project. If they ever disagreed the type would vanish
	// on the way and no test that looked at only one of them would notice.
	for (const TCHAR* const Type : CataclysmDamageTypeTest::EveryDamageType)
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(UCataclysmDamageCalculation::ElementTagFor(FName(Type)));

		TestEqual(*FString::Printf(TEXT("%s comes back as itself"), Type),
			UCataclysmDamageCalculation::DamageTypeFromTags(Tags), FName(Type));
	}

	return true;
}

CATACLYSM_TEST(FCataclysmUntypedHitHasNoTagTest,
	"Cataclysm.DamageType.AnUntypedHitCarriesNoElementTag")
{
	TestFalse(TEXT("no damage type gives no tag"),
		UCataclysmDamageCalculation::ElementTagFor(NAME_None).IsValid());

	TestFalse(TEXT("a damage type nobody has heard of gives no tag"),
		UCataclysmDamageCalculation::ElementTagFor(FName(TEXT("Sarcasm"))).IsValid());

	// Tags that are not Element tags must not be mistaken for one. Every damage
	// effect in the game carries other tags -- a burn carries Keyword.DoT.Burn --
	// so reading the first tag rather than the first Element tag would work until
	// the day the order changed.
	FGameplayTagContainer Other;
	Other.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Burn")), /*ErrorIfNotFound=*/false));
	TestEqual(TEXT("a burn tag is not a damage type"),
		UCataclysmDamageCalculation::DamageTypeFromTags(Other), FName(NAME_None));

	TestEqual(TEXT("and no tags at all is an untyped hit"),
		UCataclysmDamageCalculation::DamageTypeFromTags(FGameplayTagContainer()),
		FName(NAME_None));

	return true;
}

// --------------------------------------------------------------------------
// Which side of a fight types its damage
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmOnlyEnemyDamageIsTypedTest,
	"Cataclysm.DamageType.OnlyAnEnemysDamageCarriesAType")
{
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Enemy = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an enemy"), Enemy))
	{
		TestEqual(TEXT("an enemy's damage is its own damage type"),
			UCataclysmSkillEffects::DamageTypeOf(Enemy), FName(TEXT("Demonic")));

		// Read off the creature rather than hard-coded a second time, so a
		// creature given a different Cataclysm keeps this test true.
		Enemy->DamageType = FName(TEXT("Void"));
		TestEqual(TEXT("and it follows the creature, not the class"),
			UCataclysmSkillEffects::DamageTypeOf(Enemy), FName(TEXT("Void")));

		Enemy->Destroy();
	}

	// ANYTHING THAT IS NOT AN ENEMY IS UNTYPED, which is the player and every
	// projectile and minion a player sends. That is the ruling, not an omission:
	// an enemy resists everything equally, so a type would be selecting between
	// eight copies of one number.
	AActor* NotAnEnemy = World->SpawnActor<AActor>();
	if (TestNotNull(TEXT("a plain actor"), NotAnEnemy))
	{
		TestEqual(TEXT("a player's damage carries no type"),
			UCataclysmSkillEffects::DamageTypeOf(NotAnEnemy), FName(NAME_None));
		NotAnEnemy->Destroy();
	}

	TestEqual(TEXT("and nothing at all carries no type"),
		UCataclysmSkillEffects::DamageTypeOf(nullptr), FName(NAME_None));

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// The generic resistance, which is what an untyped hit meets
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmGenericResistanceMeetsAnUntypedHitTest,
	"Cataclysm.DamageType.AnUntypedHitStillMeetsTheGenericResistance")
{
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	{
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.AllResistance->SetAllResistance(35.0f);

		// THE DEFECT IN ISSUE #486, IN ONE ASSERTION. A player's hit carries no
		// damage type, so before this the lookup selected none of the eight
		// resistances and returned zero: the Abyssal Warden's 35% removed nothing
		// at all and 1,000 damage arrived as 1,000.
		const FCataclysmDamageResult Untyped =
			CataclysmDamageTypeTest::Resolve(Defender, 1'000.0f, NAME_None);
		TestEqual(TEXT("35 percent of an untyped hit is resisted"),
			Untyped.DealtToHealth, 650.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmGenericResistanceMeetsEveryTypeTest,
	"Cataclysm.DamageType.TheGenericResistanceMeetsAHitOfAnyType")
{
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	{
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.AllResistance->SetAllResistance(35.0f);

		// The word "generic" has to mean every type and not merely the untyped
		// case, or a creature would resist a player's plain hit and not its
		// elemental one.
		for (const TCHAR* const Type : CataclysmDamageTypeTest::EveryDamageType)
		{
			TestEqual(*FString::Printf(TEXT("a %s hit is resisted too"), Type),
				CataclysmDamageTypeTest::Resolve(Defender, 1'000.0f,
												 FName(Type)).DealtToHealth,
				650.0f, 0.01f);
		}

		// Including one the lookup has never heard of. Being generic means it
		// cannot depend on the type being recognised.
		TestEqual(TEXT("and so is a hit of a type nobody has heard of"),
			CataclysmDamageTypeTest::Resolve(Defender, 1'000.0f,
											 FName(TEXT("Sarcasm"))).DealtToHealth,
			650.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmTypedResistanceAddsToTheGenericOneTest,
	"Cataclysm.DamageType.ATypedResistanceAddsToTheGenericOne")
{
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	{
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.AllResistance->SetAllResistance(20.0f);
		Defender.Resistances->SetDemonicResistance(30.0f);

		// The two parts add, so a character can carry both. No creature does
		// today -- an enemy holds only the generic one and a player only the
		// eight -- and the rule has to be stated anyway, because "which of the
		// two wins" is otherwise decided by accident.
		TestEqual(TEXT("20 generic and 30 Demonic remove half of a Demonic hit"),
			CataclysmDamageTypeTest::Resolve(Defender, 1'000.0f,
											 FName(TEXT("Demonic"))).DealtToHealth,
			500.0f, 0.01f);

		// And a type the defender has no typed resistance to meets the generic
		// part alone.
		TestEqual(TEXT("a Void hit meets the generic 20 only"),
			CataclysmDamageTypeTest::Resolve(Defender, 1'000.0f,
											 FName(TEXT("Void"))).DealtToHealth,
			800.0f, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// End to end: through a real gameplay effect, the way a fight does it
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmEnemyHitIsMetByThePlayersResistanceTest,
	"Cataclysm.DamageType.AnEnemysHitIsMetByTheResistanceToItsOwnType")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1'000.0f);

		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		// The shape a player has: eight typed resistances and no generic one.
		Defender.Resistances->SetDemonicResistance(40.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		// EVERY STEP THE REAL GAME TAKES. ApplyHit builds the effect, puts the
		// attacker's Element tag on it, applies it to the Damage meta attribute,
		// and PostGameplayEffectExecute reads the tag back off the spec. Calling
		// Resolve directly would skip all of that, and all of that is what
		// issue #486 was about.
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);

		TestEqual(TEXT("the player's 40 percent Demonic resistance applies"),
			Defender.TakeDamageReading(), 600.0f, 1.0f);

		// AND A RESISTANCE TO SOMETHING ELSE DOES NOT. Without this the test
		// above would pass against a build that applied every resistance to
		// everything, which is the opposite mistake and just as wrong.
		Defender.Resistances->SetDemonicResistance(0.0f);
		Defender.Resistances->SetVoidResistance(40.0f);
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);

		TestEqual(TEXT("resistance to a type it does not deal does not apply"),
			Defender.TakeDamageReading(), 1'000.0f, 1.0f);

		Attacker->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPenetrationReachesTheResistanceStepTest,
	"Cataclysm.DamageType.TheAttackersPenetrationReachesTheResistanceStep")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1'000.0f);

		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Resistances->SetDemonicResistance(40.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("with no penetration, 40 percent is resisted"),
			Defender.TakeDamageReading(), 600.0f, 1.0f);

		// PENETRATION IS A WHOLE PLAYER STAT WITH AFFIXES BEHIND IT, and until
		// issue #486 it reduced a resistance that was already zero. Read off the
		// ATTACKER, because penetration belongs to whoever is swinging rather
		// than to any one blow.
		if (UAbilitySystemComponent* Offence =
				UCataclysmTargeting::AbilitySystemOf(Attacker))
		{
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetPenetrationAttribute(), 15.0f);
		}

		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("15 penetration leaves 25 percent resisted"),
			Defender.TakeDamageReading(), 750.0f, 1.0f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPlayerHitMeetsTheGenericResistanceEndToEndTest,
	"Cataclysm.DamageType.AnUntypedHitMeetsAnEnemysResistanceThroughARealEffect")
{
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	// A player stands in as a plain actor with attributes, because what matters
	// here is only that it is not an enemy and so types nothing.
	CataclysmDamageTypeTest::FScopedCombatant Player(World);
	Player.AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1'000.0f);

	ACataclysmEnemyCharacter* Target = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an enemy to hit"), Target))
	{
		Target->SetHealth(1'000'000.0f);
		Target->ResistancePercent = 35.0f;
		Target->ApplyStartingAttributes();

		UAbilitySystemComponent* Defence =
			UCataclysmTargeting::AbilitySystemOf(Target);
		if (TestNotNull(TEXT("the target's ability system"), Defence))
		{
			const float Before = Defence->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute());

			UCataclysmSkillEffects::ApplyHit(Player.Actor, Target, 100.0f);

			const float Lost = Before - Defence->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetHealthAttribute());

			// THE WHOLE POINT OF THE GENERIC RESISTANCE. The hit carries no type,
			// the creature holds one figure rather than eight, and the two still
			// meet. Before issue #486 this creature lost the full 1,000.
			TestEqual(TEXT("the enemy's 35 percent resists a player's untyped hit"),
				Lost, 650.0f, 1.0f);
		}

		Target->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// How a hit arrived: area damage and damage over time. Issue #513.
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmDeliveryTagsExistTest,
	"Cataclysm.DamageType.TheTwoDeliveryTagsExistInTheVocabulary")
{
	// The same silent-failure risk the element tags have. Both are requested by
	// name with ErrorIfNotFound false, so a vocabulary that has lost one returns
	// an invalid tag, nothing is put on the effect, and the property stops
	// travelling with no error anywhere. `Type.AOE` and `Keyword.DoT` are PARENTS
	// rather than leaves -- the vocabulary declares Type.AOE.Aura and
	// Keyword.DoT.Burn and the rest -- so this also checks that a parent tag
	// resolves at all, which the whole scheme rests on.
	TestTrue(TEXT("Type.AOE is a known tag"),
		UCataclysmDamageCalculation::AreaDamageTag().IsValid());
	TestTrue(TEXT("Keyword.DoT is a known tag"),
		UCataclysmDamageCalculation::DamageOverTimeTag().IsValid());

	TestNotEqual(TEXT("and they are two different tags"),
		UCataclysmDamageCalculation::AreaDamageTag(),
		UCataclysmDamageCalculation::DamageOverTimeTag());

	// A parent has to match its children, because that is how a burn carrying
	// Keyword.DoT.Burn is recognised as damage over time.
	FGameplayTagContainer Burn;
	Burn.AddTag(UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Burn")), /*ErrorIfNotFound=*/false));
	TestTrue(TEXT("Keyword.DoT.Burn counts as Keyword.DoT"),
		Burn.HasTag(UCataclysmDamageCalculation::DamageOverTimeTag()));

	return true;
}

CATACLYSM_TEST(FCataclysmAreaDamageCannotBeEvadedTest,
	"Cataclysm.DamageType.AreaDamageCannotBeEvadedAndADirectHitCan")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1000.0f);

		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		// Evades everything it is allowed to evade, so the roll cannot decide the
		// outcome and this measures the rule rather than the dice.
		Defender.Combat->SetEvasion(100.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		// THE DEFECT IN ISSUE #513. Every hit arrived as a direct one, so an
		// evasive character dodged an explosion centred on itself.
		FCataclysmHitDelivery Area;
		Area.bIsArea = true;
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), Area);
		TestEqual(TEXT("area damage lands on a character that evades everything"),
			Defender.TakeDamageReading(), 1000.0f, 1.0f);

		// AND A DIRECT HIT IS STILL EVADED. Without this the check above would
		// pass against a build that had simply stopped evading anything, which is
		// the opposite mistake and just as wrong.
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("a direct hit on the same character is avoided entirely"),
			Defender.TakeDamageReading(), 0.0f, 1.0f);

		Attacker->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmAreaDamageIsStillMitigatedTest,
	"Cataclysm.DamageType.AreaDamageSkipsEvasionAndNothingElse")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1000.0f);

		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Resistances->SetDemonicResistance(40.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		// "Cannot be evaded" is not "cannot be mitigated". Resistance, armour,
		// block and flat reduction all still apply to area damage, and only the
		// evasion roll is skipped.
		FCataclysmHitDelivery Area;
		Area.bIsArea = true;
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), Area);
		TestEqual(TEXT("resistance still applies to area damage"),
			Defender.TakeDamageReading(), 600.0f, 1.0f);

		Attacker->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmDamageOverTimeBypassesTheShieldTest,
	"Cataclysm.DamageType.AnEnergyShieldDoesNotAbsorbDamageOverTime")
{
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1000.0f);

		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Vitals->SetMaxEnergyShield(100000.0f);
		Defender.Vitals->SetEnergyShield(100000.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		// AN ORDINARY HIT IS ABSORBED WHOLE, because the shield is far larger than
		// the hit. That is the control for the case below.
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("a direct hit is absorbed by the shield"),
			Defender.TakeDamageReading(), 0.0f, 1.0f);

		// DAMAGE OVER TIME GOES STRAIGHT PAST IT. That is what makes an energy
		// shield a distinct defence rather than a second health bar, and it is the
		// design's answer to shield stacking.
		FCataclysmHitDelivery OverTime;
		OverTime.bIsDamageOverTime = true;
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), OverTime);
		TestEqual(TEXT("damage over time reaches health through a full shield"),
			Defender.TakeDamageReading(), 1000.0f, 1.0f);

		Attacker->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmAnOrdinaryHitCarriesNeitherTest,
	"Cataclysm.DamageType.AnOrdinaryHitIsNeitherAreaNorOverTime")
{
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1000.0f);

		// A CHARACTER THAT EVADES EVERYTHING AND HAS A HUGE SHIELD takes nothing
		// from an ordinary blow. It would take damage if either property leaked
		// onto a hit that should not carry it, which is the failure this catches:
		// the default has to be a direct hit that a shield absorbs.
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Combat->SetEvasion(100.0f);
		Defender.Vitals->SetMaxEnergyShield(100000.0f);
		Defender.Vitals->SetEnergyShield(100000.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("an ordinary hit is evaded and reaches no health"),
			Defender.TakeDamageReading(), 0.0f, 1.0f);

		Attacker->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmAreaComesFromTheSkillsTagsTest,
	"Cataclysm.DamageType.ASkillTaggedForAreaDamageCannotBeEvaded")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1000.0f);

		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Combat->SetEvasion(100.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		UGameplayTagsManager& Tags = UGameplayTagsManager::Get();

		// TAGGED FOR AREA DAMAGE, AND NOTHING AT THE CALL SITE SAYS SO. This is the
		// route 37 designed skills already take: `game/Data/WeaponSkills.csv` gives
		// 33 of them Type.AOE.PointBlank and 4 of them Type.AOE.Aura, and the tags
		// travel to ApplyHit with the hit.
		FGameplayTagContainer PointBlank;
		PointBlank.AddTag(Tags.RequestGameplayTag(
			FName(UCataclysmSkillEffects::PointBlankAreaTagName),
			/*ErrorIfNotFound=*/false));
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f, PointBlank);
		TestEqual(TEXT("an explosion lands on a character that evades everything"),
			Defender.TakeDamageReading(), 1000.0f, 1.0f);

		FGameplayTagContainer Aura;
		Aura.AddTag(Tags.RequestGameplayTag(
			FName(UCataclysmSkillEffects::AuraAreaTagName),
			/*ErrorIfNotFound=*/false));
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f, Aura);
		TestEqual(TEXT("and so does an aura"),
			Defender.TakeDamageReading(), 1000.0f, 1.0f);

		// A GROUND-LEAVING TAG IS NOT AN AREA HIT. Type.AOE.Persistent is defined
		// as "Ground effects, clouds, zones", so it describes the patch a skill
		// leaves rather than the blow it lands. Flamedart carries it and is a
		// charge: the charge makes contact and must stay evadable. Without this
		// check, matching on the Type.AOE parent would look correct and would make
		// 26 designed skills unevadable by accident.
		FGameplayTagContainer Persistent;
		Persistent.AddTag(Tags.RequestGameplayTag(
			FName(TEXT("Type.AOE.Persistent")), /*ErrorIfNotFound=*/false));
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f, Persistent);
		TestEqual(TEXT("a skill that leaves burning ground still lands an evadable blow"),
			Defender.TakeDamageReading(), 0.0f, 1.0f);

		// AND A SKILL WITH NO AREA TAG IS EVADED. Cinderslash is Type.Strike and
		// Type.Melee and nothing else: one sword blow.
		FGameplayTagContainer Melee;
		Melee.AddTag(Tags.RequestGameplayTag(FName(TEXT("Type.Strike")),
											  /*ErrorIfNotFound=*/false));
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f, Melee);
		TestEqual(TEXT("a plain strike is avoided entirely"),
			Defender.TakeDamageReading(), 0.0f, 1.0f);

		Attacker->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmAreaTagsAreReadCorrectlyTest,
	"Cataclysm.DamageType.OnlyTwoOfTheThreeAreaTagsMeanTheHitIsArea")
{
	UGameplayTagsManager& Tags = UGameplayTagsManager::Get();

	auto Container = [&Tags](const TCHAR* Name)
	{
		FGameplayTagContainer Held;
		Held.AddTag(Tags.RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false));
		return Held;
	};

	// The vocabulary declares three Type.AOE tags and only two of them describe
	// the blow. Checked here as well as through a real hit above, because this is
	// the whole of the rule and it is one line of code.
	TestTrue(TEXT("Type.AOE.PointBlank is area damage"),
		UCataclysmSkillEffects::IsAreaDamage(
			Container(UCataclysmSkillEffects::PointBlankAreaTagName)));
	TestTrue(TEXT("Type.AOE.Aura is area damage"),
		UCataclysmSkillEffects::IsAreaDamage(
			Container(UCataclysmSkillEffects::AuraAreaTagName)));
	TestFalse(TEXT("Type.AOE.Persistent is the ground it leaves, not the blow"),
		UCataclysmSkillEffects::IsAreaDamage(Container(TEXT("Type.AOE.Persistent"))));

	TestFalse(TEXT("a skill with no tags at all deals a direct hit"),
		UCataclysmSkillEffects::IsAreaDamage(FGameplayTagContainer()));
	TestFalse(TEXT("and so does an ordinary strike"),
		UCataclysmSkillEffects::IsAreaDamage(Container(TEXT("Type.Strike"))));

	return true;
}

// --------------------------------------------------------------------------
// Armour penetration, which is a different stat from the one above. Issue #520.
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmArmorPenetrationReachesTheArmorStepTest,
	"Cataclysm.DamageType.TheAttackersArmorPenetrationReachesTheArmorStep")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	// WHAT WAS WRONG. `FCataclysmIncomingHit::ArmorPenetration` was applied
	// correctly by UCataclysmDamageCalculation::Resolve and was never set, because
	// nothing in the project held an armour penetration value. Three enchantments
	// in game/Data/EnchantmentsPositive.csv grant it and none could do anything.
	//
	// A DIFFERENT STAT FROM THE RESISTANCE PENETRATION ABOVE. That one is
	// subtracted from the target's resistance at step 4; this ignores a share of
	// its armour at step 3. The test above would pass with the two confused,
	// which is why this one sets armour and no resistance at all.
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1'000.0f);

		CataclysmDamageTypeTest::FScopedCombatant Defender(World);

		// 800 ARMOUR AT TIER 1 IS EXACTLY HALF A HIT, because armour removes
		// `armor / (armor + 800 x tier)`. A round figure makes the two readings
		// below arithmetic rather than approximate.
		Defender.Combat->SetArmor(800.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("with no armour penetration, half the hit is stopped"),
			Defender.TakeDamageReading(), 500.0f, 1.0f);

		// READ OFF THE ATTACKER, because penetration of either kind belongs to
		// whoever is swinging rather than to any one blow.
		if (UAbilitySystemComponent* Offence =
				UCataclysmTargeting::AbilitySystemOf(Attacker))
		{
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute(), 50.0f);
		}

		// IGNORING HALF THE ARMOUR LEAVES 400, which removes a third rather than a
		// half, so 666.7 lands. Note the two are NOT proportional: the armour curve
		// bends, which is the whole reason it is a curve.
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("ignoring half the armour lets two thirds through"),
			Defender.TakeDamageReading(), 666.7f, 1.0f);

		// AND IGNORING ALL OF IT LETS THE WHOLE HIT THROUGH, which is what the
		// enchantment "Your first hit against each enemy ignores all armor" needs.
		if (UAbilitySystemComponent* Offence =
				UCataclysmTargeting::AbilitySystemOf(Attacker))
		{
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute(), 100.0f);
		}

		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("ignoring all of it lets the whole hit through"),
			Defender.TakeDamageReading(), 1'000.0f, 1.0f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmTheTwoPenetrationsAreSeparateTest,
	"Cataclysm.DamageType.TheTwoPenetrationStatsAreNotTheSameStat")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	// THE FAILURE THIS CATCHES is one attribute being read where the other was
	// meant, which every assertion above would survive: each of those tests sets
	// only the layer it is about, so a swap would look like the stat working.
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Attacker = World->SpawnActor<ACataclysmEnemyCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("an attacking enemy"), Attacker))
	{
		Attacker->SetAttackDamage(1'000.0f);

		CataclysmDamageTypeTest::FScopedCombatant Defender(World);

		// ARMOUR ONLY, AND NO RESISTANCE AT ALL. Resistance penetration therefore
		// has nothing to work on, and any damage it appears to add is it reaching
		// the wrong step.
		Defender.Combat->SetArmor(800.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		if (UAbilitySystemComponent* Offence =
				UCataclysmTargeting::AbilitySystemOf(Attacker))
		{
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetPenetrationAttribute(), 100.0f);
		}

		UCataclysmSkillEffects::ApplyHit(Attacker, Defender.Actor, 100.0f);
		TestEqual(TEXT("resistance penetration does nothing to armour"),
			Defender.TakeDamageReading(), 500.0f, 1.0f);

		// AND THE OTHER WAY ROUND: resistance and no armour, with armour
		// penetration at its maximum.
		CataclysmDamageTypeTest::FScopedCombatant Second(World);
		Second.Resistances->SetDemonicResistance(50.0f);
		Second.LastHealth = Second.Vitals->GetHealth();

		if (UAbilitySystemComponent* Offence =
				UCataclysmTargeting::AbilitySystemOf(Attacker))
		{
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetPenetrationAttribute(), 0.0f);
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute(), 100.0f);
		}

		UCataclysmSkillEffects::ApplyHit(Attacker, Second.Actor, 100.0f);
		TestEqual(TEXT("armour penetration does nothing to resistance"),
			Second.TakeDamageReading(), 500.0f, 1.0f);
	}

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// The weapon sub-type, which now reaches a hit. Issue #639.
// --------------------------------------------------------------------------

namespace CataclysmDamageTypeTest
{
	/** An attacker holding a weapon, so its sub-type reaches what it hits. */
	struct FScopedArmedAttacker
	{
		FScopedArmedAttacker(UWorld* World, const TCHAR* WeaponType)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
			check(Actor);

			// AN ENEMY CHARACTER CARRYING WEAPON SLOTS, which no real enemy does.
			// It stands in for a player because a player reaches its ability
			// system component through its player state and a test world has
			// none. What is checked is the join between a held weapon and a hit,
			// and that reads the component rather than the class.
			Slots = NewObject<UCataclysmWeaponSlotsComponent>(Actor);
			Slots->RegisterComponent();
			Slots->SetDamageType(TEXT("Demonic"));
			Slots->EquipWeaponType(WeaponType);

			// THE DAMAGE IS SET AFTER THE WEAPON, AND THAT ORDER IS THE TEST
			// WORKING. Equipping writes the weapon's own attack damage over
			// whatever was there -- a Fist states 30 against an Axe's 46 -- so
			// setting it first made every reading the weapon's base damage rather
			// than its sub-type. Measured: 8.5 and 14.4 where 1,000 and 1,100 were
			// expected. With one figure for every attacker, the sub-type is the
			// only thing that can move a reading.
			Actor->SetAttackDamage(1000.0f);
		}

		~FScopedArmedAttacker()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<ACataclysmEnemyCharacter> Actor = nullptr;
		TObjectPtr<UCataclysmWeaponSlotsComponent> Slots = nullptr;
	};
}

CATACLYSM_TEST(FCataclysmSlashingReachesHealthTest,
	"Cataclysm.DamageType.ASlashingWeaponDealsMoreToHealth")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		// NO ARMOUR, NO RESISTANCE AND NO SHIELD on the defender, so the only
		// thing that can move the reading is the slashing bonus itself.
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		{
			// A Fist is Blunt, which does nothing to health.
			CataclysmDamageTypeTest::FScopedArmedAttacker Blunt(World, TEXT("Fist"));
			UCataclysmSkillEffects::ApplyHit(Blunt.Actor, Defender.Actor, 100.0f);
			TestEqual(TEXT("a blunt weapon deals its hit unchanged"),
				Defender.TakeDamageReading(), 1000.0f, 1.0f);
		}

		{
			// An Axe is Slashing: 10% more to what reaches health.
			CataclysmDamageTypeTest::FScopedArmedAttacker Slashing(World, TEXT("Axe"));
			UCataclysmSkillEffects::ApplyHit(Slashing.Actor, Defender.Actor, 100.0f);
			TestEqual(TEXT("a slashing weapon deals ten percent more"),
				Defender.TakeDamageReading(), 1100.0f, 1.0f);
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmMagicStripsMoreShieldTest,
	"Cataclysm.DamageType.AMagicWeaponStripsMoreEnergyShield")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		// A SHIELD LARGER THAN THE HIT, so the whole hit lands on it and the
		// reading is what the shield lost rather than what health lost.
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Vitals->SetMaxEnergyShield(100000.0f);
		Defender.Vitals->SetEnergyShield(100000.0f);

		const auto ShieldLost = [&](const TCHAR* WeaponType) -> float
		{
			const float Before = Defender.Vitals->GetEnergyShield();
			CataclysmDamageTypeTest::FScopedArmedAttacker Armed(World, WeaponType);
			UCataclysmSkillEffects::ApplyHit(Armed.Actor, Defender.Actor, 100.0f);
			return Before - Defender.Vitals->GetEnergyShield();
		};

		// A Fist is Blunt, so the shield loses exactly the hit.
		TestEqual(TEXT("a blunt weapon strips its hit"),
			ShieldLost(TEXT("Fist")), 1000.0f, 1.0f);

		// A Wand is Magic: 10% more shield stripped per hit.
		TestEqual(TEXT("a magic weapon strips ten percent more"),
			ShieldLost(TEXT("Wand")), 1100.0f, 1.0f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPiercingIgnoresArmourTest,
	"Cataclysm.DamageType.APiercingWeaponIgnoresAShareOfArmour")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	// THE ONE THAT NEEDED ISSUE #520 FIRST. Piercing ignores 20% of the target's
	// armour, and until armour penetration was a stat there was nothing for that
	// 20% to be added to. Resolve combines the two the way
	// `Attacker.total_armor_ignored` in sim/cataclysm_sim/damage.py does.
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		// 800 ARMOUR AT TIER 1 IS EXACTLY HALF A HIT, so the figures below are
		// arithmetic rather than approximate.
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Combat->SetArmor(800.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		{
			// A Fist is Blunt, so the armour is met in full: 500 lands.
			CataclysmDamageTypeTest::FScopedArmedAttacker Blunt(World, TEXT("Fist"));
			UCataclysmSkillEffects::ApplyHit(Blunt.Actor, Defender.Actor, 100.0f);
			TestEqual(TEXT("a blunt weapon meets the whole armour"),
				Defender.TakeDamageReading(), 500.0f, 1.0f);
		}

		{
			// A Dagger is Piercing: it ignores 20%, so 640 armour is met, which
			// removes 44.4% and lets 555.6 through.
			CataclysmDamageTypeTest::FScopedArmedAttacker Piercing(World, TEXT("Dagger"));
			UCataclysmSkillEffects::ApplyHit(Piercing.Actor, Defender.Actor, 100.0f);
			TestEqual(TEXT("a piercing weapon ignores a fifth of it"),
				Defender.TakeDamageReading(), 555.6f, 1.0f);
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPiercingAddsToTheStatTest,
	"Cataclysm.DamageType.APiercingWeaponAddsToTheArmourPenetrationStat")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	// THEY ADD RATHER THAN ONE WINNING, which is the rule
	// `Attacker.total_armor_ignored` states and the reason Resolve owns the
	// combination rather than each caller doing it.
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Combat->SetArmor(800.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		CataclysmDamageTypeTest::FScopedArmedAttacker Piercing(World, TEXT("Dagger"));
		if (UAbilitySystemComponent* Offence =
				UCataclysmTargeting::AbilitySystemOf(Piercing.Actor))
		{
			// 80 FROM GEAR PLUS THE WEAPON'S 20 IS 100, so all the armour is
			// ignored and the whole hit lands. Either alone would not reach it.
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute(), 80.0f);
		}

		UCataclysmSkillEffects::ApplyHit(Piercing.Actor, Defender.Actor, 100.0f);
		TestEqual(TEXT("80 from gear plus a piercing weapon's 20 ignores all of it"),
			Defender.TakeDamageReading(), 1000.0f, 1.0f);
	}

	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// A hit that penetrates nothing, and the summoned minion it exists for.
// Issue #659.
//
// WHY THESE ARE IN THIS FILE RATHER THAN A MINION ONE. Every test of either
// penetration stat is here, and so is the piercing weapon test, which is half of
// what the minion inherits. Splitting the minion case out would put two halves of
// one rule in two places.
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmNoPenetrationTagExistsTest,
	"Cataclysm.DamageType.TheTagThatForbidsPenetrationIsInTheVocabulary")
{
	// AN INVALID TAG WOULD STOP THE FLAG TRAVELLING AND FAIL NOTHING ELSE. The tag
	// is requested by name rather than declared natively, so a name the Tags sheet
	// of docs/All_Things_Cataclysm.xlsx has lost answers with an invalid tag rather
	// than being created out of thin air. That is the right behaviour and it is
	// also silent: every minion would quietly start penetrating again and no other
	// test would notice. The same reasoning is why Keyword.NoCrit has this test.
	const FGameplayTag NoPenetration =
		UCataclysmDamageCalculation::NoPenetrationTag();

	TestTrue(TEXT("Keyword.NoPenetration is a tag the vocabulary knows"),
		NoPenetration.IsValid());
	TestEqual(TEXT("and it is spelled the way the generator writes it"),
		NoPenetration.GetTagName().ToString(),
		FString(TEXT("Keyword.NoPenetration")));

	return true;
}

CATACLYSM_TEST(FCataclysmCallerCanForbidPenetrationTest,
	"Cataclysm.DamageType.ACallerCanSayAHitIgnoresNoArmourOrResistance")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	// THE MECHANISM, TESTED APART FROM THE MINION THAT USES IT. All three routes
	// at once: the attacker's two penetration attributes and a piercing weapon.
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		// 800 ARMOUR AT TIER 1 REMOVES EXACTLY HALF, and 30 generic resistance
		// removes 30% of what is left, so every figure below is arithmetic.
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Combat->SetArmor(800.0f);
		Defender.AllResistance->SetAllResistance(30.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		// A Dagger is Piercing, which ignores a further 20% of armour on its own.
		CataclysmDamageTypeTest::FScopedArmedAttacker Attacker(World, TEXT("Dagger"));
		if (UAbilitySystemComponent* Offence =
				UCataclysmTargeting::AbilitySystemOf(Attacker.Actor))
		{
			// 80 from gear plus the weapon's 20 ignores all the armour, and 30
			// penetration cancels all the resistance.
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute(), 80.0f);
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetPenetrationAttribute(), 30.0f);
		}

		FCataclysmHitDelivery NoPenetration;
		NoPenetration.bCannotPenetrate = true;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f,
										 FGameplayTagContainer(), NoPenetration);

		// Half to armour, then 30% of the remainder to resistance: 350 of 1,000.
		TestEqual(TEXT("a hit forbidden to penetrate meets armour and resistance in full"),
			Defender.TakeDamageReading(), 350.0f, 1.0f);

		// THE SAME ATTACKER WITHOUT THE FLAG, which is what makes the reading above
		// a rule rather than a hit that failed to land for some other reason.
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f);
		TestEqual(TEXT("where the same blow without the flag ignores both"),
			Defender.TakeDamageReading(), 1000.0f, 1.0f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmMinionTakesNoPenetrationTest,
	"Cataclysm.DamageType.AMinionDoesNotTakeItsSummonersPenetration")
{
	// The roll is pinned off. See FScopedNoCriticalStrikes: without it this
	// test reads half as much again as it expects, at random.
	const CataclysmDamageTypeTest::FScopedNoCriticalStrikes NoCrits;

	// THE REAL CALL SITE RATHER THAN THE MECHANISM. A summoned minion's damage is
	// dealt in its summoner's name, so everything read off "the attacker" when its
	// blow lands is the player's. The design blocks it: a minion reaches its
	// summoner through exactly three channels -- its side, its base health and
	// damage raised by the summoner's level, and increased damage from one primary
	// attribute -- and penetration is named among what does not cross
	// (docs/Cataclysm_GDD_v2.md:1747). Issue #659.
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);
		Defender.Combat->SetArmor(800.0f);
		Defender.AllResistance->SetAllResistance(30.0f);
		Defender.LastHealth = Defender.Vitals->GetHealth();

		// A DAGGER, SO ALL THREE ROUTES ARE OPEN AT ONCE. Two are attributes on
		// the summoner and the third is what is in its hand: a piercing weapon
		// ignores a further 20% of armour, and the weapon a hit is credited to is
		// read off the effect causer, which for a minion's blow is the summoner.
		CataclysmDamageTypeTest::FScopedArmedAttacker Summoner(World, TEXT("Dagger"));
		if (UAbilitySystemComponent* Offence =
				UCataclysmTargeting::AbilitySystemOf(Summoner.Actor))
		{
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute(), 80.0f);
			Offence->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetPenetrationAttribute(), 30.0f);
		}

		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(
			Summoner.Actor, FVector(200.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f,
			/*bBurns=*/false);
		if (!TestNotNull(TEXT("an imp"), Imp))
		{
			World->DestroyWorld(false);
			return false;
		}

		// TOLD WHO TO HIT RATHER THAN LEFT TO FIND ONE. AttackTarget is the path a
		// controller drives and it is what AttackOnce calls once it has chosen;
		// naming the target skips the hostility search, which is not what this is
		// about.
		Imp->AttackTarget(Defender.Actor);

		// An imp hits for 30% of its summoner's weapon damage, so 300 is swung.
		// Half of it is taken by 800 armour and 30% of the remainder by the
		// resistance: 105 lands. Before this was fixed the imp ignored all of both
		// and dealt the whole 300.
		TestEqual(TEXT("a minion's blow meets armour and resistance in full"),
			Defender.TakeDamageReading(), 105.0f, 1.0f);

		// AND THE SUMMONER'S OWN BLOW STILL PENETRATES, which is what makes the
		// reading above a rule about minions rather than penetration being broken.
		UCataclysmSkillEffects::ApplyHit(Summoner.Actor, Defender.Actor, 100.0f);
		TestEqual(TEXT("while its summoner's own blow ignores both"),
			Defender.TakeDamageReading(), 1000.0f, 1.0f);
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmStunApplicationTest,
	"Cataclysm.DamageType.ChanceToStunPastCertaintyBecomesDuration")
{
	using FCalc = UCataclysmDamageCalculation;

	// THE RULE, set by the project owner for damage over time on 2026-08-03 and
	// extended to stun on 2026-08-16: chance caps at 100% and everything past it
	// multiplies the magnitude. A stun has no damage, so its magnitude is its
	// duration. Mirrors `stun_application` in sim/cataclysm_sim/damage.py.
	float Chance = -1.0f;
	float Seconds = -1.0f;

	FCalc::StunApplication(50.0f, Chance, Seconds);
	TestEqual(TEXT("half a chance is half a chance"), Chance, 50.0f);
	TestEqual(TEXT("and the duration is the base"),
		Seconds, FCalc::IncidentalStunSeconds);

	FCalc::StunApplication(200.0f, Chance, Seconds);
	TestEqual(TEXT("twice certainty still applies once"), Chance, 100.0f);
	TestEqual(TEXT("and lasts twice as long"),
		Seconds, FCalc::IncidentalStunSeconds * 2.0f);

	// THE CAP, AND THE PROPERTY IT EXISTS FOR. A stun as long as the immunity
	// window would hold a target for ever, because the window would expire while
	// it was still held.
	FCalc::StunApplication(400.0f, Chance, Seconds);
	TestEqual(TEXT("four times certainty reaches the cap"),
		Seconds, FCalc::LongestStunSeconds);

	FCalc::StunApplication(100000.0f, Chance, Seconds);
	TestEqual(TEXT("and nothing goes past it"),
		Seconds, FCalc::LongestStunSeconds);
	TestTrue(TEXT("which is short of the immunity window"),
		FCalc::LongestStunSeconds
			< UCataclysmSkillEffects::StunImmunityWindowSeconds);

	return true;
}

CATACLYSM_TEST(FCataclysmBluntWeaponCanStunTest,
	"Cataclysm.DamageType.ABluntWeaponCanStunWhatItHits")
{
	// THE LAST OF THE FOUR SUB-TYPES. Issue #639 left blunt unbuilt because
	// nothing rolled a chance to stun on an ordinary hit; this is that roll.
	//
	// THE CHANCE IS 10%, SO THE ROLL IS NOT WATCHED DIRECTLY. Hitting until it
	// stuns would be a test that fails once in a while for no reason. What is
	// checked instead is that a blunt weapon reaches the stun path at all and a
	// slashing one never does, over enough hits that 10% would have landed
	// many times.
	UWorld* World = CataclysmDamageTypeTest::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	{
		CataclysmDamageTypeTest::FScopedCombatant Defender(World);

		// SMALL ENOUGH THAT EVERY HIT CLEARS THE 10% DAMAGE THRESHOLD. The
		// fixture starts at a million health, which no single hit would dent,
		// and an incidental stun obeys that threshold on purpose.
		Defender.Vitals->SetMaxHealth(1000.0f);
		Defender.Vitals->SetHealth(1000.0f);

		bool bBluntEverStunned = false;
		for (int32 Attempt = 0; Attempt < 200 && !bBluntEverStunned; ++Attempt)
		{
			CataclysmDamageTypeTest::FScopedArmedAttacker Blunt(World, TEXT("Fist"));
			Blunt.Actor->SetAttackDamage(500.0f);

			Defender.Vitals->SetHealth(1000.0f);
			UCataclysmSkillEffects::ApplyHit(Blunt.Actor, Defender.Actor, 100.0f);
			bBluntEverStunned =
				UCataclysmSkillEffects::IsStunned(Defender.Actor);
		}

		TestTrue(TEXT("a blunt weapon stuns within 200 hits at a 10% chance"),
			bBluntEverStunned);
	}

	{
		// AND A SLASHING WEAPON NEVER DOES, which is what makes the above a
		// property of the sub-type rather than of hitting things.
		CataclysmDamageTypeTest::FScopedCombatant Untouched(World);
		Untouched.Vitals->SetMaxHealth(1000.0f);
		Untouched.Vitals->SetHealth(1000.0f);

		bool bSlashingEverStunned = false;
		for (int32 Attempt = 0; Attempt < 200 && !bSlashingEverStunned; ++Attempt)
		{
			CataclysmDamageTypeTest::FScopedArmedAttacker Slashing(World, TEXT("Axe"));
			Slashing.Actor->SetAttackDamage(500.0f);

			Untouched.Vitals->SetHealth(1000.0f);
			UCataclysmSkillEffects::ApplyHit(Slashing.Actor, Untouched.Actor, 100.0f);
			bSlashingEverStunned =
				UCataclysmSkillEffects::IsStunned(Untouched.Actor);
		}

		TestFalse(TEXT("a slashing weapon never stuns in 200 hits"),
			bSlashingEverStunned);
	}

	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_TEST


#endif  // WITH_AUTOMATION_TESTS
