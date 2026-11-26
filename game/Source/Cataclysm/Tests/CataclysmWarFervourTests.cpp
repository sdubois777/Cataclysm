// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
// For the slot a skill trap and a skill gadget are placed for.
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWarFervour.h"
#include "AbilitySystem/CataclysmBerserkerStates.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * THE WAR CLASSES' FERVOUR. Slice C of `plan_death_and_war.md`, 2026-11-24: "A character in War trees alone cannot cast
 * an Ultimate", because no War tree filled Fervour and every Ultimate costs 50. `FCataclysmWarFervour` holds the seven
 * gains and the out-of-combat decay; each gain is called at its event's own site.
 *
 * WHAT IS EXERCISED THROUGH ITS REAL SITE: a hit taken and a block (`UCataclysmVitalAttributeSet`, a real blow through
 * `UCataclysmSkillEffects::ApplyHit` with the block roll pinned), a critical strike (`ACataclysmPlayerCharacter::
 * OnSomethingWasHit`, a hit notice announced on a real player), a skill trap and a skill gadget placed
 * (`ACataclysmMinion::PlaceSkillTrap` and `PlaceSkillGadget`, with the new `gadget_placed` event). WHAT IS NOT: a trap
 * triggering and a device's blow (`TriggerSkillTrap`, `AttackTarget`, `ApplyHit`'s `deployable_hit`), whose sites need a
 * weapon skill to cast and a War machine; their gains are exercised directly, and by the stat exemption probes.
 */

namespace CataclysmWarFervourTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** Sets a console variable and puts it back at scope exit. */
	struct FWarFervourPinned
	{
		FWarFervourPinned(const TCHAR* Name, float Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FWarFervourPinned()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Previous = 0.0f;
	};

	/** One flat stat line for `SetStatInputs`, optionally scoped to one tag. */
	struct FWarFervourStat
	{
		const TCHAR* Stat = nullptr;
		float Value = 0.0f;
		const TCHAR* RequiredTag = nullptr;
	};

	/** REPLACES THE WHOLE STAT LINE of `System` with these flat rows. */
	void HoldWarFervourStats(UCataclysmAbilitySystemComponent* System, const TArray<FWarFervourStat>& Rows)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const FWarFervourStat& Each : Rows)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = Each.Value;
			if (Each.RequiredTag)
			{
				Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(Each.RequiredTag), false));
			}
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Each.Stat));
			Line.Base = 0.0f;
			Line.Modifiers.Add(Row);
		}
		System->SetStatInputs(MoveTemp(Stats));
	}

	/** A bare actor holding the four combat sets and a class resource set, with a hundred thousand health. */
	struct FWarFervourFighter
	{
		FWarFervourFighter(UWorld* World, float AttackDamage)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), AttackDamage);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												   100.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
												   10.0f);
		}
		~FWarFervourFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}
		float Fervour() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}
		void SetFervour(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
												   Value);
		}
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A possessed player and its ability system. */
	struct FWarFervourPlayer
	{
		explicit FWarFervourPlayer(UWorld* World)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			System = State->GetCataclysmAbilitySystemComponent();
		}
		bool IsUsable() const { return Character && System; }
		float Fervour() const
		{
			return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}
		void Pool(float Maximum, float Held) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Maximum);
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Held);
		}
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** One blow of `Attacker`'s on `Defender` at 100% of its attack. */
	FCataclysmDamageResult WarFervourBlow(const FWarFervourFighter& Attacker, const FWarFervourFighter& Defender)
	{
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f, FGameplayTagContainer(),
										 FCataclysmHitDelivery(), &Result);
		return Result;
	}
}

