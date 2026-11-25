// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * IRONHIDE AND OVERWHELM. The Ravager tree rework of 2026-11-24.
 *
 * `Ravager_keystone_spine_001` Ironhide now reads "Your Armor cannot be ignored, and Overwhelm takes only half as much
 * from it." Its second row is `armor_overwhelm_halved`, a flag read on the defender at the armour step of
 * `UCataclysmDamageCalculation::Resolve`. THE PROJECT OWNER'S RULING OF 2026-11-24: Ironhide is an exception to
 * "Overwhelm strips every kind of mitigation", and only halves what Overwhelm takes from armour. Resistance, block and
 * evasion stay fully overwhelmed, so the keystone cannot be used to sidestep the rule.
 *
 * THE FIGURES ARE `CataclysmOverwhelmTests.cpp`'S OWN, OW5: 800 armour at tier 1 removes 50%, so a 1,000 blow lands
 * for 500; less 10 points it removes 40% and lands for 600; less 60 it removes nothing and lands for 1,000. With the
 * flag the same points take half: 5 points leave 45% (550), 30 points leave 20% (800).
 *
 * THE FLAG IS GIVEN AS A STAT LINE, the way `UCataclysmPlayerClassStats::ApplyTo` leaves a passive row, so the read
 * under test is the one play takes. Every other stat falls back to its attribute.
 *
 * WRITTEN WITHOUT A BUILD, from the source, on 2026-11-24.
 */
namespace CataclysmIronhideOverwhelmTest
{
	/** A bare defender with the three sets a hit reads, its health large enough that no blow reaches the floor. */
	struct FIronhideDefender
	{
		explicit FIronhideDefender(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			Abilities = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			Abilities->RegisterComponent();
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResistances = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			Abilities->AddAttributeSetSubobject(NewVitals);
			Abilities->AddAttributeSetSubobject(NewCombat);
			Abilities->AddAttributeSetSubobject(NewResistances);
			Abilities->InitAbilityActorInfo(Actor, Actor);
			Combat = NewCombat;
			Resistances = NewResistances;
			NewVitals->SetMaxHealth(10000.0f);
			NewVitals->SetHealth(10000.0f);
		}

		~FIronhideDefender()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Ironhide's second row, a flat one, or nothing at all. */
		void HoldIronhide(bool bHeld) const
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			if (bHeld)
			{
				FCataclysmStatModifier Flag;
				Flag.Bucket = ECataclysmStatBucket::Flat;
				Flag.Source = ECataclysmModifierSource::PassiveKeystone;
				Flag.Value = 1.0f;
				FCataclysmStatInputs& Line =
					Inputs.FindOrAdd(FName(UCataclysmDamageCalculation::ArmorOverwhelmHalvedStat));
				Line.Base = 0.0f;
				Line.Modifiers = {Flag};
			}
			Abilities->SetStatInputs(MoveTemp(Inputs));
		}

		/** A 1,000 Demonic blow carrying `Overwhelm` points at tier 1, with both rolls pinned. */
		float Landed(float Overwhelm, float EvasionRoll = 100.0f, float BlockRoll = 100.0f) const
		{
			FCataclysmIncomingHit Hit;
			Hit.Damage = 1000.0f;
			Hit.DamageType = FName(TEXT("Demonic"));
			Hit.OverwhelmPoints = Overwhelm;
			return UCataclysmDamageCalculation::Resolve(Hit, Abilities, /*Tier=*/1, EvasionRoll, BlockRoll)
				.DealtToHealth;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* Abilities = nullptr;
		UCataclysmCombatAttributeSet* Combat = nullptr;
		UCataclysmResistanceAttributeSet* Resistances = nullptr;
	};
}

