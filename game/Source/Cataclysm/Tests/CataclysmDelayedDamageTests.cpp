// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDelayedDamage.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * A share of the damage that would reach a character's health, taken later instead of now. Ruled 2026-10-12 under
 * the owner's delegation, for "25%-50% of damage taken is dealt over 4-8 seconds". No row is authored, so the
 * wearer's stat line is written by hand as a gear refresh would write it.
 *
 * WHERE THESE STAND. Beside `CataclysmHealthDebtTests.cpp` and `CataclysmDamageConversionTests.cpp`, the tests of
 * the two mechanisms this one was read against, and on their rig: a plain actor holding an ability system and its
 * attribute sets. A plain actor has no root component and so no body, and every one stands at the world's origin
 * without overlapping another. One test needs a real death and spawns a real player character, at the origin,
 * struck by a plain actor.
 *
 * EVERY FIGURE IS A DIFFERENCE, AND EACH TEST READS A CONTROL FIRST: what the same blow takes from a twin that
 * wears no row. Nothing here states what a blow of 400 is worth; it is read.
 *
 * NO ROLL IS MADE. Every blow is sent as direct damage that cannot be evaded, cannot critically strike and cannot
 * be retaliated against, and the critical strike roll is pinned as well.
 *
 * THE CLOCK IS THE WORLD'S, MOVED BY HAND. A world built for a test is never ticked, so `World->TimeSeconds` stays
 * where it is until a test moves it, and the drain is called as `ACataclysmCharacterBase::RegenerationStep` calls
 * it. Nothing waits on a tick. That the step calls it is checked by
 * `tools/tests/test_hooks_no_headless_test_can_drive_still_call_their_jobs.py`.
 */
namespace CataclysmDelayedDamageTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;
	using FDelayed = UCataclysmDelayedDamage;
	using FSystem = UCataclysmAbilitySystemComponent;

	/** The health every plain actor here starts with. */
	constexpr float HealthPool = 10000.0f;

	/** What every blow here is sent as, before the struck character's mitigation. */
	constexpr float BlowDamage = 400.0f;

	/** How near two figures must be to be read as equal. */
	constexpr float Tolerance = 0.01f;

	/**
	 * Write the row's two stats on a stat line, as a gear refresh would. A figure of nought leaves that stat off.
	 *
	 * IT REPLACES THE WHOLE STAT LINE, as `SetStatInputs` does, so calling it with two noughts takes the row off.
	 */
	void WearTheRow(FSystem* Wearer, float SharePercent, float Seconds)
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		const auto Add = [&Inputs](const TCHAR* Stat, float Value)
		{
			if (Value <= 0.0f)
			{
				return;
			}
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::Enchantment;
			Flat.Value = Value;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		};
		Add(FDelayed::SharePercentStat, SharePercent);
		Add(FDelayed::SecondsStat, Seconds);
		Wearer->SetStatInputs(MoveTemp(Inputs));
	}

	/** A plain actor that can be struck and can strike: an ability system and the four sets a blow reads. */
	struct FScopedTaker
	{
		explicit FScopedTaker(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper
			// rather than the set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist = NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), HealthPool);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), HealthPool);
		}

		~FScopedTaker()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Health() const { return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute()); }
		float Shield() const { return AbilitySystem->GetNumericAttribute(Vital::GetEnergyShieldAttribute()); }
		float Owed() const { return AbilitySystem->DelayedDamageOwed(); }
		void SetHealth(float Value) const { AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), Value); }
		void Wear(float SharePercent, float Seconds) const { WearTheRow(AbilitySystem, SharePercent, Seconds); }

		/** An energy shield of this size, full. */
		void GiveShield(float Size) const
		{
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxEnergyShieldAttribute(), Size);
			AbilitySystem->SetNumericAttributeBase(Vital::GetEnergyShieldAttribute(), Size);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** The health of any actor with a vital set. */
	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : 0.0f;
	}

	/**
	 * One blow from `Attacker` on `Target`, on which no roll is made. A tick when `bTick`.
	 *
	 * @return the health `Target` lost to it at once
	 */
	float Strike(const FScopedTaker& Attacker, AActor* Target, float Damage, bool bTick = false,
				 FCataclysmDamageResult* OutResolved = nullptr)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bIsDamageOverTime = bTick;
		Delivery.bCannotBeEvaded = true;
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bCannotBeRetaliatedAgainst = true;
		const float Before = HealthOf(Target);
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Target, Damage, Delivery, OutResolved);
		return Before - HealthOf(Target);
	}

	/**
	 * A real player character at the origin, possessed, on a player state, as the health debt's lethal test makes
	 * one. Null when any of the three could not be spawned.
	 */
	ACataclysmPlayerCharacter* SpawnPossessedPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Player =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Player)
		{
			return nullptr;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Player);
		return Player;
	}

	/** Pins the block roll at 100, which never blocks, for as long as it is in scope. */
	struct FScopedNeverBlocks
	{
		FScopedNeverBlocks()
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.BlockRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(100.0f, ECVF_SetByConsole);
			}
		}

		~FScopedNeverBlocks()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** Move the world's clock on and drain once, as the regeneration step does. @return what the drain took */
	float StepOn(UWorld* World, AActor* Character, float Seconds)
	{
		World->TimeSeconds += Seconds;
		return FDelayed::DrainStep(Character);
	}
}

