// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"

/**
 * Issue #920, ruled by the coordinating session 2026-10-31: Necrosis "denies the affected target all healing for that
 * time, dealing the denied amount as damage over the following 5 seconds".
 *
 * Q4: the TOTAL denied while the Necrosis held is dealt over the 5 seconds after it ends; a refresh extends "that
 * time", so the total keeps growing; it is dealt when the Necrosis ends for ANY reason, a cleanse included; a carrier
 * that dies first takes nothing more. Q5: healing is everything that goes through `UCataclysmRegeneration::TopUp` for
 * health, and the denied amount is what would have been restored after `healing_received` and
 * `healing_received_reduction`.
 *
 * THE NECROSES ARE REAL. Each is laid by `UCataclysmSkillEffects::ApplyDamageOverTime`, which puts the listener on it
 * that deals the total when it ends, and each heal is a real `TopUp`. The clock is moved by
 * `CataclysmTestWorld::RunClock`, which fires the engine's own period and duration timers.
 *
 * EVERY CREATURE STARTS AT 500 OF 1000 HEALTH, so a heal of 100 has room, with no regeneration and no energy shield.
 * The applier stands 20 m away and every creature 5 m from the next, as in `CataclysmBleedMovementTests.cpp`.
 *
 * NO FIGURE ASSUMES A CREATURE'S DEFENCES. What the denied healing deals is compared with what a control creature
 * loses to a plain damage over time of the same figure a tick, laid by the same applier with the same flags; both
 * meet the same resistances.
 */
namespace CataclysmNecrosisDenialTest
{
	constexpr float M = 100.0f;

	/** Every creature's maximum, and where its health is put before each test. */
	constexpr float NecroticMaximum = 1000.0f;
	constexpr float NecroticStart = 500.0f;

	/** A player character on a player state at the origin: whoever lays the Necroses. */
	struct FNecrosisApplier
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		explicit FNecrosisApplier(UWorld* World)
		{
			ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			AbilitySystem = PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (Character && PlayerState && AbilitySystem)
			{
				Character->SetPlayerState(PlayerState);
				Character->OnRep_PlayerState();
				AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(),
													   100000.0f);
				AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			}
		}

		bool IsComplete() const { return Character && AbilitySystem; }
	};

	/** The ability system of this project's that a creature holds. */
	UCataclysmAbilitySystemComponent* NecroticSystemOf(const AActor* Who)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who));
	}

	/**
	 * A creature of the monsters' team standing here, at 500 of 1000 health, with no regeneration and no shield, or
	 * null if any of that could not be written.
	 */
	ACataclysmEnemyCharacter* NecroticCreatureAt(UWorld* World, const FVector& At)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (!Made)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetHealth(NecroticMaximum);
		// ONLY ONCE ITS ATTRIBUTE SETS ARE REGISTERED: writing before raises an engine ensure (see `SetHealth`).
		UCataclysmAbilitySystemComponent* System = NecroticSystemOf(Made);
		if (!System || !System->HasAttributeSetForAttribute(UCataclysmVitalAttributeSet::GetHealthRegenAttribute()))
		{
			return nullptr;
		}
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthRegenAttribute(), 0.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 0.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), NecroticStart);
		return Made;
	}

	/** The health an actor holds, or nought for one that is gone. */
	float NecroticHealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = IsValid(Who) ? UCataclysmTargeting::AbilitySystemOf(Who) : nullptr;
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	/** The healing a creature's Necrosis has denied it so far, or -1 for one with no ability system of ours. */
	float DeniedSoFar(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* System = NecroticSystemOf(Who);
		return System ? System->NecrosisDeniedHealing : -1.0f;
	}

	/** Whether this actor carries this tag now. */
	bool NecroticCarries(const AActor* Who, const FGameplayTag& Tag)
	{
		const UCataclysmAbilitySystemComponent* System = NecroticSystemOf(Who);
		return System && Tag.IsValid() && System->HasMatchingGameplayTag(Tag);
	}

	/** A heal of health through the one function every heal goes through. */
	void HealThroughTopUp(const AActor* Who, float Amount)
	{
		if (UCataclysmAbilitySystemComponent* System = NecroticSystemOf(Who))
		{
			UCataclysmRegeneration::TopUp(*System, UCataclysmVitalAttributeSet::GetHealthAttribute(),
										  UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Amount);
		}
	}

	/** How many damage over time effects are running on this actor, by the flag every tick carries. */
	int32 DamageOverTimeEffectsOn(const AActor* Who)
	{
		const UCataclysmAbilitySystemComponent* System = NecroticSystemOf(Who);
		const FGameplayTag OverTime = UCataclysmDebuffs::DamageOverTimeTag();
		if (!System || !OverTime.IsValid())
		{
			return -1;
		}
		return System->GetActiveEffects(
			FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(OverTime))).Num();
	}

	/** `Keyword.DoT.Generic`: the control's plain damage over time, which is no ailment. */
	FGameplayTag PlainDamageOverTimeTag()
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Keyword.DoT.Generic")), /*ErrorIfNotFound=*/false);
	}

	/** The damage over time hit notices `UCataclysmCombatEvents` sends, counted by the character struck. */
	struct FNecroticTickCount
	{
		TArray<const AActor*> Struck;
		FDelegateHandle Handle;
		UCataclysmCombatEvents* Events = nullptr;

		explicit FNecroticTickCount(UWorld* World)
		{
			Events = UCataclysmCombatEvents::In(World);
			if (Events)
			{
				Handle = Events->OnHit.AddLambda([this](const FCataclysmHitNotice& Notice)
				{
					if (Notice.bDamageOverTime)
					{
						Struck.Add(Notice.Target);
					}
				});
			}
		}

		~FNecroticTickCount()
		{
			if (Events)
			{
				Events->OnHit.Remove(Handle);
			}
		}

		/** How many ticks have been announced on `Target` since this began counting or was last emptied. */
		int32 On(const AActor* Target) const
		{
			int32 Count = 0;
			for (const AActor* One : Struck)
			{
				Count += One == Target ? 1 : 0;
			}
			return Count;
		}

		void Empty() { Struck.Reset(); }
	};

	/** Where the creatures stand. The applier is at the origin. */
	const FVector NecroticFirstAt(20.0f * M, 0.0f, 0.0f);
	const FVector NecroticSecondAt(25.0f * M, 0.0f, 0.0f);
}

