// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/**
 * Tests for the damage calculation.
 *
 * These mirror the ones in `sim/tests/test_damage.py`. The two implementations
 * have to agree, and the Python one is where the numbers were argued out, so
 * the same cases are checked here on purpose.
 */

namespace CataclysmDamageTest
{
	struct FScopedDefender
	{
		explicit FScopedDefender(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);

			Vitals = NewVitals;
			Combat = NewCombat;
			Resistances = NewResist;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// A large health pool so the tests measure the calculation rather
			// than running into the floor at zero.
			Vitals->SetMaxHealth(1'000'000.0f);
			Vitals->SetHealth(1'000'000.0f);
		}

		~FScopedDefender()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Resolve a hit with both random rolls pinned to "did not happen". */
		FCataclysmDamageResult Hit(float Damage, int32 Tier = 1) const
		{
			FCataclysmIncomingHit Incoming;
			Incoming.Damage = Damage;
			return UCataclysmDamageCalculation::Resolve(
				Incoming, AbilitySystem, Tier, /*EvasionRoll=*/100.0f, /*BlockRoll=*/100.0f);
		}

		FCataclysmDamageResult Resolve(const FCataclysmIncomingHit& Incoming,
									   int32 Tier = 1,
									   float EvasionRoll = 100.0f,
									   float BlockRoll = 100.0f) const
		{
			return UCataclysmDamageCalculation::Resolve(
				Incoming, AbilitySystem, Tier, EvasionRoll, BlockRoll);
		}

		/**
		 * Deal damage the way the game does: through a gameplay effect that
		 * adds to the Damage meta attribute.
		 *
		 * Calling the calculation directly does NOT exercise the wiring in
		 * UCataclysmVitalAttributeSet::PostGameplayEffectExecute, so a test that
		 * only did that could not tell whether the meta attribute was using the
		 * calculation or ignoring it.
		 */
		void ApplyDamageThroughMetaAttribute(float Amount) const
		{
			UGameplayEffect* Effect = NewObject<UGameplayEffect>(
				GetTransientPackage(), FName(TEXT("DamageEffect")));
			Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

			Effect->Modifiers.SetNum(1);
			FGameplayModifierInfo& Info = Effect->Modifiers[0];
			Info.Attribute = UCataclysmVitalAttributeSet::GetDamageAttribute();
			Info.ModifierOp = EGameplayModOp::Additive;
			Info.ModifierMagnitude = FScalableFloat(Amount);

			AbilitySystem->ApplyGameplayEffectToSelf(
				Effect, 1.0f, AbilitySystem->MakeEffectContext());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
		TObjectPtr<UCataclysmResistanceAttributeSet> Resistances = nullptr;
	};

	static UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// Armor and resistance, which are pure arithmetic
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmArmorCurveTest, "Cataclysm.Damage.ArmorUsesACurveNotASubtraction")
{
	using FCalc = UCataclysmDamageCalculation;

	// Never immunity, however much armor.
	for (const float Armor : { 1'000.0f, 100'000.0f, 10'000'000.0f })
	{
		TestTrue(TEXT("Armor never reaches full immunity"),
			FCalc::ArmorReduction(Armor, 1) <= FCalc::ArmorReductionCap);
	}

	// Diminishing returns: doubling armor must not double the reduction.
	const float First = FCalc::ArmorReduction(500.0f, 1);
	const float Second = FCalc::ArmorReduction(1000.0f, 1);
	TestTrue(TEXT("More armor reduces more"), Second > First);
	TestTrue(TEXT("Doubling armor does not double the reduction"), Second < 2.0f * First);

	// The same armor is worth less at a higher tier, or gear stops mattering.
	TestTrue(TEXT("Armor is worth less at tier 8 than at tier 1"),
		FCalc::ArmorReduction(371.0f, 1) > 4.0f * FCalc::ArmorReduction(371.0f, 8));

	TestEqual(TEXT("No armor reduces nothing"), FCalc::ArmorReduction(0.0f, 1), 0.0f);
	return true;
}