// ---------------------------------------------------------------------------
// The arithmetic
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageArithmeticTest,
	"Cataclysm.DelayedDamage.TheShareTakenAndWhatAParcelOwesByATimeAreArithmetic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmDelayedDamageArithmeticTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	// THE SHARE TAKEN OFF A BLOW.
	TestEqual(TEXT("25% of 400 is 100"), FDelayed::AmountDelayed(400.0f, 25.0f, 4.0f), 100.0f, Tolerance);
	TestEqual(TEXT("50% of 400 is 200"), FDelayed::AmountDelayed(400.0f, 50.0f, 8.0f), 200.0f, Tolerance);
	TestEqual(TEXT("a share of nought takes nothing"), FDelayed::AmountDelayed(400.0f, 0.0f, 4.0f), 0.0f, 0.0f);
	TestEqual(TEXT("seconds of nought take nothing"), FDelayed::AmountDelayed(400.0f, 50.0f, 0.0f), 0.0f, 0.0f);
	TestEqual(TEXT("a blow of nothing gives nothing"), FDelayed::AmountDelayed(0.0f, 50.0f, 4.0f), 0.0f, 0.0f);
	TestEqual(TEXT("a negative share takes nothing"), FDelayed::AmountDelayed(400.0f, -10.0f, 4.0f), 0.0f, 0.0f);
	TestEqual(TEXT("a share above 100 is held to the whole blow"), FDelayed::AmountDelayed(400.0f, 250.0f, 4.0f),
			  400.0f, Tolerance);

	// WHAT A PARCEL OWES BY A TIME: evenly, nothing before it is taken and all of it from its seconds on.
	TestEqual(TEXT("at the moment it is taken a parcel owes nothing"), FSystem::DelayedDamageOwedBy(200.0f, 0.0f, 4.0f),
			  0.0f, 0.0f);
	TestEqual(TEXT("a quarter of the way through it owes a quarter"), FSystem::DelayedDamageOwedBy(200.0f, 1.0f, 4.0f),
			  50.0f, Tolerance);
	TestEqual(TEXT("half way through it owes half"), FSystem::DelayedDamageOwedBy(200.0f, 2.0f, 4.0f), 100.0f, Tolerance);
	TestEqual(TEXT("at its seconds it owes exactly the whole"), FSystem::DelayedDamageOwedBy(200.0f, 4.0f, 4.0f),
			  200.0f, 0.0f);
	TestEqual(TEXT("and after them the whole and no more"), FSystem::DelayedDamageOwedBy(200.0f, 40.0f, 4.0f),
			  200.0f, 0.0f);
	TestEqual(TEXT("a time before it was taken owes nothing"), FSystem::DelayedDamageOwedBy(200.0f, -1.0f, 4.0f),
			  0.0f, 0.0f);
	TestEqual(TEXT("seconds of nought owe the whole at once"), FSystem::DelayedDamageOwedBy(200.0f, 0.0f, 0.0f),
			  200.0f, 0.0f);
	TestEqual(TEXT("the same parcel over 8 seconds owes a quarter at 2"), FSystem::DelayedDamageOwedBy(200.0f, 2.0f, 8.0f),
			  50.0f, Tolerance);
	return true;
}

// ---------------------------------------------------------------------------
// Taking the share
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageShareTakenTest,
	"Cataclysm.DelayedDamage.AShareOfABlowIsTakenOffWhatReachesHealthAndHeldAndTheRestLandsAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: nowhere. Every character is a plain actor with no root component, so none has a body to overlap
 * another.
 *
 * One attacker strikes six takers once each with the same blow: one wearing no row (the control), one holding the
 * share and no seconds, one holding the seconds and no share, one at 50% over 8 seconds, one at 25% over 4, and one
 * at 250% over 4.
 */
bool FCataclysmDelayedDamageShareTakenTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedTaker Attacker(World);
	FScopedTaker Plain(World);
	FScopedTaker ShareOnly(World);
	FScopedTaker SecondsOnly(World);
	FScopedTaker High(World);
	FScopedTaker Low(World);
	FScopedTaker Over(World);
	ShareOnly.Wear(50.0f, 0.0f);
	SecondsOnly.Wear(0.0f, 8.0f);
	High.Wear(50.0f, 8.0f);
	Low.Wear(25.0f, 4.0f);
	Over.Wear(250.0f, 4.0f);

	// THE CONTROL, READ FIRST: what the blow takes from a character wearing no row, and that it holds nothing.
	const float Whole = Strike(Attacker, Plain.Actor, BlowDamage);
	if (!TestTrue(*FString::Printf(TEXT("control: the blow takes health from a character wearing no row: %.2f"), Whole),
				  Whole > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: and that character owes nothing"), Plain.Owed(), 0.0f, 0.0f);

	// EITHER STAT WITHOUT THE OTHER DELAYS NOTHING.
	TestEqual(TEXT("a character holding the share and no seconds takes the whole blow at once"),
			  Strike(Attacker, ShareOnly.Actor, BlowDamage), Whole, Tolerance);
	TestEqual(TEXT("and owes nothing"), ShareOnly.Owed(), 0.0f, 0.0f);
	TestEqual(TEXT("a character holding the seconds and no share takes the whole blow at once"),
			  Strike(Attacker, SecondsOnly.Actor, BlowDamage), Whole, Tolerance);
	TestEqual(TEXT("and owes nothing"), SecondsOnly.Owed(), 0.0f, 0.0f);

	// 50% OVER 8 SECONDS: half lands at once and half is held.
	FCataclysmDamageResult Resolved;
	const float HighAtOnce = Strike(Attacker, High.Actor, BlowDamage, /*bTick=*/false, &Resolved);
	TestEqual(TEXT("at 50% half of the blow lands at once"), HighAtOnce, Whole * 0.5f, Tolerance);
	TestEqual(TEXT("and the other half is held"), High.Owed(), Whole * 0.5f, Tolerance);
	TestEqual(TEXT("so what landed and what is held are the whole blow"), HighAtOnce + High.Owed(), Whole, Tolerance);
	TestEqual(TEXT("the blow's own result still holds the whole blow as dealt to health: the share is taken after "
				   "the blow is resolved"), Resolved.DealtToHealth, Whole, Tolerance);

	// 25% OVER 4 SECONDS: three quarters land at once and a quarter is held.
	const float LowAtOnce = Strike(Attacker, Low.Actor, BlowDamage);
	TestEqual(TEXT("at 25% three quarters of the blow land at once"), LowAtOnce, Whole * 0.75f, Tolerance);
	TestEqual(TEXT("and a quarter is held"), Low.Owed(), Whole * 0.25f, Tolerance);

	// A SHARE ABOVE 100 IS HELD TO THE WHOLE BLOW: nothing lands at once and no more than the blow is held.
	TestEqual(TEXT("at 250% nothing lands at once"), Strike(Attacker, Over.Actor, BlowDamage), 0.0f, Tolerance);
	TestEqual(TEXT("and the whole blow is held, and no more"), Over.Owed(), Whole, Tolerance);

	// A SECOND BLOW ADDS TO WHAT IS HELD.
	Strike(Attacker, High.Actor, BlowDamage);
	TestEqual(TEXT("a second blow adds its half to what is held"), High.Owed(), Whole, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageTickTest,
	"Cataclysm.DelayedDamage.ATickOfDamageOverTimeIsNotDelayed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** STANDING: nowhere, as above. Two wearers at 50% over 8 seconds and one character wearing no row. */
bool FCataclysmDelayedDamageTickTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedTaker Attacker(World);
	FScopedTaker Plain(World);
	FScopedTaker HitWearer(World);
	FScopedTaker TickWearer(World);
	HitWearer.Wear(50.0f, 8.0f);
	TickWearer.Wear(50.0f, 8.0f);

	// THE CONTROLS, READ FIRST: what a tick takes from a character wearing no row, and that a hit on a wearer IS
	// delayed.
	const float WholeTick = Strike(Attacker, Plain.Actor, BlowDamage, /*bTick=*/true);
	if (!TestTrue(*FString::Printf(TEXT("control: a tick takes health from a character wearing no row: %.2f"), WholeTick),
				  WholeTick > 0.0f))
	{
		return false;
	}
	Strike(Attacker, HitWearer.Actor, BlowDamage);
	if (!TestTrue(TEXT("control: a hit on a wearer leaves it owing something"), HitWearer.Owed() > 0.0f))
	{
		return false;
	}

	TestEqual(TEXT("a tick on a wearer takes at once what it takes from a character wearing no row"),
			  Strike(Attacker, TickWearer.Actor, BlowDamage, /*bTick=*/true), WholeTick, Tolerance);
	TestEqual(TEXT("and the wearer owes nothing for it"), TickWearer.Owed(), 0.0f, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamagePoolsTest,
	"Cataclysm.DelayedDamage.WhatTheEnergyShieldAndTheTemporaryAbsorbTookIsNotDelayed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: nowhere, as above.
 *
 * Two pairs. In each, a character wearing no row and a wearer at 50% over 8 seconds hold the same pool, an energy
 * shield of 100 in the first pair and a temporary absorb of 150 in the second, and take the same blow.
 *
 * MANA IS NOT READ HERE. Mana takes damage only from a tick, on a character holding
 * `damage_over_time_taken_from_mana_first`, and a tick is never delayed; the test of a tick stands for it.
 */
bool FCataclysmDelayedDamagePoolsTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedTaker Attacker(World);

	// THE ENERGY SHIELD.
	{
		FScopedTaker Plain(World);
		FScopedTaker Wearer(World);
		Plain.GiveShield(100.0f);
		Wearer.GiveShield(100.0f);
		Wearer.Wear(50.0f, 8.0f);

		const float PlainHealth = Strike(Attacker, Plain.Actor, BlowDamage);
		const float PlainShield = 100.0f - Plain.Shield();
		if (!TestTrue(*FString::Printf(TEXT("control: with no row the shield takes part of the blow, %.2f, and health "
											"the rest, %.2f"), PlainShield, PlainHealth),
					  PlainShield > 0.0f && PlainHealth > 0.0f))
		{
			return false;
		}

		const float WearerHealth = Strike(Attacker, Wearer.Actor, BlowDamage);
		TestEqual(TEXT("the wearer's shield takes what the shield of the character with no row took"),
				  100.0f - Wearer.Shield(), PlainShield, Tolerance);
		TestEqual(TEXT("half of what was left for health lands at once"), WearerHealth, PlainHealth * 0.5f, Tolerance);
		TestEqual(TEXT("and half of what was left for health is held: none of what the shield took is delayed"),
				  Wearer.Owed(), PlainHealth * 0.5f, Tolerance);
	}

	// THE TEMPORARY ABSORB.
	{
		FScopedTaker Plain(World);
		FScopedTaker Wearer(World);
		Plain.AbilitySystem->GrantTemporaryAbsorb(150.0f);
		Wearer.AbilitySystem->GrantTemporaryAbsorb(150.0f);
		Wearer.Wear(50.0f, 8.0f);

		const float PlainHealth = Strike(Attacker, Plain.Actor, BlowDamage);
		const float PlainAbsorb = 150.0f - Plain.AbilitySystem->TemporaryAbsorbHeld();
		if (!TestTrue(*FString::Printf(TEXT("control: with no row the temporary absorb takes part of the blow, %.2f, "
											"and health the rest, %.2f"), PlainAbsorb, PlainHealth),
					  PlainAbsorb > 0.0f && PlainHealth > 0.0f))
		{
			return false;
		}

		const float WearerHealth = Strike(Attacker, Wearer.Actor, BlowDamage);
		TestEqual(TEXT("the wearer's temporary absorb takes what the other's took"),
				  150.0f - Wearer.AbilitySystem->TemporaryAbsorbHeld(), PlainAbsorb, Tolerance);
		TestEqual(TEXT("half of what was left for health lands at once"), WearerHealth, PlainHealth * 0.5f, Tolerance);
		TestEqual(TEXT("and half of what was left for health is held: none of what the absorb took is delayed"),
				  Wearer.Owed(), PlainHealth * 0.5f, Tolerance);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Draining the store
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageDrainsEvenlyTest,
	"Cataclysm.DelayedDamage.TheStoreDrainsEvenlyByTheWorldsClockAndIsEmptyAtTheRowsSeconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: nowhere, as above.
 *
 * A wearer at 50% over 4 seconds takes one blow and is drained sixteen times, a quarter of a second apart, which
 * is the regeneration step. A second wearer takes the same blow and is drained late: once after one second and
 * once after ten more.
 */
bool FCataclysmDelayedDamageDrainsEvenlyTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedTaker Attacker(World);
	FScopedTaker Plain(World);
	FScopedTaker Wearer(World);
	FScopedTaker Late(World);
	Wearer.Wear(50.0f, 4.0f);
	Late.Wear(50.0f, 4.0f);

	const float Whole = Strike(Attacker, Plain.Actor, BlowDamage);
	Strike(Attacker, Wearer.Actor, BlowDamage);
	Strike(Attacker, Late.Actor, BlowDamage);
	const float Held = Wearer.Owed();
	if (!TestTrue(TEXT("set-up: the blow takes health from a character wearing no row"), Whole > 0.0f)
		|| !TestEqual(TEXT("set-up: the wearer holds half of the blow"), Held, Whole * 0.5f, Tolerance)
		|| !TestEqual(TEXT("set-up: and so does the second wearer"), Late.Owed(), Whole * 0.5f, Tolerance))
	{
		return false;
	}

	// THE CONTROLS, READ FIRST: a drain in the instant of the blow takes nothing, and a character holding no parcel
	// is never drained.
	const float HealthAfterBlow = Wearer.Health();
	TestEqual(TEXT("control: a drain in the instant of the blow takes nothing"), FDelayed::DrainStep(Wearer.Actor),
			  0.0f, 0.0f);
	TestEqual(TEXT("control: and the store still holds the half"), Wearer.Owed(), Held, 0.0f);

	// SIXTEEN STEPS OF A QUARTER OF A SECOND. Each takes a sixteenth, out of health.
	float Taken = 0.0f;
	for (int32 Step = 1; Step <= 16; ++Step)
	{
		const float Before = Wearer.Health();
		const float Drained = StepOn(World, Wearer.Actor, 0.25f);
		Taken += Drained;
		TestEqual(*FString::Printf(TEXT("step %d of 16 takes a sixteenth of what was held"), Step), Drained,
				  Held / 16.0f, Tolerance);
		TestEqual(*FString::Printf(TEXT("step %d takes it out of health"), Step), Before - Wearer.Health(), Drained,
				  Tolerance);
		if (Step == 8)
		{
			TestEqual(TEXT("after two of the four seconds half of what was held is still owed"), Wearer.Owed(),
					  Held * 0.5f, Tolerance);
		}
		if (Step == 15)
		{
			TestTrue(TEXT("a quarter of a second before the row's seconds something is still owed"), Wearer.Owed() > 0.0f);
		}
	}
	TestEqual(TEXT("at the row's four seconds the store is empty"), Wearer.Owed(), 0.0f, 0.0f);
	TestEqual(TEXT("and the sixteen steps took what was held"), Taken, Held, Tolerance);
	TestEqual(TEXT("so the blow has cost the wearer what it cost the character wearing no row"),
			  HealthPool - Wearer.Health(), Whole, Tolerance);
	TestEqual(TEXT("and health fell by what was held since the blow"), HealthAfterBlow - Wearer.Health(), Held, Tolerance);
	TestEqual(TEXT("a seventeenth step takes nothing"), StepOn(World, Wearer.Actor, 0.25f), 0.0f, 0.0f);
	TestEqual(TEXT("control: the character wearing no row is never drained"), FDelayed::DrainStep(Plain.Actor), 0.0f,
			  0.0f);

	// A LATE STEP TAKES WHAT THE LATE TIME OWES. The second wearer's parcel is by now 4.25 seconds old and has not
	// been drained once: one drain takes all of it.
	TestEqual(TEXT("a parcel past its seconds and never drained is taken whole by the first drain"),
			  FDelayed::DrainStep(Late.Actor), Held, Tolerance);
	TestEqual(TEXT("and nothing is owed after it"), Late.Owed(), 0.0f, 0.0f);

	// AND A STEP PART WAY THROUGH TAKES THE PART: a fresh blow, one second, a quarter of the four.
	Strike(Attacker, Late.Actor, BlowDamage);
	TestEqual(TEXT("one second after a fresh blow a single drain takes a quarter of what it held"),
			  StepOn(World, Late.Actor, 1.0f), Held * 0.25f, Tolerance);
	TestEqual(TEXT("and ten seconds later one drain takes the other three quarters"), StepOn(World, Late.Actor, 10.0f),
			  Held * 0.75f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageTwoBlowsTest,
	"Cataclysm.DelayedDamage.TwoBlowsAreTwoParcelsEachDrainedOverTheRowsSecondsFromItsOwnMoment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: nowhere, as above.
 *
 * A wearer at 50% over 4 seconds takes a blow of 400, and two seconds later a blow of 800. With one pooled figure
 * and one clock the second blow would stretch or shorten what the first still owed; the figures read here are those
 * of two parcels, and the last line says which figure a pooled store restarted at the second blow would have given.
 */
bool FCataclysmDelayedDamageTwoBlowsTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedTaker Attacker(World);
	FScopedTaker Plain(World);
	FScopedTaker Wearer(World);
	Wearer.Wear(50.0f, 4.0f);

	// THE CONTROLS: what each of the two blows takes from a character wearing no row.
	const float First = Strike(Attacker, Plain.Actor, BlowDamage) * 0.5f;
	const float Second = Strike(Attacker, Plain.Actor, BlowDamage * 2.0f) * 0.5f;
	if (!TestTrue(TEXT("set-up: both blows take health from a character wearing no row, the second more than the first"),
				  First > 0.0f && Second > First))
	{
		return false;
	}

	Strike(Attacker, Wearer.Actor, BlowDamage);
	if (!TestEqual(TEXT("set-up: the first blow's half is held"), Wearer.Owed(), First, Tolerance))
	{
		return false;
	}

	// TWO SECONDS ON: half of the first parcel is taken, and then the second blow lands.
	TestEqual(TEXT("two seconds after the first blow a drain takes half of its parcel"), StepOn(World, Wearer.Actor, 2.0f),
			  First * 0.5f, Tolerance);
	Strike(Attacker, Wearer.Actor, BlowDamage * 2.0f);
	TestEqual(TEXT("the second blow's half joins what is left of the first"), Wearer.Owed(), First * 0.5f + Second, Tolerance);

	// ONE SECOND ON: a quarter of each parcel.
	TestEqual(TEXT("a second later the drain takes a quarter of the first parcel and a quarter of the second"),
			  StepOn(World, Wearer.Actor, 1.0f), First * 0.25f + Second * 0.25f, Tolerance);

	// ONE MORE: the first parcel is four seconds old and is finished; the second is two.
	TestEqual(TEXT("and a second after that the same again"), StepOn(World, Wearer.Actor, 1.0f),
			  First * 0.25f + Second * 0.25f, Tolerance);
	TestEqual(TEXT("four seconds after the first blow only half of the second parcel is still owed: the second blow "
				   "did not lengthen the first parcel's four seconds"), Wearer.Owed(), Second * 0.5f, Tolerance);

	// TWO MORE: the second parcel is four seconds old and is finished.
	TestEqual(TEXT("two seconds on the drain takes the second parcel's other half"), StepOn(World, Wearer.Actor, 2.0f),
			  Second * 0.5f, Tolerance);
	TestEqual(TEXT("and six seconds after the first blow, four after the second, nothing is owed"), Wearer.Owed(), 0.0f,
			  0.0f);
	TestEqual(TEXT("and both blows have cost the wearer what they cost the character wearing no row"),
			  HealthPool - Wearer.Health(), HealthPool - Plain.Health(), Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageDirectLossTest,
	"Cataclysm.DelayedDamage.TheDrainedLossMeetsNoArmourNoEnergyShieldAndNoTemporaryAbsorb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: nowhere, as above.
 *
 * A wearer at 50% over 4 seconds takes a blow with no armour and no pool, and is then given 5,000 armour, an energy
 * shield of 500 and a temporary absorb of 500 before anything is drained. A character wearing no row, given the same
 * three, is the control that those three do take from a blow.
 */
bool FCataclysmDelayedDamageDirectLossTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const auto Defend = [](const FScopedTaker& Who)
	{
		Who.AbilitySystem->SetNumericAttributeBase(Combat::GetArmorAttribute(), 5000.0f);
		Who.GiveShield(500.0f);
		Who.AbilitySystem->GrantTemporaryAbsorb(500.0f);
	};

	FScopedTaker Attacker(World);
	FScopedTaker Plain(World);
	FScopedTaker Defended(World);
	FScopedTaker Wearer(World);
	Wearer.Wear(50.0f, 4.0f);

	// THE CONTROL, READ FIRST: the armour, the shield and the absorb do take from a blow. A character wearing no row
	// and holding all three loses less health to the blow than one holding none, and its two pools lose something.
	const float Whole = Strike(Attacker, Plain.Actor, BlowDamage);
	Defend(Defended);
	const float DefendedHealth = Strike(Attacker, Defended.Actor, BlowDamage);
	const float DefendedPools =
		(500.0f - Defended.Shield()) + (500.0f - Defended.AbilitySystem->TemporaryAbsorbHeld());
	if (!TestTrue(*FString::Printf(TEXT("control: a blow that takes %.2f from a character with no defences takes %.2f "
										"from the health of one holding the three, and %.2f from its pools"),
								   Whole, DefendedHealth, DefendedPools),
				  Whole > 0.0f && DefendedHealth < Whole && DefendedPools > 0.0f))
	{
		return false;
	}

	// THE WEARER TAKES ITS BLOW WITH NO DEFENCES, AND IS GIVEN THE THREE AFTERWARDS.
	Strike(Attacker, Wearer.Actor, BlowDamage);
	const float Held = Wearer.Owed();
	if (!TestEqual(TEXT("set-up: the blow leaves the wearer owing half of it"), Held, Whole * 0.5f, Tolerance))
	{
		return false;
	}
	Defend(Wearer);

	const float HealthBefore = Wearer.Health();
	const float ShieldBefore = Wearer.Shield();
	const float AbsorbBefore = Wearer.AbilitySystem->TemporaryAbsorbHeld();
	const float Drained = StepOn(World, Wearer.Actor, 4.0f);
	TestEqual(TEXT("at the row's seconds the drain takes the whole of what was held, unreduced by the armour"), Drained,
			  Held, Tolerance);
	TestEqual(TEXT("out of health"), HealthBefore - Wearer.Health(), Held, Tolerance);
	TestEqual(TEXT("and nothing out of the energy shield"), Wearer.Shield(), ShieldBefore, 0.0f);
	TestEqual(TEXT("and nothing out of the temporary absorb"), Wearer.AbilitySystem->TemporaryAbsorbHeld(), AbsorbBefore,
			  0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// A blow that would have killed, and the drain that kills
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageWouldHaveKilledTest,
	"Cataclysm.DelayedDamage.TheShareIsTakenOfTheWholeBlowSoABlowThatWouldHaveEmptiedHealthLeavesSome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: nowhere, as above.
 *
 * A character wearing no row and a wearer at 50% over 4 seconds each hold four fifths of what the blow is worth.
 * A plain actor has no death to die, so health reaching nought is what is read here; the test below reads a death.
 */
bool FCataclysmDelayedDamageWouldHaveKilledTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedTaker Attacker(World);
	FScopedTaker Measure(World);
	FScopedTaker Plain(World);
	FScopedTaker Wearer(World);
	Wearer.Wear(50.0f, 4.0f);

	const float Whole = Strike(Attacker, Measure.Actor, BlowDamage);
	if (!TestTrue(TEXT("set-up: the blow takes health from a character wearing no row"), Whole > 0.0f))
	{
		return false;
	}
	Plain.SetHealth(Whole * 0.8f);
	Wearer.SetHealth(Whole * 0.8f);

	// THE CONTROL: with no row the blow empties the health.
	Strike(Attacker, Plain.Actor, BlowDamage);
	if (!TestEqual(TEXT("control: holding four fifths of what the blow is worth and no row, the blow empties health"),
				   Plain.Health(), 0.0f, Tolerance))
	{
		return false;
	}

	// THE WEARER: half of the WHOLE blow is held, and not half of the four fifths the health could take.
	const float AtOnce = Strike(Attacker, Wearer.Actor, BlowDamage);
	TestEqual(TEXT("the wearer loses half of the whole blow at once"), AtOnce, Whole * 0.5f, Tolerance);
	TestEqual(TEXT("and is left with three tenths of what the blow is worth"), Wearer.Health(), Whole * 0.3f, Tolerance);
	TestEqual(TEXT("and owes half of the whole blow, which is more than the health it has left"), Wearer.Owed(),
			  Whole * 0.5f, Tolerance);

	// THE STORE THEN EMPTIES IT. After two seconds half the parcel is taken; a second later the next quarter is
	// more than the health left.
	TestEqual(TEXT("two seconds on the drain takes a quarter of the whole blow"), StepOn(World, Wearer.Actor, 2.0f),
			  Whole * 0.25f, Tolerance);
	TestEqual(TEXT("leaving a twentieth of it as health"), Wearer.Health(), Whole * 0.05f, Tolerance);
	TestEqual(TEXT("a second later the drain is owed an eighth and takes the twentieth that is there"),
			  StepOn(World, Wearer.Actor, 1.0f), Whole * 0.05f, Tolerance);
	TestEqual(TEXT("health is at nought"), Wearer.Health(), 0.0f, 0.0f);
	TestEqual(TEXT("and the store went with it"), Wearer.Owed(), 0.0f, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageKillsTest,
	"Cataclysm.DelayedDamage.TheDrainKillsAndTheDeathNamesTheAttackerOfTheLastBlowOnRecordAndClearsTheStore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: a real player character at the origin, on a player state, because a real death is what is read. The
 * attacker is a plain actor with no root component and so no body.
 *
 * The player is struck twice with no row, to read what the blow is worth to it and that two blows agree. It is then
 * left four fifths of that as health, wears 50% over 4 seconds, and is struck once more.
 */
bool FCataclysmDelayedDamageKillsTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FScopedNeverBlocks NeverBlocks;
	ACataclysmPlayerCharacter* Player = SpawnPossessedPlayer(World);
	if (!TestNotNull(TEXT("set-up: a possessed player character on a player state"), Player))
	{
		return false;
	}

	FSystem* System = Cast<FSystem>(Player->GetAbilitySystemComponent());
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("set-up: the player's ability system"), System)
		|| !TestNotNull(TEXT("set-up: the world's combat events"), Events))
	{
		return false;
	}
	System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 100000.0f);
	System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 100000.0f);

	FScopedTaker Attacker(World);

	// WHO EACH DEATH OF THE PLAYER NAMES AS ITS KILLER.
	TArray<AActor*> Killers;
	const FDelegateHandle Heard = Events->OnDeath.AddLambda(
		[&Killers, Player](const FCataclysmDeathNotice& Notice)
		{
			if (Notice.Victim == Player)
			{
				Killers.Add(Notice.Killer);
			}
		});
	ON_SCOPE_EXIT { Events->OnDeath.Remove(Heard); };

	// THE CONTROLS, READ FIRST: what the blow is worth to this player, twice, with no row.
	const float Whole = Strike(Attacker, Player, BlowDamage);
	if (!TestTrue(*FString::Printf(TEXT("set-up: the blow takes health from the player with no row: %.2f"), Whole),
				  Whole > 0.0f)
		|| !TestEqual(TEXT("set-up: and a second blow takes the same, so nothing about it is rolled"),
					  Strike(Attacker, Player, BlowDamage), Whole, Tolerance)
		|| !TestEqual(TEXT("control: with no row the player owes nothing"), System->DelayedDamageOwed(), 0.0f, 0.0f)
		|| !TestEqual(TEXT("control: and a drain takes nothing"), FDelayed::DrainStep(Player), 0.0f, 0.0f))
	{
		return false;
	}

	System->SetNumericAttributeBase(Vital::GetHealthAttribute(), Whole * 0.8f);
	WearTheRow(System, 50.0f, 4.0f);

	// THE BLOW THAT WOULD HAVE KILLED LEAVES THE PLAYER ALIVE AND OWING.
	TestEqual(TEXT("the player loses half of the whole blow at once"), Strike(Attacker, Player, BlowDamage),
			  Whole * 0.5f, Tolerance);
	if (!TestFalse(TEXT("and is alive, though the blow was worth more than its health"),
				   UCataclysmSkillEffects::IsDead(Player))
		|| !TestEqual(TEXT("and owes half of the whole blow"), System->DelayedDamageOwed(), Whole * 0.5f, Tolerance))
	{
		return false;
	}
	TestTrue(TEXT("control: the player's last blow on record names the attacker"),
			 System->GetLastBlow().Attacker.Get() == Attacker.Actor);
	TestEqual(TEXT("control: no death has been announced"), Killers.Num(), 0);

	// TWO SECONDS ON THE DRAIN TAKES A QUARTER OF THE WHOLE BLOW AND THE PLAYER LIVES.
	TestEqual(TEXT("two seconds on the drain takes a quarter of the whole blow"), StepOn(World, Player, 2.0f),
			  Whole * 0.25f, Tolerance);
	if (!TestFalse(TEXT("and the player is still alive, with a twentieth of the blow as health"),
				   UCataclysmSkillEffects::IsDead(Player)))
	{
		return false;
	}

	// A SECOND LATER THE DRAIN IS OWED MORE THAN THE HEALTH LEFT, AND KILLS.
	TestEqual(TEXT("a second later the drain takes the health that is left"), StepOn(World, Player, 1.0f),
			  Whole * 0.05f, Tolerance);
	TestTrue(TEXT("and the player is dead of it"), UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("its health is at nought"), HealthOf(Player), 0.0f, 0.0f);
	TestEqual(TEXT("the store was cleared by the death"), System->DelayedDamageOwed(), 0.0f, 0.0f);
	if (TestEqual(TEXT("one death of the player was announced"), Killers.Num(), 1))
	{
		TestTrue(TEXT("and it names the attacker of the last blow on record as the killer"),
				 Killers[0] == Attacker.Actor);
	}

	// A CORPSE PAYS NOTHING, and what a revival clears includes the store.
	if (TestTrue(TEXT("set-up: a parcel can be held on the dead player by hand"), System->HoldDelayedDamage(50.0f, 4.0f)))
	{
		TestEqual(TEXT("a drain a second later takes nothing from a corpse"), StepOn(World, Player, 1.0f), 0.0f, 0.0f);
		TestEqual(TEXT("control: the parcel is still held whole"), System->DelayedDamageOwed(), 50.0f, Tolerance);
		const FCataclysmWhatDeathEnded Ended = System->ClearWhatDeathEnds();
		TestEqual(TEXT("what a death ends, cleared at the revival, includes the 50 still owed"), Ended.DelayedDamageOwed,
				  50.0f, Tolerance);
		TestEqual(TEXT("and nothing is owed after it"), System->DelayedDamageOwed(), 0.0f, 0.0f);
	}
	TestEqual(TEXT("and no second death was announced"), Killers.Num(), 1);
	return true;
}

// ---------------------------------------------------------------------------
// What clears the store, and what does not
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageClearedTest,
	"Cataclysm.DelayedDamage.ADeathAndAnotherDungeonClearTheStoreAndAFloorOfTheSameDungeonDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: nowhere, as above. A dungeon game mode is spawned into the world to say which dungeon is walked, as the
 * test of the kept stacks does it.
 *
 * WHAT THE FLOOR CHANGE HERE IS. The game mode's floor number is written by hand. `GoToFloor` is not called: it
 * builds a floor, and the game mode's file is not this change's to read further. So this shows that the store does
 * not read the floor number. It does not show what the game mode does to a character when a floor changes in play.
 */
bool FCataclysmDelayedDamageClearedTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedTaker Attacker(World);
	FScopedTaker Died(World);
	FScopedTaker Walker(World);
	Died.Wear(50.0f, 8.0f);
	Walker.Wear(50.0f, 8.0f);

	// A DEATH. `ClearWhatDeathEnds` is what a revival runs.
	Strike(Attacker, Died.Actor, BlowDamage);
	const float Held = Died.Owed();
	if (!TestTrue(TEXT("control: the blow leaves the wearer owing something"), Held > 0.0f))
	{
		return false;
	}
	const FCataclysmWhatDeathEnded Ended = Died.AbilitySystem->ClearWhatDeathEnds();
	TestEqual(TEXT("what a death ends includes all that was owed"), Ended.DelayedDamageOwed, Held, Tolerance);
	TestEqual(TEXT("and nothing is owed after it"), Died.Owed(), 0.0f, 0.0f);
	TestEqual(TEXT("and a drain at the row's seconds takes nothing"), StepOn(World, Died.Actor, 8.0f), 0.0f, 0.0f);

	// THE DUNGEON. First with no game mode, then with one walking no dungeon of the empire: nothing changes.
	Strike(Attacker, Walker.Actor, BlowDamage);
	if (!TestEqual(TEXT("control: with no dungeon game mode in the world the wearer owes its half"), Walker.Owed(), Held,
				   Tolerance))
	{
		return false;
	}
	ACataclysmDungeonGameMode* Mode = World->SpawnActor<ACataclysmDungeonGameMode>();
	if (!TestNotNull(TEXT("set-up: the dungeon game mode spawned"), Mode)
		|| !TestEqual(TEXT("set-up: it walks no dungeon of the empire"), Mode->EmpireDungeonId,
					  static_cast<int32>(INDEX_NONE)))
	{
		return false;
	}
	TestEqual(TEXT("control: with the game mode walking no dungeon the half is still owed"), Walker.Owed(), Held, Tolerance);

	// A FLOOR OF THE SAME DUNGEON: the floor number moves and nothing of the store does.
	Mode->FloorNumber = Mode->FloorNumber + 1;
	TestEqual(TEXT("with the floor number moved on the half is still owed"), Walker.Owed(), Held, Tolerance);
	TestEqual(TEXT("and a second later the drain takes an eighth of it, as on any floor"),
			  StepOn(World, Walker.Actor, 1.0f), Held / 8.0f, Tolerance);
	TestEqual(TEXT("leaving seven eighths owed"), Walker.Owed(), Held * 7.0f / 8.0f, Tolerance);

	// ANOTHER DUNGEON: what was taken outside it is not read inside it, is not drained, and is gone after a step.
	Mode->EmpireDungeonId = 7;
	TestEqual(TEXT("in dungeon 7 what was taken outside it is not owed"), Walker.Owed(), 0.0f, 0.0f);
	const float HealthInside = Walker.Health();
	TestEqual(TEXT("and a drain a second later takes nothing"), StepOn(World, Walker.Actor, 1.0f), 0.0f, 0.0f);
	TestEqual(TEXT("and health is where it was"), Walker.Health(), HealthInside, 0.0f);
	Mode->EmpireDungeonId = INDEX_NONE;
	TestEqual(TEXT("and outside it again, after that step, nothing is owed: the parcel was removed"), Walker.Owed(), 0.0f,
			  0.0f);

	// AND LEAVING A DUNGEON CLEARS WHAT WAS TAKEN INSIDE IT.
	Mode->EmpireDungeonId = 7;
	Strike(Attacker, Walker.Actor, BlowDamage);
	if (TestEqual(TEXT("control: a blow taken inside dungeon 7 is owed inside it"), Walker.Owed(), Held, Tolerance))
	{
		Mode->EmpireDungeonId = INDEX_NONE;
		TestEqual(TEXT("with the dungeon left it is not owed"), Walker.Owed(), 0.0f, 0.0f);
		TestEqual(TEXT("and a drain a second later takes nothing"), StepOn(World, Walker.Actor, 1.0f), 0.0f, 0.0f);
		Mode->EmpireDungeonId = 7;
		TestEqual(TEXT("and back inside dungeon 7, after that step, nothing is owed"), Walker.Owed(), 0.0f, 0.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageGearOffTest,
	"Cataclysm.DelayedDamage.WithTheGearOffTheStoreKeepsDrainingAndAChangedRowLeavesParcelsAlreadyTakenAsTheyWere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: nowhere, as above.
 *
 * One wearer at 50% over 4 seconds takes a blow and takes the row off. A second takes a blow at 50% over 4 seconds
 * and then wears 25% over 8.
 */
bool FCataclysmDelayedDamageGearOffTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;

	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedTaker Attacker(World);
	FScopedTaker Plain(World);
	FScopedTaker Bare(World);
	FScopedTaker Changed(World);
	Bare.Wear(50.0f, 4.0f);
	Changed.Wear(50.0f, 4.0f);

	const float Whole = Strike(Attacker, Plain.Actor, BlowDamage);
	Strike(Attacker, Bare.Actor, BlowDamage);
	Strike(Attacker, Changed.Actor, BlowDamage);
	if (!TestTrue(TEXT("set-up: the blow takes health from a character wearing no row"), Whole > 0.0f)
		|| !TestEqual(TEXT("control: the first wearer owes half of the blow"), Bare.Owed(), Whole * 0.5f, Tolerance)
		|| !TestEqual(TEXT("control: and so does the second"), Changed.Owed(), Whole * 0.5f, Tolerance))
	{
		return false;
	}

	// THE GEAR COMES OFF. What is owed stays owed; a new blow is not delayed; and the parcel drains over its four
	// seconds as it would have.
	Bare.Wear(0.0f, 0.0f);
	TestEqual(TEXT("with the row off the half is still owed"), Bare.Owed(), Whole * 0.5f, Tolerance);
	TestEqual(TEXT("a blow taken with the row off lands whole"), Strike(Attacker, Bare.Actor, BlowDamage), Whole, Tolerance);
	TestEqual(TEXT("and adds nothing to what is owed"), Bare.Owed(), Whole * 0.5f, Tolerance);
	TestEqual(TEXT("two seconds on, with the row off, the drain takes half of the parcel"),
			  StepOn(World, Bare.Actor, 2.0f), Whole * 0.25f, Tolerance);
	TestEqual(TEXT("and two seconds after that the other half"), StepOn(World, Bare.Actor, 2.0f), Whole * 0.25f, Tolerance);
	TestEqual(TEXT("and nothing is owed"), Bare.Owed(), 0.0f, 0.0f);

	// THE ROW CHANGES TO 25% OVER 8 SECONDS, four seconds after the second wearer's blow, which has not been
	// drained yet. Its parcel keeps the four seconds it was taken with: one drain now takes all of it.
	Changed.Wear(25.0f, 8.0f);
	TestEqual(TEXT("a parcel taken at 4 seconds is finished at 4 seconds, though the row now states 8"),
			  FDelayed::DrainStep(Changed.Actor), Whole * 0.5f, Tolerance);
	// AND A BLOW TAKEN NOW IS A PARCEL OF THE NEW FIGURES.
	TestEqual(TEXT("a blow taken under the changed row lands three quarters at once"),
			  Strike(Attacker, Changed.Actor, BlowDamage), Whole * 0.75f, Tolerance);
	TestEqual(TEXT("and a quarter is held"), Changed.Owed(), Whole * 0.25f, Tolerance);
	TestEqual(TEXT("which four seconds on is half drained, being a parcel over 8"), StepOn(World, Changed.Actor, 4.0f),
			  Whole * 0.125f, Tolerance);
	return true;
}

// ---------------------------------------------------------------------------
// What the wearer sees
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDelayedDamageTextTest,
	"Cataclysm.DelayedDamage.TheTextSaysWhatIsStillOwedRoundedUpWhileTheStoreHoldsAnything",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: a real player character at the origin, on a player state, because the text is drawn above the player's
 * own bars; and a plain actor with no body, which owes nothing.
 *
 * The parcel is held by hand, 131.2 over 4 seconds, so that the text can be stated as written. The heads-up display
 * draws this text; no test draws, so only the text and the status line are read.
 */
bool FCataclysmDelayedDamageTextTest::RunTest(const FString&)
{
	using namespace CataclysmDelayedDamageTest;
	using FOverlay = UCataclysmCombatOverlay;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnPossessedPlayer(World);
	FSystem* System = Player ? Cast<FSystem>(Player->GetAbilitySystemComponent()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a player with an ability system"), System))
	{
		return false;
	}
	System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), HealthPool);
	System->SetNumericAttributeBase(Vital::GetHealthAttribute(), HealthPool);
	FScopedTaker Other(World);

	// THE CONTROL, READ FIRST: nothing owed, no text, and nothing of it on the status line.
	TestEqual(TEXT("control: a player owing nothing says nothing of it"), FOverlay::OwedTextFor(Player), FString());
	TestFalse(TEXT("control: and its status line does not say Owed"),
			  FOverlay::StatusLineFor(Player).Contains(TEXT("Owed")));

	if (!TestTrue(TEXT("set-up: a parcel of 131.2 over 4 seconds is held"), System->HoldDelayedDamage(131.2f, 4.0f)))
	{
		return false;
	}
	TestEqual(TEXT("with 131.2 owed the text reads Owed 132, rounded up"), FOverlay::OwedTextFor(Player),
			  FString(TEXT("Owed 132")));
	TestTrue(TEXT("and the status line holds that text"), FOverlay::StatusLineFor(Player).Contains(TEXT("Owed 132")));
	TestEqual(TEXT("control: another character, owing nothing, says nothing"), FOverlay::OwedTextFor(Other.Actor),
			  FString());

	// IT FALLS AS THE STORE DRAINS. After two seconds 65.6 is owed, which reads 66.
	TestEqual(TEXT("set-up: two seconds on the drain takes half of the parcel"), StepOn(World, Player, 2.0f), 65.6f,
			  Tolerance);
	TestEqual(TEXT("with 65.6 owed it reads Owed 66"), FOverlay::OwedTextFor(Player), FString(TEXT("Owed 66")));
	// A SECOND AND THREE QUARTERS ON, 8.2 IS OWED, WHICH READS 9.
	StepOn(World, Player, 1.75f);
	TestEqual(TEXT("with 8.2 owed it reads Owed 9"), FOverlay::OwedTextFor(Player), FString(TEXT("Owed 9")));
	StepOn(World, Player, 0.25f);
	TestEqual(TEXT("at the parcel's four seconds the text is empty"), FOverlay::OwedTextFor(Player), FString());
	TestFalse(TEXT("and the status line no longer says Owed"), FOverlay::StatusLineFor(Player).Contains(TEXT("Owed")));
	TestEqual(TEXT("and no actor gives no text"), FOverlay::OwedTextFor(nullptr), FString());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