#define CATACLYSM_NECROSIS_DENIAL_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * A CARRIER OF NECROSIS IS HEALED BY NOTHING, AND WHAT IT WOULD HAVE BEEN HEALED IS RECORDED. Two creatures at 500 of
 * 1000. The first carries a Necrosis; the second, the control, carries none. Each is offered the same heal of 100
 * through `TopUp`. The control is healed the whole 100, which is the control on the heal itself; the carrier is healed
 * nothing, and the 100 is its denied total. No clock runs, so no tick of the Necrosis lands.
 */
CATACLYSM_NECROSIS_DENIAL_TEST(FCataclysmNecrosisDeniesHealingTest,
	"Cataclysm.Ailments.NecrosisDeniesHealingWhileItHolds")
{
	using namespace CataclysmNecrosisDenialTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();
	FNecrosisApplier Applier(World);
	ACataclysmEnemyCharacter* Carrier = NecroticCreatureAt(World, NecroticFirstAt);
	ACataclysmEnemyCharacter* Control = NecroticCreatureAt(World, NecroticSecondAt);
	if (!TestTrue(TEXT("set-up: Keyword.DoT.Necrosis is a tag this build knows"), Necrosis.IsValid())
		|| !TestTrue(TEXT("set-up: the applier"), Applier.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, the carrier"), Carrier)
		|| !TestNotNull(TEXT("set-up: creature 1, the control"), Control)
		|| !TestTrue(TEXT("set-up: the carrier's Necrosis is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Carrier, 1.0f, 10.0f, Necrosis,
														/*bScalesWithInstigator=*/false))
		|| !TestTrue(TEXT("set-up: the carrier carries it"), NecroticCarries(Carrier, Necrosis))
		|| !TestFalse(TEXT("set-up: and the control does not"), NecroticCarries(Control, Necrosis))
		|| !TestEqual(TEXT("set-up: the carrier stands at 500"), NecroticHealthOf(Carrier), NecroticStart, 0.001f)
		|| !TestEqual(TEXT("set-up: and so does the control"), NecroticHealthOf(Control), NecroticStart, 0.001f)
		|| !TestEqual(TEXT("set-up: and nothing has been denied yet"), DeniedSoFar(Carrier), 0.0f, 0.001f))
	{
		return false;
	}

	HealThroughTopUp(Control, 100.0f);
	HealThroughTopUp(Carrier, 100.0f);

	if (!TestEqual(TEXT("control: the same heal without a Necrosis restores the whole 100"),
				   NecroticHealthOf(Control), NecroticStart + 100.0f, 0.001f))
	{
		return false;
	}
	TestEqual(TEXT("the carrier of a Necrosis is healed nothing"), NecroticHealthOf(Carrier), NecroticStart, 0.001f);
	TestEqual(TEXT("and the 100 it would have been healed is its denied total"), DeniedSoFar(Carrier), 100.0f, 0.001f);
	TestEqual(TEXT("and the control has denied nothing"), DeniedSoFar(Control), 0.0f, 0.001f);
	return true;
}

/**
 * THE DENIED TOTAL IS DEALT AFTER THE NECROSIS ENDS, OVER 5 SECONDS. A Necrosis of one a tick for 2 s; a heal of 100
 * denied inside it. At 2.25 s the Necrosis has run out by time, its total has been taken, and the control's plain
 * damage over time of 20 a tick for 5 s (100 / 5) is laid. Over the next 0.5 s nothing reaches the carrier: the first
 * tick is a second after the end. Over the 3 s after that it loses what the control loses in the same 3 s, three
 * ticks, and less than the whole; over the 2.75 s after that the two have lost the same in all, five ticks each, and
 * the carrier's five are its only ones in the window. A further second deals nothing.
 */
CATACLYSM_NECROSIS_DENIAL_TEST(FCataclysmNecrosisDealsTheDeniedHealingTest,
	"Cataclysm.Ailments.NecrosisDealsTheDeniedHealingOverTheFollowingFiveSeconds")
{
	using namespace CataclysmNecrosisDenialTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();
	const FGameplayTag Plain = PlainDamageOverTimeTag();
	FNecrosisApplier Applier(World);
	ACataclysmEnemyCharacter* Carrier = NecroticCreatureAt(World, NecroticFirstAt);
	ACataclysmEnemyCharacter* Control = NecroticCreatureAt(World, NecroticSecondAt);
	if (!TestTrue(TEXT("set-up: the Necrosis and plain damage over time tags"), Necrosis.IsValid() && Plain.IsValid())
		|| !TestTrue(TEXT("set-up: the applier"), Applier.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, the carrier"), Carrier)
		|| !TestNotNull(TEXT("set-up: creature 1, the control"), Control)
		|| !TestTrue(TEXT("set-up: the carrier's Necrosis, one a tick for 2 s, is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Carrier, 1.0f, 2.0f, Necrosis,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	HealThroughTopUp(Carrier, 100.0f);
	if (!TestEqual(TEXT("set-up: the heal of 100 is denied"), DeniedSoFar(Carrier), 100.0f, 0.001f))
	{
		return false;
	}

	CataclysmTestWorld::RunClock(World, 2.25f);
	if (!TestFalse(TEXT("set-up: the Necrosis has run out by time"), NecroticCarries(Carrier, Necrosis))
		|| !TestEqual(TEXT("its denied total has been taken to be dealt"), DeniedSoFar(Carrier), 0.0f, 0.001f)
		|| !TestTrue(TEXT("set-up: the control's plain damage over time, 20 a tick for 5 s, is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Control, 20.0f, 5.0f, Plain,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}

	FNecroticTickCount Ticks(World);
	if (!TestNotNull(TEXT("set-up: the hit notices"), Ticks.Events))
	{
		return false;
	}
	const float CarrierAtTheEnd = NecroticHealthOf(Carrier);
	const float ControlAtTheEnd = NecroticHealthOf(Control);

	CataclysmTestWorld::RunClock(World, 0.5f);
	TestEqual(TEXT("nothing is dealt at once: the first tick is a second after the end"),
			  CarrierAtTheEnd - NecroticHealthOf(Carrier), 0.0f, 0.001f);

	CataclysmTestWorld::RunClock(World, 3.0f);
	const float CarrierPart = CarrierAtTheEnd - NecroticHealthOf(Carrier);
	const float ControlPart = ControlAtTheEnd - NecroticHealthOf(Control);
	if (!TestTrue(FString::Printf(TEXT("control: three of the control's ticks took %.3f"), ControlPart),
				  ControlPart > 0.0f))
	{
		return false;
	}
	TestEqual(FString::Printf(TEXT("three seconds in, the carrier has lost what the control lost (%.3f against %.3f)"),
							  CarrierPart, ControlPart),
			  CarrierPart, ControlPart, 0.01f);

	CataclysmTestWorld::RunClock(World, 2.75f);
	const float CarrierWhole = CarrierAtTheEnd - NecroticHealthOf(Carrier);
	const float ControlWhole = ControlAtTheEnd - NecroticHealthOf(Control);
	TestTrue(FString::Printf(TEXT("so the total was dealt over time and not at once (%.3f of %.3f by then)"),
							 CarrierPart, CarrierWhole),
			 CarrierPart > 0.0f && CarrierPart < CarrierWhole);
	TestEqual(FString::Printf(TEXT("the carrier lost the whole denied 100 as the control lost its 100 (%.3f against %.3f)"),
							  CarrierWhole, ControlWhole),
			  CarrierWhole, ControlWhole, 0.01f);
	TestEqual(TEXT("in five ticks, one a second"), Ticks.On(Carrier), 5);

	CataclysmTestWorld::RunClock(World, 1.0f);
	TestEqual(TEXT("and a second later nothing more"),
			  CarrierAtTheEnd - NecroticHealthOf(Carrier), CarrierWhole, 0.001f);
	return true;
}

/**
 * A CLEANSE ENDS A NECROSIS, AND THE HEALING IT DENIED IS STILL DEALT: the denial has already happened (Q4). A
 * Necrosis of one a tick for 10 s; a heal of 100 denied; a cleanse before any tick. The Necrosis is gone and its total
 * taken; the control's plain damage over time of 20 a tick for 5 s is laid at the same moment. After 5.5 s the carrier
 * has lost what the control lost, in five ticks.
 */
CATACLYSM_NECROSIS_DENIAL_TEST(FCataclysmNecrosisCleansedTest,
	"Cataclysm.Ailments.ANecrosisEndedByACleanseStillDealsTheHealingItDenied")
{
	using namespace CataclysmNecrosisDenialTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();
	const FGameplayTag Plain = PlainDamageOverTimeTag();
	FNecrosisApplier Applier(World);
	ACataclysmEnemyCharacter* Carrier = NecroticCreatureAt(World, NecroticFirstAt);
	ACataclysmEnemyCharacter* Control = NecroticCreatureAt(World, NecroticSecondAt);
	if (!TestTrue(TEXT("set-up: the Necrosis and plain damage over time tags"), Necrosis.IsValid() && Plain.IsValid())
		|| !TestTrue(TEXT("set-up: the applier"), Applier.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, the carrier"), Carrier)
		|| !TestNotNull(TEXT("set-up: creature 1, the control"), Control)
		|| !TestTrue(TEXT("set-up: the carrier's Necrosis, one a tick for 10 s, is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Carrier, 1.0f, 10.0f, Necrosis,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	HealThroughTopUp(Carrier, 100.0f);
	if (!TestEqual(TEXT("set-up: the heal of 100 is denied"), DeniedSoFar(Carrier), 100.0f, 0.001f))
	{
		return false;
	}

	FNecroticTickCount Ticks(World);
	if (!TestNotNull(TEXT("set-up: the hit notices"), Ticks.Events)
		|| !TestTrue(TEXT("set-up: the cleanse removes something"), UCataclysmDebuffs::Cleanse(Carrier) > 0)
		|| !TestFalse(TEXT("set-up: and the Necrosis is gone"), NecroticCarries(Carrier, Necrosis))
		|| !TestTrue(TEXT("set-up: the control's plain damage over time, 20 a tick for 5 s, is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Control, 20.0f, 5.0f, Plain,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	TestEqual(TEXT("the cleansed Necrosis's denied total has been taken to be dealt"),
			  DeniedSoFar(Carrier), 0.0f, 0.001f);

	CataclysmTestWorld::RunClock(World, 5.5f);
	const float CarrierLost = NecroticStart - NecroticHealthOf(Carrier);
	const float ControlLost = NecroticStart - NecroticHealthOf(Control);
	if (!TestTrue(FString::Printf(TEXT("control: the control's plain damage over time took %.3f"), ControlLost),
				  ControlLost > 0.0f))
	{
		return false;
	}
	TestEqual(FString::Printf(TEXT("the carrier lost the denied 100 as the control lost its 100 (%.3f against %.3f)"),
							  CarrierLost, ControlLost),
			  CarrierLost, ControlLost, 0.01f);
	TestEqual(TEXT("in five ticks"), Ticks.On(Carrier), 5);
	return true;
}

/**
 * A NECROSIS REPLACED BY A STRONGER ONE HAS NOT ENDED, AND ITS TOTAL KEEPS GROWING (Q4: "a refresh extends that
 * time"). A Necrosis of one a tick; a heal of 100 denied; a Necrosis of two a tick, which replaces it. Nothing is dealt
 * at the replacement: the total is still 100, and the carrier still carries a Necrosis. A heal of 50 is denied too, so
 * the total is 150. A cleanse ends it, and over 5.5 s the carrier loses what a control loses to a plain damage over
 * time of 30 a tick for 5 s, in five ticks: one payout of the whole 150.
 */
CATACLYSM_NECROSIS_DENIAL_TEST(FCataclysmNecrosisReplacedTest,
	"Cataclysm.Ailments.ANecrosisReplacedByAStrongerOneKeepsTheHealingItDenied")
{
	using namespace CataclysmNecrosisDenialTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();
	const FGameplayTag Plain = PlainDamageOverTimeTag();
	FNecrosisApplier Applier(World);
	ACataclysmEnemyCharacter* Carrier = NecroticCreatureAt(World, NecroticFirstAt);
	ACataclysmEnemyCharacter* Control = NecroticCreatureAt(World, NecroticSecondAt);
	if (!TestTrue(TEXT("set-up: the Necrosis and plain damage over time tags"), Necrosis.IsValid() && Plain.IsValid())
		|| !TestTrue(TEXT("set-up: the applier"), Applier.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, the carrier"), Carrier)
		|| !TestNotNull(TEXT("set-up: creature 1, the control"), Control)
		|| !TestTrue(TEXT("set-up: the first Necrosis, one a tick for 10 s, is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Carrier, 1.0f, 10.0f, Necrosis,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	HealThroughTopUp(Carrier, 100.0f);
	if (!TestEqual(TEXT("set-up: the heal of 100 is denied"), DeniedSoFar(Carrier), 100.0f, 0.001f)
		|| !TestEqual(TEXT("set-up: one damage over time runs on the carrier, the first Necrosis"),
			DamageOverTimeEffectsOn(Carrier), 1)
		|| !TestTrue(TEXT("set-up: the second Necrosis, two a tick for 10 s, is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Carrier, 2.0f, 10.0f, Necrosis,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	TestTrue(TEXT("the carrier still carries a Necrosis"), NecroticCarries(Carrier, Necrosis));
	TestEqual(TEXT("the replaced Necrosis has not ended, so its total of 100 is still owed"),
			  DeniedSoFar(Carrier), 100.0f, 0.001f);
	TestEqual(TEXT("and nothing was laid to deal it: one damage over time, the second Necrosis"),
			  DamageOverTimeEffectsOn(Carrier), 1);

	HealThroughTopUp(Carrier, 50.0f);
	TestEqual(TEXT("the stronger Necrosis denies healing too"), NecroticHealthOf(Carrier), NecroticStart, 0.001f);
	TestEqual(TEXT("and its total grows to 150"), DeniedSoFar(Carrier), 150.0f, 0.001f);

	FNecroticTickCount Ticks(World);
	if (!TestNotNull(TEXT("set-up: the hit notices"), Ticks.Events)
		|| !TestTrue(TEXT("set-up: the cleanse removes something"), UCataclysmDebuffs::Cleanse(Carrier) > 0)
		|| !TestFalse(TEXT("set-up: and the Necrosis is gone"), NecroticCarries(Carrier, Necrosis))
		|| !TestTrue(TEXT("set-up: the control's plain damage over time, 30 a tick for 5 s, is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Control, 30.0f, 5.0f, Plain,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}

	CataclysmTestWorld::RunClock(World, 5.5f);
	const float CarrierLost = NecroticStart - NecroticHealthOf(Carrier);
	const float ControlLost = NecroticStart - NecroticHealthOf(Control);
	if (!TestTrue(FString::Printf(TEXT("control: the control's plain damage over time took %.3f"), ControlLost),
				  ControlLost > 0.0f))
	{
		return false;
	}
	TestEqual(FString::Printf(TEXT("the carrier lost the whole 150 as the control lost its 150 (%.3f against %.3f)"),
							  CarrierLost, ControlLost),
			  CarrierLost, ControlLost, 0.01f);
	TestEqual(TEXT("in five ticks: one payout, not one per Necrosis"), Ticks.On(Carrier), 5);
	return true;
}

/**
 * A CARRIER THAT DIES BEFORE ITS NECROSIS ENDS TAKES NOTHING MORE (Q4). Two creatures each carry a Necrosis of one a
 * tick for 2 s and are each denied a heal of 100. The first is then marked dead, which leaves its effects running.
 * At 2.25 s both Necroses have run out and both totals are taken. The second, the control, is dealing its total: one
 * damage over time runs on it. The dead one has none.
 */
CATACLYSM_NECROSIS_DENIAL_TEST(FCataclysmNecrosisDeadCarrierTest,
	"Cataclysm.Ailments.ANecrosisOnACarrierThatDiedFirstDealsNothingMore")
{
	using namespace CataclysmNecrosisDenialTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();
	FNecrosisApplier Applier(World);
	ACataclysmEnemyCharacter* Dead = NecroticCreatureAt(World, NecroticFirstAt);
	ACataclysmEnemyCharacter* Living = NecroticCreatureAt(World, NecroticSecondAt);
	if (!TestTrue(TEXT("set-up: Keyword.DoT.Necrosis is a tag this build knows"), Necrosis.IsValid())
		|| !TestTrue(TEXT("set-up: the applier"), Applier.IsComplete())
		|| !TestNotNull(TEXT("set-up: creature 0, to die"), Dead)
		|| !TestNotNull(TEXT("set-up: creature 1, the control, living"), Living)
		|| !TestTrue(TEXT("set-up: creature 0's Necrosis is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Dead, 1.0f, 2.0f, Necrosis,
														/*bScalesWithInstigator=*/false))
		|| !TestTrue(TEXT("set-up: the control's Necrosis is laid"),
			UCataclysmSkillEffects::ApplyDamageOverTime(Applier.Character, Living, 1.0f, 2.0f, Necrosis,
														/*bScalesWithInstigator=*/false)))
	{
		return false;
	}
	HealThroughTopUp(Dead, 100.0f);
	HealThroughTopUp(Living, 100.0f);
	if (!TestEqual(TEXT("set-up: creature 0's heal of 100 is denied"), DeniedSoFar(Dead), 100.0f, 0.001f)
		|| !TestEqual(TEXT("set-up: and the control's"), DeniedSoFar(Living), 100.0f, 0.001f)
		|| !TestTrue(TEXT("set-up: creature 0 is marked dead"), UCataclysmSkillEffects::MarkDead(Dead))
		|| !TestTrue(TEXT("set-up: and its Necrosis is still on it"), NecroticCarries(Dead, Necrosis)))
	{
		return false;
	}

	CataclysmTestWorld::RunClock(World, 2.25f);
	if (!TestFalse(TEXT("set-up: the control's Necrosis has run out"), NecroticCarries(Living, Necrosis))
		|| !TestFalse(TEXT("set-up: and so has the dead one's"), NecroticCarries(Dead, Necrosis))
		|| !TestEqual(TEXT("control: the control's total has been taken"), DeniedSoFar(Living), 0.0f, 0.001f)
		|| !TestEqual(TEXT("control: and is being dealt: one damage over time runs on it"),
			DamageOverTimeEffectsOn(Living), 1))
	{
		return false;
	}
	TestEqual(TEXT("the dead carrier's total is emptied"), DeniedSoFar(Dead), 0.0f, 0.001f);
	TestEqual(TEXT("and nothing was laid to deal it: no damage over time runs on the body"),
			  DamageOverTimeEffectsOn(Dead), 0);
	return true;
}

#undef CATACLYSM_NECROSIS_DENIAL_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