CATACLYSM_TEST(FCataclysmPenetrationOrderTest,
	"Cataclysm.Damage.PenetrationIsAppliedBeforeTheCap")
{
	using FCalc = UCataclysmDamageCalculation;

	// The single most load-bearing rule. A defender at exactly the cap and one
	// over-capped must NOT end up identical after penetration, or every point
	// above 70 is wasted and over-capping means nothing.
	const float AtCap = FCalc::EffectiveResistance(70.0f, 30.0f);
	const float OverCapped = FCalc::EffectiveResistance(100.0f, 30.0f);

	TestEqual(TEXT("At the cap, penetration bites"), AtCap, 40.0f);
	TestEqual(TEXT("Over-capped, penetration is absorbed"), OverCapped, 70.0f);
	TestTrue(TEXT("Over-capping is worth something"), OverCapped > AtCap);

	TestEqual(TEXT("Resistance is capped without penetration"),
		FCalc::EffectiveResistance(200.0f, 0.0f), 70.0f);
	TestEqual(TEXT("Negative resistance is bounded"),
		FCalc::EffectiveResistance(-9999.0f, 0.0f), FCalc::ResistanceFloor);
	return true;
}

CATACLYSM_TEST(FCataclysmPenetrationOvershootTest,
	"Cataclysm.Damage.PenetrationPastATargetsResistanceGrantsNothing")
{
	using FCalc = UCataclysmDamageCalculation;

	// ISSUE #482. The design document forbids over-stacked penetration turning
	// into a damage multiplier. 35% is the Abyssal Warden's resistance, the
	// worked example the document uses and the highest in the vertical slice.
	// Before the fix, 50 penetration gave -15% and 115% of a hit landed.
	for (const float Penetration : { 35.0f, 50.0f, 80.0f, 200.0f })
	{
		TestEqual(TEXT("Penetration stops at zero resistance"),
			FCalc::EffectiveResistance(35.0f, Penetration), 0.0f);
	}

	// Every point up to the target's resistance is worth the same, and every
	// point past it is worth nothing. That is what makes it a defence-stripping
	// stat rather than a scaling one.
	TestEqual(TEXT("A point below the target's resistance still bites"),
		FCalc::EffectiveResistance(35.0f, 20.0f), 15.0f);

	// A natively negative resistance is left where it is. Enchantments inflict
	// that state on purpose; penetration must not manufacture it.
	TestEqual(TEXT("Penetration does not deepen a native negative"),
		FCalc::EffectiveResistance(-25.0f, 60.0f), -25.0f);
	TestEqual(TEXT("A native negative still takes extra damage"),
		FCalc::EffectiveResistance(-25.0f, 0.0f), -25.0f);
	return true;
}