#define CATACLYSM_WAR_FERVOUR_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The gains' one shape
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourGainShapeTest,
	"Cataclysm.WarFervour.EachGainIsItsRateScaledByGenerationAndClampedToWhatThePoolMayHold")
{
	using namespace CataclysmWarFervourTest;
	// EVERY ONE OF THE SEVEN, the shape `UCataclysmFervour::GainForEnemiesHit` keeps: nothing without its rate; its rate
	// times `class_resource_generation` over 100 (150 here, so 2 becomes 3); and never past the most the pool may hold.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarFervourFighter Holder(World, 0.0f);

	struct FGain
	{
		const TCHAR* Stat;
		TFunction<float(UAbilitySystemComponent*)> Gain;
	};
	const FGameplayTagContainer NoTags;
	const TArray<FGain> Gains = {
		{FCataclysmWarFervour::PerHitTakenStat, [](UAbilitySystemComponent* S) { return FCataclysmWarFervour::GainOnHitTaken(S); }},
		{FCataclysmWarFervour::PerBlockStat, [](UAbilitySystemComponent* S) { return FCataclysmWarFervour::GainOnBlock(S); }},
		{FCataclysmWarFervour::PerCriticalStrikeStat,
		 [NoTags](UAbilitySystemComponent* S) { return FCataclysmWarFervour::GainOnCriticalStrike(S, NoTags); }},
		{FCataclysmWarFervour::PerTrapPlacedStat,
		 [NoTags](UAbilitySystemComponent* S) { return FCataclysmWarFervour::GainOnTrapPlaced(S, NoTags); }},
		{FCataclysmWarFervour::PerGadgetPlacedStat,
		 [NoTags](UAbilitySystemComponent* S) { return FCataclysmWarFervour::GainOnGadgetPlaced(S, NoTags); }},
		{FCataclysmWarFervour::PerTrapTriggeredStat,
		 [NoTags](UAbilitySystemComponent* S) { return FCataclysmWarFervour::GainOnTrapTriggered(S, NoTags); }},
		{FCataclysmWarFervour::PerDeviceHitStat,
		 [NoTags](UAbilitySystemComponent* S) { return FCataclysmWarFervour::GainOnDeviceHit(S, NoTags); }},
	};
	for (const FGain& Each : Gains)
	{
		HoldWarFervourStats(Holder.AbilitySystem, {});
		Holder.SetFervour(10.0f);
		TestEqual(*FString::Printf(TEXT("%s: control, nothing is gained without the rate"), Each.Stat),
				  Each.Gain(Holder.AbilitySystem), 0.0f, 0.001f);

		HoldWarFervourStats(Holder.AbilitySystem,
							{{Each.Stat, 2.0f}, {UCataclysmAbilitySystemComponent::ClassResourceGenerationStat, 150.0f}});
		TestEqual(*FString::Printf(TEXT("%s: 2 at 150%% generation is 3"), Each.Stat),
				  Each.Gain(Holder.AbilitySystem), 3.0f, 0.001f);
		TestEqual(*FString::Printf(TEXT("%s: and the pool holds 13"), Each.Stat), Holder.Fervour(), 13.0f, 0.001f);

		Holder.SetFervour(99.0f);
		TestEqual(*FString::Printf(TEXT("%s: at 99 of 100 it gains only the 1 left"), Each.Stat),
				  Each.Gain(Holder.AbilitySystem), 1.0f, 0.001f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The decay
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourDecayTest,
	"Cataclysm.WarFervour.TheDecayWaitsThreeSecondsAfterTheLastBlowThenTakesItsRate")
{
	using namespace CataclysmWarFervourTest;
	// A HIT TAKEN OPENS COMBAT. A step one second later takes nothing, nor one at 2.9 seconds; at 3.5 seconds the step
	// of a second takes the rate, 10. THE CONTROL: without the rate nothing ever leaves.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Holder = World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
																					 FRotator::ZeroRotator);
	UCataclysmAbilitySystemComponent* System =
		Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a character with an ability system"), System))
	{
		return false;
	}
	Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	// THE CONTROL.
	HoldWarFervourStats(System, {});
	System->SetNumericAttributeBase(Pool, 50.0f);
	World->TimeSeconds += 10.0f;
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("control: without the rate, ten seconds out of combat take nothing"),
			  System->GetNumericAttribute(Pool), 50.0f, 0.001f);

	HoldWarFervourStats(System, {{FCataclysmWarFervour::OutOfCombatDecayPerSecondStat, 10.0f}});
	System->NoteHitTaken();
	World->TimeSeconds += 1.0f;
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("a second after a hit taken, nothing leaves"), System->GetNumericAttribute(Pool), 50.0f, 0.001f);
	World->TimeSeconds += 1.9f;
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("nor at 2.9 seconds"), System->GetNumericAttribute(Pool), 50.0f, 0.001f);
	World->TimeSeconds += 0.6f;
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("at 3.5 seconds a step of a second takes 10"), System->GetNumericAttribute(Pool), 40.0f, 0.001f);

	// AND A BLOW DEALT OPENS COMBAT TOO, so the decay stops again.
	System->NoteHitDealt();
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("a blow dealt stops it"), System->GetNumericAttribute(Pool), 40.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The Bulwark: a blow taken, and a blow blocked
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourBulwarkTest,
	"Cataclysm.WarFervour.ARealBlowTakenAndARealBlockFillABulwarksFervour")
{
	using namespace CataclysmWarFervourTest;
	// THROUGH THE DAMAGE PATH: a defender holding 1 a hit taken and 2 a block takes a blow that is not blocked and gains
	// 1; a blow that is blocked landed too, so it gains both, 3. THE CONTROL: the same blow on a defender holding neither
	// rate gains nothing.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarFervourPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);

	const FWarFervourFighter Attacker(World, 100.0f);
	const FWarFervourFighter Defender(World, 0.0f);
	Defender.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);

	{
		const FWarFervourPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
		const FCataclysmDamageResult Result = WarFervourBlow(Attacker, Defender);
		TestFalse(TEXT("set-up: the control blow was not blocked"), Result.bBlocked);
		TestEqual(TEXT("control: with no rate, a blow taken gains nothing"), Defender.Fervour(), 10.0f, 0.001f);
	}

	HoldWarFervourStats(Defender.AbilitySystem,
						{{FCataclysmWarFervour::PerHitTakenStat, 1.0f}, {FCataclysmWarFervour::PerBlockStat, 2.0f}});
	{
		const FWarFervourPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);
		const FCataclysmDamageResult Result = WarFervourBlow(Attacker, Defender);
		if (TestFalse(TEXT("set-up: the blow was not blocked"), Result.bBlocked))
		{
			TestEqual(TEXT("a blow taken gains 1"), Defender.Fervour(), 11.0f, 0.001f);
		}
	}
	World->TimeSeconds += 1.0f;
	{
		const FWarFervourPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
		const FCataclysmDamageResult Result = WarFervourBlow(Attacker, Defender);
		if (TestTrue(TEXT("set-up: the blow was blocked"), Result.bBlocked))
		{
			TestEqual(TEXT("a blocked blow landed, so it gains the hit's 1 and the block's 2"), Defender.Fervour(),
					  14.0f, 0.001f);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// The Berserker: a critical strike
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourBerserkerTest,
	"Cataclysm.WarFervour.ACriticalStrikeARealPlayerDealtFillsItsFervourAndAMeleeRowAddsToAMeleeOne")
{
	using namespace CataclysmWarFervourTest;
	// A HIT NOTICE ANNOUNCED AS THE GAME ANNOUNCES ONE, on a real possessed player that holds 1 a critical strike and 3
	// more on a melee one: a critical strike with no tags gains 1, a melee one 4. THE CONTROL: the same notice not
	// critical gains nothing.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWarFervourPlayer Player(World);
	ACataclysmEnemyCharacter* Enemy =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(300.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	const FGameplayTag Melee = FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")), false);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable())
		|| !TestNotNull(TEXT("set-up: an enemy"), Enemy)
		|| !TestNotNull(TEXT("set-up: the announcer"), Events)
		|| !TestTrue(TEXT("set-up: the melee tag exists"), Melee.IsValid()))
	{
		return false;
	}
	Enemy->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	HoldWarFervourStats(Player.System, {{FCataclysmWarFervour::PerCriticalStrikeStat, 1.0f},
										{FCataclysmWarFervour::PerCriticalStrikeStat, 3.0f, TEXT("Type.Melee")}});
	Player.Pool(100.0f, 10.0f);

	FGameplayTagContainer NoTags;
	FGameplayTagContainer MeleeTags;
	MeleeTags.AddTag(Melee);
	const auto Announce = [&](bool bCritical, const FGameplayTagContainer* Tags)
	{
		FCataclysmHitNotice Notice;
		Notice.Attacker = Player.Character;
		Notice.Target = Enemy;
		Notice.SkillTags = Tags;
		Notice.DealtToHealth = 50.0f;
		Notice.bCritical = bCritical;
		Events->OnHit.Broadcast(Notice);
	};

	Announce(/*bCritical=*/false, &NoTags);
	TestEqual(TEXT("control: a blow that is not critical gains nothing"), Player.Fervour(), 10.0f, 0.001f);
	Announce(/*bCritical=*/true, &NoTags);
	TestEqual(TEXT("a critical strike gains 1"), Player.Fervour(), 11.0f, 0.001f);
	Announce(/*bCritical=*/true, &MeleeTags);
	TestEqual(TEXT("a melee critical strike gains 1 and the melee row's 3"), Player.Fervour(), 15.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The Saboteur: a trap placed, and a gadget placed
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourSaboteurTest,
	"Cataclysm.WarFervour.PlacingASkillTrapAndASkillGadgetFillsASaboteurAndTheGadgetIsAnnounced")
{
	using namespace CataclysmWarFervourTest;
	// A REAL PLAYER HOLDING 2 A TRAP PLACED AND 3 A GADGET PLACED, at 20 of 100: the trap gains 2 and the gadget 3, each
	// after its reserve is held. The gadget's placing is announced as `gadget_placed`: an action on that event, written
	// by hand as an item's row would build it, restores 10% of the player's mana. THE CONTROL: a trap placed with no
	// rate gains nothing.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FWarFervourPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	HoldWarFervourStats(Player.System, {});
	Player.Pool(100.0f, 20.0f);
	ACataclysmMinion* First =
		ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy, FVector(300.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the control trap is placed"), First))
	{
		return false;
	}
	TestEqual(TEXT("control: with no rate, a trap placed gains nothing"), Player.Fervour(), 20.0f, 0.001f);

	HoldWarFervourStats(Player.System, {{FCataclysmWarFervour::PerTrapPlacedStat, 2.0f},
										{FCataclysmWarFervour::PerGadgetPlacedStat, 3.0f}});
	Player.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
	Player.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 0.0f);
	FCataclysmPoolAction OnGadget;
	OnGadget.Event = FName(TEXT("gadget_placed"));
	OnGadget.Pool = FName(TEXT("mana"));
	OnGadget.Percent = 10.0f;
	OnGadget.TriggerKey = FName(TEXT("A_row:mana:gadget_placed"));
	Player.System->SetPoolActions({OnGadget});

	ACataclysmMinion* Trap =
		ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy, FVector(300.0f, 200.0f, 0.0f));
	if (TestNotNull(TEXT("set-up: the second trap is placed"), Trap))
	{
		TestEqual(TEXT("a trap placed gains 2"), Player.Fervour(), 22.0f, 0.001f);
	}
	TestEqual(TEXT("and a trap's placing is not a gadget's: no mana"),
			  Player.System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute()), 0.0f, 0.001f);

	ACataclysmMinion* Gadget = ACataclysmMinion::PlaceSkillGadget(Player.Character, ECataclysmAbilitySlot::Special,
																	FVector(300.0f, 400.0f, 0.0f));
	if (TestNotNull(TEXT("set-up: the gadget is placed"), Gadget))
	{
		TestEqual(TEXT("a gadget placed gains 3"), Player.Fervour(), 25.0f, 0.001f);
		TestEqual(TEXT("and is announced: the action on gadget_placed restored 10% of 1,000 mana"),
				  Player.System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute()), 100.0f, 0.001f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// The starting nodes' rows, as the game loads them
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourRowsTest,
	"Cataclysm.WarFervour.TheThreeWarStartingNodesGrantTheirFervourRates")
{
	using namespace CataclysmWarFervourTest;
	// THE GENERATED TABLE, as the game loads it: the Bulwark's 1 a hit taken, 1 a block and 5 a second out of combat;
	// the Berserker's 1 a critical strike and 10 a second; the Saboteur's 1 for each of its four.
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effect table loads"), Effects))
	{
		return false;
	}
	const auto Flat = [Effects](const TCHAR* Node, const TCHAR* Stat)
	{
		for (const FCataclysmPassiveEffectRow* Effect : UCataclysmPassiveTree::EffectsFor(Effects, FName(Node)))
		{
			if (Effect && Effect->Stat == Stat && Effect->ValueKind.Equals(TEXT("flat")) && Effect->Condition.IsEmpty()
				&& Effect->Scale.IsEmpty() && Effect->MinPoints == 0)
			{
				return Effect->ValuePerPoint;
			}
		}
		return -1.0f;
	};
	struct FExpected
	{
		const TCHAR* Node;
		const TCHAR* Stat;
		float Value;
	};
	const TArray<FExpected> Expected = {
		{TEXT("Bulwark_basic_trunk_000"), FCataclysmWarFervour::PerHitTakenStat, 1.0f},
		{TEXT("Bulwark_basic_trunk_000"), FCataclysmWarFervour::PerBlockStat, 1.0f},
		{TEXT("Bulwark_basic_trunk_000"), FCataclysmWarFervour::OutOfCombatDecayPerSecondStat, 5.0f},
		// AND 1 A KILL since the final bundle, 2026-11-26, on the Death line's kill generator.
		{TEXT("Bulwark_basic_trunk_000"), UCataclysmFervour::PerKillStat, 1.0f},
		{TEXT("Berserker_trunk_000"), FCataclysmWarFervour::PerCriticalStrikeStat, 1.0f},
		{TEXT("Berserker_trunk_000"), FCataclysmWarFervour::OutOfCombatDecayPerSecondStat, 10.0f},
		{TEXT("Saboteur_basic_trunk_000"), FCataclysmWarFervour::PerTrapPlacedStat, 1.0f},
		{TEXT("Saboteur_basic_trunk_000"), FCataclysmWarFervour::PerGadgetPlacedStat, 1.0f},
		{TEXT("Saboteur_basic_trunk_000"), FCataclysmWarFervour::PerTrapTriggeredStat, 1.0f},
		{TEXT("Saboteur_basic_trunk_000"), FCataclysmWarFervour::PerDeviceHitStat, 1.0f},
	};
	for (const FExpected& Each : Expected)
	{
		const float Found = Flat(Each.Node, Each.Stat);
		if (!TestEqual(*FString::Printf(TEXT("%s grants %s %.0f"), Each.Node, Each.Stat, Each.Value), Found, Each.Value,
					   0.001f)
			&& Found < 0.0f)
		{
			AddError(TEXT("If the rows are in game/Data/PassiveEffects.csv, DT_PassiveEffects is older than they are: run  "
						  "python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// The Bulwark's Fervour for a kill, 2026-11-26 (the final bundle)
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourBulwarkKillTest,
	"Cataclysm.WarFervour.ABulwarkIsPaidOneFervourForAKillAtEveryRarityThroughTheRealRow")
{
	using namespace CataclysmWarFervourTest;
	// THE REAL ROWS: the Bulwark's starting node read out of `DT_PassiveEffects` through `ModifiersFor`, as the game does,
	// and handed to a fighter at 10 Fervour of 100. A kill is paid by the Death line's generator,
	// `UCataclysmFervour::GainOnOwnKill`, which writes through `RaisePool`: 1 for a common creature and 1 for a Boss, the
	// sentence stating no rarity. THE CONTROL: a fighter holding the Berserker's starting node, which states no kill
	// Fervour, is paid nothing for the same kill.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const auto RealLines = [](const TCHAR* Node)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), 1);
		TMap<FName, FCataclysmStatInputs> Lines;
		for (const TPair<FName, TArray<FCataclysmStatModifier>>& Row : UCataclysmPassiveTree::ModifiersFor(
				 Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(),
				 {FName(TEXT("War"))}))
		{
			Lines.FindOrAdd(Row.Key).Modifiers = Row.Value;
		}
		return Lines;
	};
	const auto CreatureOfRung = [World](int32 Rung, float Y)
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(800.0f + 300.0f * Rung, Y, 0.0f), FRotator::ZeroRotator);
		if (Creature)
		{
			Creature->SetRarityStep(Rung);
		}
		return Creature;
	};

	TMap<FName, FCataclysmStatInputs> BulwarkLines = RealLines(TEXT("Bulwark_basic_trunk_000"));
	if (!TestTrue(TEXT("set-up: the Bulwark's starting node carries fervour_per_kill"),
				  BulwarkLines.Contains(FName(UCataclysmFervour::PerKillStat))))
	{
		AddError(TEXT("If the row is in game/Data/PassiveEffects.csv, DT_PassiveEffects is older than it is: run  "
					  "python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		return false;
	}
	const FWarFervourFighter Bulwark(World, 100.0f);
	Bulwark.AbilitySystem->SetStatInputs(MoveTemp(BulwarkLines));
	for (const int32 Rung : {0, 4})
	{
		ACataclysmEnemyCharacter* Slain = CreatureOfRung(Rung, 0.0f);
		if (!TestNotNull(*FString::Printf(TEXT("set-up: a creature of rung %d"), Rung), Slain))
		{
			return false;
		}
		Bulwark.SetFervour(10.0f);
		const float Paid = UCataclysmFervour::GainOnOwnKill(Bulwark.AbilitySystem, Slain);
		TestEqual(*FString::Printf(TEXT("a kill of rung %d pays the Bulwark 1"), Rung), Paid, 1.0f, 0.001f);
		TestEqual(*FString::Printf(TEXT("and the pool holds it: 10 to 11, rung %d"), Rung), Bulwark.Fervour(), 11.0f,
				  0.001f);
	}

	TMap<FName, FCataclysmStatInputs> BerserkerLines = RealLines(TEXT("Berserker_trunk_000"));
	if (!TestFalse(TEXT("set-up: the Berserker's starting node carries no fervour_per_kill"),
				   BerserkerLines.Contains(FName(UCataclysmFervour::PerKillStat))))
	{
		return false;
	}
	const FWarFervourFighter Berserker(World, 100.0f);
	Berserker.AbilitySystem->SetStatInputs(MoveTemp(BerserkerLines));
	Berserker.SetFervour(10.0f);
	ACataclysmEnemyCharacter* Control = CreatureOfRung(0, 600.0f);
	TestEqual(TEXT("control: a Berserker is paid nothing for the same kill"),
			  UCataclysmFervour::GainOnOwnKill(Berserker.AbilitySystem, Control), 0.0f, 0.001f);
	TestEqual(TEXT("control: and its pool stays at 10"), Berserker.Fervour(), 10.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The Fervour spends, 2026-11-25
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourIronWillTest,
	"Cataclysm.WarFervour.ANegatedBlockPaysIronWillsCostAndABlockThatTookAShareDoesNot")
{
	using namespace CataclysmWarFervourTest;
	// IRON WILL THROUGH THE DAMAGE PATH: a defender at 50 Fervour holding a negation chance of 100 and a cost of 20 blocks a
	// real blow, which is negated and costs 20. THE CONTROL: the cost alone, with no negation, on a block that took only
	// its share, costs nothing. And a cost above what the pool holds takes what there is.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarFervourPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FWarFervourPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);

	const FWarFervourFighter Attacker(World, 100.0f);
	const FWarFervourFighter Defender(World, 0.0f);
	Defender.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);

	HoldWarFervourStats(Defender.AbilitySystem, {{FCataclysmWarFervour::CostPerNegatedBlockStat, 20.0f}});
	Defender.SetFervour(50.0f);
	{
		const FCataclysmDamageResult Result = WarFervourBlow(Attacker, Defender);
		if (TestTrue(TEXT("set-up: the control blow was blocked"), Result.bBlocked))
		{
			TestFalse(TEXT("and not negated: no negation chance is held"), Result.bNegated);
			TestEqual(TEXT("control: a block that took its share costs nothing"), Defender.Fervour(), 50.0f, 0.001f);
		}
	}

	HoldWarFervourStats(Defender.AbilitySystem, {{TEXT("block_negation_chance"), 100.0f},
												 {FCataclysmWarFervour::CostPerNegatedBlockStat, 20.0f}});
	World->TimeSeconds += 1.0f;
	{
		const FCataclysmDamageResult Result = WarFervourBlow(Attacker, Defender);
		if (TestTrue(TEXT("set-up: the blow was negated"), Result.bNegated))
		{
			TestEqual(TEXT("a negated block costs 20"), Defender.Fervour(), 30.0f, 0.001f);
			TestEqual(TEXT("and nothing reached health"), Result.DealtToHealth, 0.0f, 0.001f);
		}
	}

	Defender.SetFervour(5.0f);
	World->TimeSeconds += 1.0f;
	WarFervourBlow(Attacker, Defender);
	TestEqual(TEXT("a cost of 20 with 5 held takes the 5"), Defender.Fervour(), 0.0f, 0.001f);
	return true;
}

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourUndyingTest,
	"Cataclysm.WarFervour.UndyingSpendsThePoolOnALethalBlowAndRestoresFivePercentForEachTen")
{
	using namespace CataclysmWarFervourTest;
	// UNDYING THROUGH THE DAMAGE PATH: a defender at 50 of 1,000 health and 73 Fervour, holding 5% for each 10, takes a
	// blow of 100. It is left 1, the 73 are spent, and seven whole steps of 50 come back: 351. THE CONTROL: the same blow
	// on a defender without the row empties its health.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarFervourPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FWarFervourPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);

	const FWarFervourFighter Attacker(World, 100.0f);
	const auto Health = [](const FWarFervourFighter& Who)
	{
		return Who.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	};
	const auto Weaken = [](const FWarFervourFighter& Who)
	{
		Who.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		Who.AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 50.0f);
		Who.SetFervour(73.0f);
	};

	const FWarFervourFighter Unsaved(World, 0.0f);
	Weaken(Unsaved);
	WarFervourBlow(Attacker, Unsaved);
	TestEqual(TEXT("control: without the row the blow empties health"), Health(Unsaved), 0.0f, 0.001f);
	TestEqual(TEXT("control: and spends no Fervour"), Unsaved.Fervour(), 73.0f, 0.001f);

	const FWarFervourFighter Saved(World, 0.0f);
	Weaken(Saved);
	HoldWarFervourStats(Saved.AbilitySystem, {{FCataclysmWarFervour::LethalBlowHealthPerTenFervourStat, 5.0f}});
	WarFervourBlow(Attacker, Saved);
	TestEqual(TEXT("Undying spends the whole pool"), Saved.Fervour(), 0.0f, 0.001f);
	TestEqual(TEXT("and leaves 1 health with seven steps of 5% of 1,000 restored"), Health(Saved), 351.0f, 0.001f);
	return true;
}

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourCleaveTest,
	"Cataclysm.WarFervour.CleavePaysForEachEnemyBeyondTheFirstAndGainsForEachPaid")
{
	using namespace CataclysmWarFervourTest;
	// CLEAVE'S TRADE, with its rows on melee: a cost of 5 and a gain of 3 for each paid. Three enemies struck with 20 held
	// pay twice and gain twice, 16. THE CONTROLS: one enemy struck, and three struck by an attack that is not melee, change
	// nothing; and 4 held cannot pay for the first extra enemy, so nothing is paid or gained.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarFervourFighter Holder(World, 0.0f);
	HoldWarFervourStats(Holder.AbilitySystem, {{FCataclysmWarFervour::CostPerExtraEnemyHitStat, 5.0f, TEXT("Type.Melee")},
											   {FCataclysmWarFervour::GainPerExtraEnemyHitPaidStat, 3.0f,
												TEXT("Type.Melee")}});
	FGameplayTagContainer Melee;
	Melee.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")), false));
	if (!TestEqual(TEXT("set-up: the melee tag exists"), Melee.Num(), 1))
	{
		return false;
	}

	Holder.SetFervour(20.0f);
	FCataclysmWarFervour::TradeForEnemiesStruckTogether(Holder.AbilitySystem, Melee, 1);
	TestEqual(TEXT("control: one enemy struck changes nothing"), Holder.Fervour(), 20.0f, 0.001f);
	FCataclysmWarFervour::TradeForEnemiesStruckTogether(Holder.AbilitySystem, FGameplayTagContainer(), 3);
	TestEqual(TEXT("control: an attack that is not melee changes nothing"), Holder.Fervour(), 20.0f, 0.001f);
	TestEqual(TEXT("three enemies struck by a melee attack: two paid at 5, two gained at 3"),
			  FCataclysmWarFervour::TradeForEnemiesStruckTogether(Holder.AbilitySystem, Melee, 3), -4.0f, 0.001f);
	TestEqual(TEXT("which leaves 16"), Holder.Fervour(), 16.0f, 0.001f);

	Holder.SetFervour(4.0f);
	FCataclysmWarFervour::TradeForEnemiesStruckTogether(Holder.AbilitySystem, Melee, 3);
	TestEqual(TEXT("4 held cannot pay for the first extra enemy, so nothing is paid or gained"), Holder.Fervour(), 4.0f,
			  0.001f);
	return true;
}

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourBloodTitheTest,
	"Cataclysm.WarFervour.BloodTitheTakesFervourForEachWholeStepOfHealthRegenerated")
{
	using namespace CataclysmWarFervourTest;
	// BLOOD TITHE THROUGH THE HEAL'S OWN ROUTE, `UCataclysmRegeneration::TopUp`: 5 Fervour for each 100 health regenerated.
	// 250 regenerated takes 10 and carries 50; 60 more makes a third step and takes 5. THE CONTROL: 500 restored by a heal
	// that is not regeneration takes nothing.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarFervourFighter Holder(World, 0.0f);
	HoldWarFervourStats(Holder.AbilitySystem, {{FCataclysmWarFervour::LostToRegenerationStat, 5.0f},
											   {FCataclysmWarFervour::RegenerationPerLossStat, 100.0f}});
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute Maximum = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	FGameplayTagContainer Regenerating;
	Regenerating.AddTag(UCataclysmFervour::RegenerationTag());
	Holder.AbilitySystem->SetNumericAttributeBase(Health, 1000.0f);
	Holder.SetFervour(50.0f);

	UCataclysmRegeneration::TopUp(*Holder.AbilitySystem, Health, Maximum, 500.0f, FGameplayTagContainer());
	TestEqual(TEXT("control: 500 restored by a heal that is not regeneration takes nothing"), Holder.Fervour(), 50.0f,
			  0.001f);

	UCataclysmRegeneration::TopUp(*Holder.AbilitySystem, Health, Maximum, 250.0f, Regenerating);
	TestEqual(TEXT("250 regenerated takes two steps of 5"), Holder.Fervour(), 40.0f, 0.001f);
	UCataclysmRegeneration::TopUp(*Holder.AbilitySystem, Health, Maximum, 60.0f, Regenerating);
	TestEqual(TEXT("and 60 more, with the 50 carried, a third"), Holder.Fervour(), 35.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// Riposte, Vengeful Discharge and Crimson Tide, 2026-11-25
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourRiposteTest,
	"Cataclysm.WarFervour.RiposteCountersABlockAtTwentyFiveFervourAndStunsAtTheMaximum")
{
	using namespace CataclysmWarFervourTest;
	// RIPOSTE THROUGH THE DAMAGE PATH: a defender holding 75% of the damage blocked, a cost of 25 and a stun of 1 second
	// (the share as 25 Fervour or more would grant it; the stun as a full pool would) blocks real blows. THE CONTROL: with
	// no share, a block deals nothing back and spends nothing. With the share, the attacker loses health and 25 are spent;
	// with the stun beside it, the attacker is stunned too.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarFervourPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FWarFervourPinned AlwaysBlocks(TEXT("Cataclysm.BlockRoll"), 0.0f);
	const FWarFervourPinned NeverNegates(TEXT("Cataclysm.BlockNegationRoll"), 100.0f);

	const FWarFervourFighter Attacker(World, 100.0f);
	const FWarFervourFighter Defender(World, 0.0f);
	Defender.AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);
	const auto AttackersHealth = [&Attacker]()
	{
		return Attacker.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	};

	HoldWarFervourStats(Defender.AbilitySystem, {{FCataclysmWarFervour::RiposteFervourCostStat, 25.0f}});
	Defender.SetFervour(50.0f);
	const float Untouched = AttackersHealth();
	{
		const FCataclysmDamageResult Result = WarFervourBlow(Attacker, Defender);
		if (TestTrue(TEXT("set-up: the control blow was blocked"), Result.bBlocked))
		{
			TestEqual(TEXT("control: with no share nothing is dealt back"), AttackersHealth(), Untouched, 0.001f);
			TestEqual(TEXT("control: and nothing is spent"), Defender.Fervour(), 50.0f, 0.001f);
		}
	}

	HoldWarFervourStats(Defender.AbilitySystem, {{FCataclysmWarFervour::RiposteShareOfBlockedStat, 75.0f},
												 {FCataclysmWarFervour::RiposteFervourCostStat, 25.0f}});
	World->TimeSeconds += 1.0f;
	{
		const FCataclysmDamageResult Result = WarFervourBlow(Attacker, Defender);
		if (TestTrue(TEXT("set-up: the blow was blocked and removed something"), Result.DamageBlocked > 0.0f))
		{
			TestTrue(TEXT("Riposte deals the attacker damage"), AttackersHealth() < Untouched);
			TestEqual(TEXT("and spends 25"), Defender.Fervour(), 25.0f, 0.001f);
			TestFalse(TEXT("with no stun held, the attacker is not stunned"), UCataclysmSkillEffects::IsStunned(Attacker.Actor));
		}
	}

	HoldWarFervourStats(Defender.AbilitySystem, {{FCataclysmWarFervour::RiposteShareOfBlockedStat, 75.0f},
												 {FCataclysmWarFervour::RiposteFervourCostStat, 25.0f},
												 {FCataclysmWarFervour::RiposteStunSecondsAtMaximumStat, 1.0f}});
	World->TimeSeconds += 1.0f;
	WarFervourBlow(Attacker, Defender);
	TestTrue(TEXT("with the stun held, the attacker is stunned"), UCataclysmSkillEffects::IsStunned(Attacker.Actor));
	return true;
}