#define CATACLYSM_IRONHIDE_OVERWHELM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// IO1. Overwhelm takes half as many points off a holder's armour.
// --------------------------------------------------------------------------------------------------------------------
/** Prints "IRONHIDE ARMOUR". */
CATACLYSM_IRONHIDE_OVERWHELM_TEST(FCataclysmIronhideHalvesOverwhelmOnArmourTest,
	"Cataclysm.IronhideOverwhelm.OverwhelmTakesHalfAsManyPointsOffAHoldersArmour")
{
	using namespace CataclysmIronhideOverwhelmTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FIronhideDefender D(World);
	D.Combat->SetArmor(800.0f);

	D.HoldIronhide(false);
	const float PlainNone = D.Landed(0.0f);
	const float PlainTen = D.Landed(10.0f);
	const float PlainSixty = D.Landed(60.0f);
	D.HoldIronhide(true);
	const float HeldNone = D.Landed(0.0f);
	const float HeldTen = D.Landed(10.0f);
	const float HeldSixty = D.Landed(60.0f);
	AddInfo(FString::Printf(TEXT("IRONHIDE ARMOUR: without %.2f / %.2f (10) / %.2f (60); with %.2f / %.2f (10) / %.2f (60)"),
							PlainNone, PlainTen, PlainSixty, HeldNone, HeldTen, HeldSixty));

	TestEqual(TEXT("set-up: 800 armour at tier 1 removes half"), UCataclysmDamageCalculation::ArmorReduction(800.0f, 1),
			  50.0f, 0.001f);
	TestEqual(TEXT("CONTROL: with no Overwhelm the blow lands for 500 without the flag"), PlainNone, 500.0f, 0.01f);
	TestEqual(TEXT("CONTROL: and for 500 with it, so the flag is not armour"), HeldNone, 500.0f, 0.01f);
	TestEqual(TEXT("CONTROL: 10 points take armour's 50% to 40% without the flag, 600"), PlainTen, 600.0f, 0.01f);
	TestEqual(TEXT("**with the flag the same 10 take half as many points, 45%, so 550**"), HeldTen, 550.0f, 0.01f);
	TestEqual(TEXT("CONTROL: 60 points take all of it without the flag, 1,000"), PlainSixty, 1000.0f, 0.01f);
	TestEqual(TEXT("**with the flag 60 take 30, leaving 20%, so 800**"), HeldSixty, 800.0f, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IO2. Resistance, block and evasion stay fully overwhelmed for a holder.
// --------------------------------------------------------------------------------------------------------------------
/**
 * THE OWNER'S RULING HAS TWO HALVES, AND THIS IS THE ONE A CARELESS BUILD WOULD BREAK: halving the points the blow
 * carries, rather than what one step takes, would halve all four. OW4 and OW5's own figures: 50 resistance with 20
 * Overwhelm lands 700; 30 evasion against a roll of 25 less 10 points is 20 and does not evade; 30 block against 25
 * less 10 does not block. Each is asserted with the flag held. Prints "IRONHIDE OTHERS".
 */
CATACLYSM_IRONHIDE_OVERWHELM_TEST(FCataclysmIronhideLeavesTheOtherStepsOverwhelmedTest,
	"Cataclysm.IronhideOverwhelm.ResistanceBlockAndEvasionStayFullyOverwhelmedForAHolder")
{
	using namespace CataclysmIronhideOverwhelmTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FIronhideDefender D(World);
	D.HoldIronhide(true);

	D.Resistances->SetDemonicResistance(50.0f);
	const float Resisted = D.Landed(20.0f);
	D.Resistances->SetDemonicResistance(0.0f);

	D.Combat->SetEvasion(30.0f);
	const float EvadedWithout = D.Landed(0.0f, /*EvasionRoll=*/25.0f);
	const float EvadedWithTen = D.Landed(10.0f, /*EvasionRoll=*/25.0f);
	D.Combat->SetEvasion(0.0f);

	D.Combat->SetBlockChance(30.0f);
	const float BlockedWithout = D.Landed(0.0f, 100.0f, /*BlockRoll=*/25.0f);
	const float BlockedWithTen = D.Landed(10.0f, 100.0f, /*BlockRoll=*/25.0f);
	D.Combat->SetBlockChance(0.0f);
	AddInfo(FString::Printf(TEXT("IRONHIDE OTHERS: resisted %.2f; evasion %.2f then %.2f; block %.2f then %.2f"), Resisted,
							EvadedWithout, EvadedWithTen, BlockedWithout, BlockedWithTen));

	TestEqual(TEXT("**at 50 resistance a holder still meets all 20 points: 700**"), Resisted, 700.0f, 0.01f);
	TestEqual(TEXT("CONTROL: 30 evasion against 25 evades"), EvadedWithout, 0.0f);
	TestEqual(TEXT("**less all 10 points it does not, for a holder too**"), EvadedWithTen, 1000.0f, 0.01f);
	TestEqual(TEXT("CONTROL: 30 block against 25 blocks half"), BlockedWithout, 500.0f, 0.01f);
	TestEqual(TEXT("**less all 10 points it does not block, for a holder too**"), BlockedWithTen, 1000.0f, 0.01f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// IO3. The stat is one the engine records with no attribute.
// --------------------------------------------------------------------------------------------------------------------
/** A name off the exemption list would be dropped by `ApplyTo` in silence, and the row would grant nothing. */
CATACLYSM_IRONHIDE_OVERWHELM_TEST(FCataclysmIronhideStatIsRecordedTest,
	"Cataclysm.IronhideOverwhelm.TheStatIsOneTheEngineRecordsWithNoAttribute")
{
	TestTrue(TEXT("armor_overwhelm_halved is on the exemption list"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(UCataclysmDamageCalculation::ArmorOverwhelmHalvedStat)));
	TestEqual(TEXT("and is spelled as the row spells it"),
			  FString(UCataclysmDamageCalculation::ArmorOverwhelmHalvedStat), FString(TEXT("armor_overwhelm_halved")));
	return true;
}

#undef CATACLYSM_IRONHIDE_OVERWHELM_TEST

#endif // WITH_AUTOMATION_TESTS