// --------------------------------------------------------------------------
// The order, against a real set of attributes
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmEvasionTest, "Cataclysm.Damage.EvasionAvoidsDirectAttacksOnly")
{
	UWorld* World = CataclysmDamageTest::MakeWorld();
	{
		const CataclysmDamageTest::FScopedDefender D(World);
		D.Combat->SetEvasion(100.0f);

		FCataclysmIncomingHit Direct;
		Direct.Damage = 1000.0f;
		const FCataclysmDamageResult Evaded = D.Resolve(Direct, 1, /*EvasionRoll=*/0.0f);
		TestTrue(TEXT("A direct attack is evaded"), Evaded.bEvaded);
		TestEqual(TEXT("An evaded hit deals nothing"), Evaded.DealtToHealth, 0.0f);

		// Area damage lands regardless. This is why evasion's cap can be soft.
		FCataclysmIncomingHit Area = Direct;
		Area.bIsArea = true;
		const FCataclysmDamageResult Landed = D.Resolve(Area, 1, /*EvasionRoll=*/0.0f);
		TestFalse(TEXT("Area damage is not evaded"), Landed.bEvaded);
		TestEqual(TEXT("Area damage lands in full"), Landed.DealtToHealth, 1000.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmBlockTest, "Cataclysm.Damage.BlockRemovesHalfAndAppliesToArea")
{
	UWorld* World = CataclysmDamageTest::MakeWorld();
	{
		const CataclysmDamageTest::FScopedDefender D(World);
		D.Combat->SetBlockChance(100.0f);

		FCataclysmIncomingHit Incoming;
		Incoming.Damage = 1000.0f;
		const FCataclysmDamageResult Blocked =
			D.Resolve(Incoming, 1, /*EvasionRoll=*/100.0f, /*BlockRoll=*/0.0f);
		TestTrue(TEXT("The hit is blocked"), Blocked.bBlocked);
		TestEqual(TEXT("A block removes exactly half"), Blocked.DealtToHealth, 500.0f);

		// Unlike evasion, block works against area damage.
		Incoming.bIsArea = true;
		const FCataclysmDamageResult Area =
			D.Resolve(Incoming, 1, /*EvasionRoll=*/100.0f, /*BlockRoll=*/0.0f);
		TestEqual(TEXT("Block applies to area damage"), Area.DealtToHealth, 500.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmEnergyShieldTest,
	"Cataclysm.Damage.EnergyShieldAbsorbsBeforeHealthButNotDamageOverTime")
{
	UWorld* World = CataclysmDamageTest::MakeWorld();
	{
		const CataclysmDamageTest::FScopedDefender D(World);
		D.Vitals->SetMaxEnergyShield(400.0f);
		D.Vitals->SetEnergyShield(400.0f);

		const FCataclysmDamageResult Ordinary = D.Hit(1000.0f);
		TestEqual(TEXT("The shield absorbs what it can"), Ordinary.AbsorbedByShield, 400.0f);
		TestEqual(TEXT("Health takes the rest"), Ordinary.DealtToHealth, 600.0f);

		// Damage over time passes straight through. This is what makes energy
		// shield a distinct defence rather than a second health bar, and it is
		// proven by a negative enchantment that removes the immunity.
		FCataclysmIncomingHit Dot;
		Dot.Damage = 1000.0f;
		Dot.bIsDamageOverTime = true;
		const FCataclysmDamageResult OverTime = D.Resolve(Dot);
		TestEqual(TEXT("The shield absorbs no damage over time"),
			OverTime.AbsorbedByShield, 0.0f);
		TestEqual(TEXT("Damage over time reaches health in full"),
			OverTime.DealtToHealth, 1000.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmWeaponSubtypeTest,
	"Cataclysm.Damage.SlashingAndMagicTargetDifferentPools")
{
	UWorld* World = CataclysmDamageTest::MakeWorld();
	{
		const CataclysmDamageTest::FScopedDefender D(World);

		FCataclysmIncomingHit Slashing;
		Slashing.Damage = 1000.0f;
		Slashing.bIsSlashing = true;
		TestEqual(TEXT("Slashing adds ten per cent against health"),
			D.Resolve(Slashing).DealtToHealth, 1100.0f);

		D.Vitals->SetMaxEnergyShield(10'000.0f);
		D.Vitals->SetEnergyShield(10'000.0f);

		FCataclysmIncomingHit Magic;
		Magic.Damage = 1000.0f;
		Magic.bIsMagic = true;
		const FCataclysmDamageResult MagicHit = D.Resolve(Magic);
		TestEqual(TEXT("Magic strips ten per cent more shield"),
			MagicHit.AbsorbedByShield, 1100.0f);

		// And it must not destroy more raw damage than the hit contained.
		TestEqual(TEXT("Magic still deals nothing to health through a full shield"),
			MagicHit.DealtToHealth, 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmNoImmunityTest,
	"Cataclysm.Damage.StackingEveryLayerStillLetsDamageThrough")
{
	UWorld* World = CataclysmDamageTest::MakeWorld();
	{
		const CataclysmDamageTest::FScopedDefender D(World);
		D.Combat->SetArmor(1'000'000.0f);
		D.Resistances->SetDemonicResistance(300.0f);

		FCataclysmIncomingHit Incoming;
		Incoming.Damage = 1'000'000.0f;
		Incoming.DamageType = TEXT("Demonic");

		// IT ASKS AT AND ABOVE 100 FLAT REDUCTION, WHICH IT DID NOT UNTIL ISSUE
		// #644. This test set damage reduction to 90 and passed, one step below
		// the value that broke it: at exactly 100 that layer removed the whole
		// hit and this assertion was false. So the one guard aimed at immunity
		// was passing because of the number it happened to pick rather than
		// because anything stopped the layer.
		const float Reductions[] = { 90.0f, 100.0f, 1'000.0f };
		for (const float Reduction : Reductions)
		{
			D.Combat->SetDamageReduction(Reduction);

			// If any combination reached immunity, a character at the caps would
			// be unkillable and the difficulty system would stop meaning
			// anything.
			TestTrue(*FString::Printf(
				TEXT("damage gets through at %.0f%% flat reduction"), Reduction),
				D.Resolve(Incoming).DealtToHealth > 0.0f);
		}
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmDamageReductionCapTest,
	"Cataclysm.Damage.FlatDamageReductionStopsRisingAtItsCap")
{
	// A HARD CAP, WHICH IS WHAT SEPARATES IT FROM RESISTANCE'S SOFT ONE. Over-
	// capped resistance is worth having because penetration is subtracted before
	// the cap; nothing in the game penetrates this layer, so everything past the
	// cap is worth exactly nothing.
	TestEqual(TEXT("below the cap it is worth what it says"),
		UCataclysmDamageCalculation::EffectiveDamageReduction(20.0f), 20.0f);
	TestEqual(TEXT("at the cap it is worth the cap"),
		UCataclysmDamageCalculation::EffectiveDamageReduction(
			UCataclysmDamageCalculation::DamageReductionCap),
		UCataclysmDamageCalculation::DamageReductionCap);
	TestEqual(TEXT("past the cap it is still worth only the cap"),
		UCataclysmDamageCalculation::EffectiveDamageReduction(99.0f),
		UCataclysmDamageCalculation::DamageReductionCap);
	TestEqual(TEXT("and far past it, the same"),
		UCataclysmDamageCalculation::EffectiveDamageReduction(100'000.0f),
		UCataclysmDamageCalculation::DamageReductionCap);

	// A NEGATIVE IS FLOORED RATHER THAN ALLOWED TO ADD DAMAGE. That is the one
	// place this differs from EffectiveResistance, which floors at -100 because
	// a negative resistance is a real state several enchantments inflict.
	TestEqual(TEXT("a negative is worth nothing rather than extra damage"),
		UCataclysmDamageCalculation::EffectiveDamageReduction(-40.0f), 0.0f);

	// AND THE CAP IS THE ARMOUR CAP, on purpose, so the design has one figure
	// for the most a single unconditional mitigation layer may remove.
	TestEqual(TEXT("the cap is the same figure as the armour cap"),
		UCataclysmDamageCalculation::DamageReductionCap,
		UCataclysmDamageCalculation::ArmorReductionCap);

	return true;
}

// --------------------------------------------------------------------------
// The multiplicative damage reduction bucket. Issue #665.
//
// WHY THERE ARE TWO BUCKETS. Twelve passive tree nodes -- eleven in the Bulwark
// tree and one in the Saboteur tree -- grant damage reduction and call it
// "(multiplicative)". The project owner confirmed on 2026-08-17 that the word
// means "more", which is what Path of Exile and Last Epoch both call it, so each
// such source removes a share of what the ones before it left rather than adding
// into the pool the 75% cap bounds.
//
// NOTHING GRANTS ONE YET. No code in game/Source loads a passive tree, so every
// character sits at zero and no existing behaviour changes. What is built and
// tested here is the arithmetic that consumes it.
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmMoreDamageReductionDoesNotAddTest,
	"Cataclysm.Damage.TwoMultiplicativeReductionsDoNotAddTogether")
{
	using FCalc = UCataclysmDamageCalculation;

	// THE ENTIRE DIFFERENCE BETWEEN THE TWO BUCKETS, and the reason the second
	// one exists. Twenty and twenty remove 36%, because the second removes a
	// fifth of the 80% the first left.
	TestEqual(TEXT("two sources of 20% remove 36% and not 40%"),
		FCalc::CombinedMoreDamageReduction({ 20.0f, 20.0f }), 36.0f, 0.001f);

	TestEqual(TEXT("one source is worth what it says"),
		FCalc::CombinedMoreDamageReduction({ 20.0f }), 20.0f, 0.001f);

	TestEqual(TEXT("no sources remove nothing"),
		FCalc::CombinedMoreDamageReduction({}), 0.0f, 0.001f);

	// THE 75% CAP DOES NOT REACH THIS BUCKET, by the owner's ruling. Four
	// sources of 30% pass it without any source doing so.
	TestTrue(TEXT("four sources of 30% pass the additive pool's cap"),
		FCalc::CombinedMoreDamageReduction({ 30.0f, 30.0f, 30.0f, 30.0f })
			> FCalc::DamageReductionCap);

	// A NEGATIVE IS FLOORED, the same way the additive pool's is: nothing in the
	// design grants a source that increases damage taken.
	TestEqual(TEXT("a negative source removes nothing rather than adding damage"),
		FCalc::CombinedMoreDamageReduction({ -40.0f }), 0.0f, 0.001f);

	return true;
}

CATACLYSM_TEST(FCataclysmMoreDamageReductionCannotReachImmunityTest,
	"Cataclysm.Damage.StackingMultiplicativeReductionNeverReachesImmunity")
{
	using FCalc = UCataclysmDamageCalculation;

	// THIS IS WHAT STANDS IN FOR A CAP. The design says "No combination of these
	// layers reaches immunity. Each has either a cap or a curve that cannot reach
	// zero damage." This bucket is the curve kind, so the property that has to
	// hold is that the product never reaches one.
	// FORTY SOURCES OF 50% IS THE CASE THAT CAUGHT A REAL DEFECT. Exact
	// arithmetic leaves 9.1e-13 of the damage, but a float carries about seven
	// decimal digits, so `100 * (1 - 9.1e-13)` rounded to exactly 100.0f and this
	// assertion failed the first time it was run. Multiplicative stacking cannot
	// reach immunity in mathematics and could reach it here, so the combination
	// is bounded as well as each source.
	TArray<float> Many;
	for (int32 Index = 0; Index < 40; ++Index)
	{
		Many.Add(50.0f);
	}
	TestTrue(TEXT("forty sources of 50% still leave something"),
		FCalc::CombinedMoreDamageReduction(Many) < 100.0f);

	// AND THE ARITHMETIC THAT MATTERS, ASKED DIRECTLY: a hit still lands.
	TestTrue(TEXT("and a hit against them still deals damage"),
		1'000'000.0f
			* (1.0f - FCalc::CombinedMoreDamageReduction(Many) / 100.0f) > 0.0f);

	// A SINGLE SOURCE OF 100 WOULD BE EXACT IMMUNITY, which stacking cannot
	// prevent on its own, so one source is separately bounded. That is the
	// failure the 75% cap was added for under issue #644.
	TestEqual(TEXT("one source of 100% is held below immunity"),
		FCalc::CombinedMoreDamageReduction({ 100.0f }),
		FCalc::MoreDamageReductionCap, 0.001f);

	TestTrue(TEXT("and that bound is not the additive pool's cap"),
		FCalc::MoreDamageReductionCap > FCalc::DamageReductionCap);

	return true;
}

CATACLYSM_TEST(FCataclysmBothReductionBucketsApplyTest,
	"Cataclysm.Damage.BothDamageReductionBucketsApplyOneAfterTheOther")
{
	UWorld* World = CataclysmDamageTest::MakeWorld();
	{
		const CataclysmDamageTest::FScopedDefender D(World);

		FCataclysmIncomingHit Incoming;
		Incoming.Damage = 1'000.0f;

		// The pool at its cap and one multiplicative source of 20%. If the two
		// were summed this would read 50; if the multiplicative source joined the
		// pool before capping it would read 250.
		D.Combat->SetDamageReduction(75.0f);
		D.Combat->SetDamageReductionMore(20.0f);

		TestEqual(TEXT("75% then 20% leaves a fifth of the hit"),
			D.Resolve(Incoming).DealtToHealth, 200.0f, 0.1f);

		// AND A CHARACTER WITH NEITHER IS UNCHANGED, which is every character in
		// the game today, because nothing grants either from a passive tree yet.
		D.Combat->SetDamageReduction(0.0f);
		D.Combat->SetDamageReductionMore(0.0f);
		TestEqual(TEXT("a character with neither takes the whole hit"),
			D.Resolve(Incoming).DealtToHealth, 1'000.0f, 0.1f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmDamageReductionCapAppliesToAHitTest,
	"Cataclysm.Damage.TheFlatReductionCapReachesARealHit")
{
	// The check above is arithmetic on a static function. This one proves the
	// mitigation order actually calls it, which is the half that a cap declared
	// and never applied would still pass.
	UWorld* World = CataclysmDamageTest::MakeWorld();
	{
		const CataclysmDamageTest::FScopedDefender D(World);

		FCataclysmIncomingHit Incoming;
		Incoming.Damage = 1'000.0f;

		D.Combat->SetDamageReduction(35.95f);
		TestEqual(TEXT("the most gear and a class can reach still applies in full"),
			D.Resolve(Incoming).DealtToHealth, 640.5f, 0.01f);

		D.Combat->SetDamageReduction(
			UCataclysmDamageCalculation::DamageReductionCap);
		const float AtCap = D.Resolve(Incoming).DealtToHealth;
		TestEqual(TEXT("at the cap a quarter of the hit lands"), AtCap, 250.0f,
			0.01f);

		D.Combat->SetDamageReduction(100.0f);
		TestEqual(TEXT("and at 100, which used to be immunity, the same quarter"),
			D.Resolve(Incoming).DealtToHealth, AtCap, 0.01f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmDamageAppliesThroughTheMetaAttributeTest,
	"Cataclysm.Damage.TheMetaAttributeUsesTheCalculation")
{
	UWorld* World = CataclysmDamageTest::MakeWorld();
	{
		const CataclysmDamageTest::FScopedDefender D(World);
		D.Vitals->SetMaxHealth(1000.0f);
		D.Vitals->SetHealth(1000.0f);
		D.Vitals->SetMaxEnergyShield(300.0f);
		D.Vitals->SetEnergyShield(300.0f);
		D.Combat->SetArmor(800.0f);

		// Dealt through a real gameplay effect, not by calling the calculation
		// directly, so this covers the wiring in PostGameplayEffectExecute.
		//
		// Armor at 800 against K of 800 removes exactly half, so 400 becomes
		// 200. The shield then absorbs all 200 and health is untouched. Before
		// this calculation existed, damage went straight to health and both
		// armor and the shield were ignored, so health would have dropped to
		// 600 and the shield would still be full.
		D.ApplyDamageThroughMetaAttribute(400.0f);

		TestEqual(TEXT("Health is untouched"), D.Vitals->GetHealth(), 1000.0f);
		TestEqual(TEXT("The shield absorbed the half that armor left"),
			D.Vitals->GetEnergyShield(), 100.0f);
		TestEqual(TEXT("The damage meta attribute is consumed"),
			D.Vitals->GetDamage(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_AUTOMATION_TESTS
