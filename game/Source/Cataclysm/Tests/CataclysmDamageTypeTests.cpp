// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"

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

	static UWorld* MakeWorldThatHasBegunPlay()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game,
										   /*bInformEngineOfWorld=*/false);
		if (World)
		{
			FURL URL;
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		return World;
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

#undef CATACLYSM_TEST

#endif  // WITH_AUTOMATION_TESTS