namespace CataclysmWarFervourTest
{
	/** A character with a pool of 100 holding 50, 10,000 maximum health and 1,000 health, on the players' team. */
	ACataclysmEnemyCharacter* SpendsCharacter(FAutomationTestBase& Test, UWorld* World)
	{
		ACataclysmEnemyCharacter* Holder =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		UCataclysmAbilitySystemComponent* System =
			Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
		if (!Test.TestNotNull(TEXT("set-up: a character with an ability system"), System))
		{
			return nullptr;
		}
		Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 50.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		return Holder;
	}
}

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourVengefulDischargeTest,
	"Cataclysm.WarFervour.VengefulDischargesDrainPulsesOnTheLastBlowTakenWhenItEmptiesThePool")
{
	using namespace CataclysmWarFervourTest;
	// VENGEFUL DISCHARGE'S PULSE: a holder draining 100 a second with 50% retaliation and a radius of 4 empties its pool
	// in one step. THE CONTROL: with no blow remembered this floor the pulse is sized at nothing, so a creature a metre
	// away is untouched. With a blow of 100 remembered, the same emptying strikes it.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmEnemyCharacter* Holder = SpendsCharacter(*this, World);
	ACataclysmEnemyCharacter* Creature =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector(100.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	UAbilitySystemComponent* Its = Creature ? Creature->GetAbilitySystemComponent() : nullptr;
	if (!Holder || !TestNotNull(TEXT("set-up: a creature with an ability system"), Its))
	{
		return false;
	}
	Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
	Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
	UCataclysmAbilitySystemComponent* System = Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
	HoldWarFervourStats(System, {{FCataclysmWarFervour::PulseDrainPerSecondStat, 100.0f},
								 {TEXT("retaliation"), 50.0f},
								 {FCataclysmWarFervour::PulseRadiusMetresStat, 4.0f}});
	const auto CreaturesHealth = [Its]()
	{
		return Its->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	};

	FCataclysmWarFervour::RetaliationPulseDrainStep(Holder, 1.0f);
	TestEqual(TEXT("set-up: the drain emptied the pool"),
			  System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()), 0.0f, 0.001f);
	TestEqual(TEXT("control: with no blow taken this floor the pulse strikes nothing"), CreaturesHealth(), 1000.0f,
			  0.001f);

	System->NoteBlowTakenForRetaliation(100.0f);
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 50.0f);
	FCataclysmWarFervour::RetaliationPulseDrainStep(Holder, 1.0f);
	TestTrue(TEXT("with a blow of 100 remembered, the emptying drain strikes the creature a metre away"),
			 CreaturesHealth() < 1000.0f);
	return true;
}

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourCrimsonTideTest,
	"Cataclysm.WarFervour.CrimsonTideHealsForFervourDrainedAndNotForFervourDecayed")
{
	using namespace CataclysmWarFervourTest;
	// CRIMSON TIDE, 4% of maximum health for each 10 spent or drained (its value at 8 points). THE CONTROL: 20 Fervour lost
	// to the out-of-combat decay heals nothing (the coordinating session's ruling: decay does not count). 20 drained by
	// the Berserker states' drain heals two steps of 4% of 10,000.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmEnemyCharacter* Holder = SpendsCharacter(*this, World);
	if (!Holder)
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* System = Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
	const auto Health = [System]()
	{
		return System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
	};
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	HoldWarFervourStats(System, {{FCataclysmWarFervour::HealthPerTenFervourSpentStat, 4.0f},
								 {FCataclysmWarFervour::OutOfCombatDecayPerSecondStat, 20.0f}});
	World->TimeSeconds += 10.0f;
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("set-up: the decay took 20"), System->GetNumericAttribute(Pool), 30.0f, 0.001f);
	TestEqual(TEXT("control: Fervour decayed heals nothing"), Health(), 1000.0f, 0.001f);

	HoldWarFervourStats(System, {{FCataclysmWarFervour::HealthPerTenFervourSpentStat, 4.0f},
								 {FCataclysmBerserkerStates::DrainPerSecondStat, 20.0f}});
	FCataclysmBerserkerStates::DrainStep(Holder, 1.0f);
	TestEqual(TEXT("set-up: the drain took 20"), System->GetNumericAttribute(Pool), 10.0f, 0.001f);
	TestEqual(TEXT("20 drained heals two steps of 4% of 10,000"), Health(), 1800.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// The decay meets Miser's Grip's floor (the Death trees' bundle 3 merge)
// ---------------------------------------------------------------------------

CATACLYSM_WAR_FERVOUR_TEST(FCataclysmWarFervourDecayFloorTest,
	"Cataclysm.WarFervour.TheDecayOutOfCombatStopsAtMisersGripsFloor")
{
	using namespace CataclysmWarFervourTest;
	// OBLIGATION 17 OF THE BUNDLE 3 MERGE (2026-11-25): Miser's Grip, "Your Fervour never decays below 3% of your
	// maximum Fervour per point", stops the War classes' decay as it stops the Death classes'. A floor of 40 on a bar of
	// 100: from 45, a step of a second at 10 takes 5, and the next takes nothing. THE CONTROL: without the floor, the
	// same step takes the whole 10.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmEnemyCharacter* Holder = World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
																					 FRotator::ZeroRotator);
	UCataclysmAbilitySystemComponent* System =
		Holder ? Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a character with an ability system"), System))
	{
		return false;
	}
	Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	System->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Holder));
	System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();

	// OUT OF COMBAT: a hit taken, then 5 seconds, past the 3 the decay waits.
	System->NoteHitTaken();
	World->TimeSeconds += 5.0f;

	// THE CONTROL.
	HoldWarFervourStats(System, {{FCataclysmWarFervour::OutOfCombatDecayPerSecondStat, 10.0f}});
	System->SetNumericAttributeBase(Pool, 45.0f);
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("control: without the floor a step of a second takes 10"), System->GetNumericAttribute(Pool), 35.0f,
			  0.001f);

	HoldWarFervourStats(System, {{FCataclysmWarFervour::OutOfCombatDecayPerSecondStat, 10.0f},
								 {UCataclysmFervour::DecayFloorPercentStat, 40.0f}});
	System->SetNumericAttributeBase(Pool, 45.0f);
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("with a floor of 40%, the step stops at 40"), System->GetNumericAttribute(Pool), 40.0f, 0.001f);
	FCataclysmWarFervour::OutOfCombatDecayStep(Holder, 1.0f);
	TestEqual(TEXT("and the next takes nothing"), System->GetNumericAttribute(Pool), 40.0f, 0.001f);
	return true;
}

#undef CATACLYSM_WAR_FERVOUR_TEST

#endif // WITH_AUTOMATION_TESTS
