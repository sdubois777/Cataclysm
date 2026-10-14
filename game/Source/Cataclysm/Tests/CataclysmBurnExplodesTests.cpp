// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * Ruled 2026-10-13 under the owner's delegation, for "Your burn effects have a 20%-40% chance to explode for double
 * their remaining damage when they expire": WHEN A BURN ENDS BY TIME IT HAS A CHANCE TO EXPLODE FOR DOUBLE THE DAMAGE
 * IT WAS LAID TO DEAL OVER ITS WHOLE DURATION, on its carrier and on every enemy of its applier within 5 metres of
 * the carrier. No row is authored: the wearer's stat line is written by hand, as the row would write it.
 *
 * THE BURN IS REAL AND SO IS ITS END. It is laid by `UCataclysmSkillEffects::ApplyDamageOverTime`, ten a tick for
 * four seconds with none of the applier's stats applied, and the world's clock is moved by
 * `CataclysmTestWorld::RunClock`, which fires the engine's own period and duration timers. Nothing calls the
 * explosion: the engine's notice of the effect's removal does.
 *
 * NO FIGURE HERE ASSUMES A CREATURE'S DEFENCES ARE NOUGHT. What a blow of the explosion's size takes is measured
 * first, on a creature 40 m from everything, with a direct blow delivered as the explosion's is. What one tick
 * takes is read off the carrier's own first ticks. Every expected figure is a difference from one of those.
 *
 * THE WEARER STANDS 20 M FROM EVERY CREATURE, so nothing here depends on what a creature or the player's automatic
 * attack does at close range, and each scene checks at its end that the wearer is alive.
 */
namespace CataclysmBurnExplodesTest
{
	constexpr float M = 100.0f;

	/** What the Burn of every test states: ten a tick, for four seconds, at one tick a second. */
	constexpr float PerTick = 10.0f;
	constexpr float BurnSeconds = 4.0f;

	/** So its whole stated damage is 40, and its explosion is a blow of 80 before a creature's mitigation. */
	constexpr float Whole = PerTick * BurnSeconds;
	constexpr float Explosion = 2.0f * Whole;

	/** The health every creature that is meant to live is given. Read back and never assumed. */
	constexpr float Plenty = 1000.0f;

	/** Pins a console variable by name for as long as it is in scope, at the console's own priority. */
	struct FRollPin
	{
		FRollPin(const TCHAR* Name, float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		~FRollPin()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		void Set(float Roll) const
		{
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		bool Found() const { return Variable != nullptr; }

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	const TCHAR* const RollName = TEXT("Cataclysm.DotExplodesAtExpiryRoll");

	/** A player character on a player state at the origin, with health to spare and an attack damage of 100. */
	struct FWearer
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		explicit FWearer(UWorld* World)
		{
			ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
			AbilitySystem = PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (Character && PlayerState && AbilitySystem)
			{
				Character->SetPlayerState(PlayerState);
				Character->OnRep_PlayerState();
				AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(),
													   100.0f);
				AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(),
													   100000.0f);
				AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			}
		}

		bool IsComplete() const { return Character && AbilitySystem; }

		bool IsAlive() const { return IsValid(Character) && !UCataclysmSkillEffects::IsDead(Character); }
	};

	/** A creature of the monsters' team standing here, with this maximum health and full. */
	ACataclysmEnemyCharacter* CreatureAt(UWorld* World, const FVector& At, float Health = Plenty)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(Health);
		}
		return Made;
	}

	/** The health an actor holds, or nought for one that is gone. */
	float HealthOf(const AActor* Who)
	{
		const UAbilitySystemComponent* System = IsValid(Who) ? UCataclysmTargeting::AbilitySystemOf(Who) : nullptr;
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	bool IsGone(const AActor* Who) { return !IsValid(Who) || UCataclysmSkillEffects::IsDead(Who); }

	bool Burns(const AActor* Who)
	{
		return IsValid(Who) && UCataclysmSkillEffects::HasTag(Who, UCataclysmSkillEffects::BurnTag());
	}

	/**
	 * The wearer's stat line, written by hand. The chance is one flat line requiring `Keyword.DoT.Burn`, which is
	 * the row `docs/DECISIONS.md` gives the session that writes it. Every other stat is one flat line with no tag.
	 * A chance of nought writes no line for it. `SetStatInputs` replaces the whole stat line.
	 */
	void Hold(UCataclysmAbilitySystemComponent* System, float Chance,
			  const TMap<FName, float>& Others = TMap<FName, float>())
	{
		TMap<FName, FCataclysmStatInputs> Inputs;
		if (Chance > 0.0f)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Chance;
			Row.RequiredTags.AddTag(UCataclysmSkillEffects::BurnTag());
			FCataclysmStatInputs& Line =
				Inputs.FindOrAdd(FName(UCataclysmSkillEffects::DotExplodesAtExpiryChanceStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Row};
		}
		for (const TPair<FName, float>& Other : Others)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::PassiveKeystone;
			Flat.Value = Other.Value;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(Other.Key);
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		}
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** The Burn of every test, or one of another figure a tick, laid by `Applier` with none of its stats applied. */
	bool LayBurn(AActor* Applier, AActor* Target, float DamagePerTick = PerTick)
	{
		return UCataclysmSkillEffects::ApplyDamageOverTime(Applier, Target, DamagePerTick, BurnSeconds,
														   UCataclysmSkillEffects::BurnTag(),
														   /*bScalesWithInstigator=*/false);
	}

	/** A direct blow delivered as the explosion delivers its own, for measuring what such a blow takes. */
	FCataclysmHitDelivery AsTheExplosionDelivers()
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		Delivery.bCannotBeRetaliatedAgainst = true;
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bCarriesNoWeaponSubType = true;
		Delivery.bCannotLeech = true;
		Delivery.bCarriesNoAilmentChance = true;
		Delivery.bIsAnExplosionAtAnAilmentsEnd = true;
		return Delivery;
	}

	/** The health `Creature` loses to one blow of `Damage` by the wearer, delivered as the explosion's is. */
	float WhatSuchABlowTakes(const FWearer& Wearer, ACataclysmEnemyCharacter* Creature, float Damage)
	{
		const float Before = HealthOf(Creature);
		UCataclysmSkillEffects::ApplyDirectDamage(Wearer.Character, Creature, Damage, AsTheExplosionDelivers());
		return Before - HealthOf(Creature);
	}

	/**
	 * Hears every blow and every death of a world for as long as it is in scope.
	 *
	 * `OnWatched` holds one letter for each blow announced on the watched character, in the order announced: T for
	 * a tick of damage over time, E for a blow marked as the explosion at an ailment's end, H for any other.
	 * `Explosions` counts the blows marked E on anybody, and `ExplosionsNotTheWearers` those of them credited to
	 * somebody other than `Wearer`.
	 */
	struct FListening
	{
		FListening(UCataclysmCombatEvents* InEvents, const AActor* InWatched, const AActor* InWearer)
			: Events(InEvents), Watched(InWatched), Wearer(InWearer)
		{
			if (!Events)
			{
				return;
			}
			HitHandle = Events->OnHit.AddLambda([this](const FCataclysmHitNotice& Notice)
			{
				if (Notice.bExplosionAtAnAilmentsEnd)
				{
					++Explosions;
					if (Notice.Attacker != Wearer)
					{
						++ExplosionsNotTheWearers;
					}
					if (!Notice.bArea || Notice.bCritical || Notice.bDamageOverTime)
					{
						++ExplosionsNotDeliveredAsRuled;
					}
				}
				if (Notice.Target == Watched)
				{
					OnWatched += Notice.bDamageOverTime
						? TEXT("T")
						: (Notice.bExplosionAtAnAilmentsEnd ? TEXT("E") : TEXT("H"));
				}
			});
			DeathHandle = Events->OnDeath.AddLambda([this](const FCataclysmDeathNotice& Notice)
			{
				Killers.Add(Notice.Victim, Notice.Killer);
			});
		}

		~FListening()
		{
			if (Events)
			{
				Events->OnHit.Remove(HitHandle);
				Events->OnDeath.Remove(DeathHandle);
			}
		}

		FListening(const FListening&) = delete;
		FListening& operator=(const FListening&) = delete;

		UCataclysmCombatEvents* Events = nullptr;
		const AActor* Watched = nullptr;
		const AActor* Wearer = nullptr;
		FDelegateHandle HitHandle;
		FDelegateHandle DeathHandle;

		FString OnWatched;
		int32 Explosions = 0;
		int32 ExplosionsNotTheWearers = 0;
		int32 ExplosionsNotDeliveredAsRuled = 0;
		TMap<const AActor*, const AActor*> Killers;
	};

	/** A stack granted each time `Event` is heard, so the count of stacks is the count of events. */
	FCataclysmPoolAction CountOn(const TCHAR* Event, FName Key)
	{
		FCataclysmPoolAction Grant;
		Grant.Event = FName(Event);
		Grant.StackKey = Key;
		Grant.StackSeconds = 60.0f;
		Grant.StackCap = 10;
		Grant.TriggerKey = Key;
		return Grant;
	}

	/** Where the creatures of every scene stand. The wearer is at the origin. */
	const FVector CarrierAt(20.0f * M, 0.0f, 0.0f);
	/** 3 m from the carrier. */
	const FVector NearAt(23.0f * M, 0.0f, 0.0f);
	/** 3 m from the carrier on its other side, and 6 m from `NearAt`. */
	const FVector OtherNearAt(17.0f * M, 0.0f, 0.0f);
	/** 8 m from the carrier. */
	const FVector BeyondAt(28.0f * M, 0.0f, 0.0f);
	/** 40 m from the wearer and more than 40 m from every other creature. */
	const FVector MeasuredAt(0.0f, 40.0f * M, 0.0f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnExplodesRollAndAmountTest,
	"Cataclysm.BurnExplodes.ABurnThatRunsOutExplodesForTwiceItsStatedWholeAtARollUnderItsChanceAndNotAtARollEqualToIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the wearer at the origin, the carrier 20 m along +X, the measured creature 40 m along +Y.
 *
 * Each scene is a world of its own. The Burn is laid, three and a half seconds pass, the carrier's health is read,
 * one more second passes and it is read again. In that last second the Burn's fourth tick lands and the Burn ends.
 *
 * THE CONTROL IS THE WEARER WITH NO ROW: the last second takes one tick and nothing else. With a chance of 30 and
 * the roll pinned at 29.9 it takes one tick and what a blow of 80 takes. At a roll of exactly 30 it takes one tick:
 * a roll equal to the chance fails. With a chance of 100 and the roll pinned at 100 it explodes: a chance of 100
 * is not compared with the roll.
 */
bool FCataclysmBurnExplodesRollAndAmountTest::RunTest(const FString&)
{
	using namespace CataclysmBurnExplodesTest;
	using FEffects = UCataclysmSkillEffects;

	// THE ARITHMETIC FIRST: the whole stated damage is the figure a tick over the ticks of one full duration.
	TestEqual(TEXT("ten a tick each second for four seconds is 40"),
			  FEffects::WholeStatedDamageOverTime(10.0f, 1.0f, 4.0f), 40.0f, 0.001f);
	TestEqual(TEXT("ten a tick every 0.75 of a second for four seconds is five ticks: 50"),
			  FEffects::WholeStatedDamageOverTime(10.0f, 0.75f, 4.0f), 50.0f, 0.001f);
	TestEqual(TEXT("ten a tick every half second for four seconds is eight ticks: 80"),
			  FEffects::WholeStatedDamageOverTime(10.0f, 0.5f, 4.0f), 80.0f, 0.001f);
	TestEqual(TEXT("a duration shorter than one interval has no tick: nought"),
			  FEffects::WholeStatedDamageOverTime(10.0f, 1.0f, 0.5f), 0.0f, 0.001f);
	TestEqual(TEXT("no interval: nought"), FEffects::WholeStatedDamageOverTime(10.0f, 0.0f, 4.0f), 0.0f, 0.001f);
	TestEqual(TEXT("no figure a tick: nought"), FEffects::WholeStatedDamageOverTime(0.0f, 1.0f, 4.0f), 0.0f, 0.001f);
	TestEqual(TEXT("the explosion is double"), FEffects::ExplosionAtAnAilmentsEndMultiple, 2.0f, 0.001f);
	TestEqual(TEXT("and reaches as far as the overkill explosion does, five metres"),
			  FEffects::ExplosionAtAnAilmentsEndRadiusCm, FEffects::OverkillExplosionRadiusCm, 0.001f);

	struct FRead
	{
		bool bSetUp = false;
		/** What one of the Burn's first three ticks took from the carrier. */
		float Tick = 0.0f;
		/** What the carrier lost in the second in which the Burn ended. */
		float LostAtTheEnd = 0.0f;
		/** What a blow of 80, delivered as the explosion's, takes from a creature like the carrier. */
		float SuchABlowTakes = 0.0f;
		int32 Explosions = 0;
		bool bStillBurning = true;
	};

	const auto Run = [this](const TCHAR* Who, float Chance, float Roll) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a world"), Who), World))
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRollPin Pin(RollName, Roll);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the roll's console variable exists"), Who), Pin.Found()))
		{
			return Read;
		}

		FWearer Wearer(World);
		ACataclysmEnemyCharacter* Carrier = CreatureAt(World, CarrierAt);
		ACataclysmEnemyCharacter* Measured = CreatureAt(World, MeasuredAt);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a player character on a player state"), Who),
					  Wearer.IsComplete())
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: the carrier"), Who), Carrier)
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: the measured creature"), Who), Measured))
		{
			return Read;
		}
		Read.SuchABlowTakes = WhatSuchABlowTakes(Wearer, Measured, Explosion);
		Hold(Wearer.AbilitySystem, Chance);
		FListening Heard(UCataclysmCombatEvents::In(World), Carrier, Wearer.Character);

		const float AtFirst = HealthOf(Carrier);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the Burn is laid"), Who), LayBurn(Wearer.Character, Carrier)))
		{
			return Read;
		}
		CataclysmTestWorld::RunClock(World, 3.5f);
		const float BeforeTheEnd = HealthOf(Carrier);
		Read.Tick = (AtFirst - BeforeTheEnd) / 3.0f;
		CataclysmTestWorld::RunClock(World, 1.0f);
		Read.LostAtTheEnd = BeforeTheEnd - HealthOf(Carrier);
		Read.Explosions = Heard.Explosions;
		Read.bStillBurning = Burns(Carrier);
		Read.bSetUp = TestTrue(*FString::Printf(TEXT("%s: set-up: three ticks took something, %.2f each"), Who,
												Read.Tick), Read.Tick > 0.0f)
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: a blow of 80 takes something, %.2f"), Who,
										 Read.SuchABlowTakes), Read.SuchABlowTakes > 0.0f)
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: the wearer is alive at the end"), Who), Wearer.IsAlive());
		return Read;
	};

	const FRead Control = Run(TEXT("no row"), 0.0f, 0.0f);
	const FRead Under = Run(TEXT("a chance of 30 and a roll of 29.9"), 30.0f, 29.9f);
	const FRead Equal = Run(TEXT("a chance of 30 and a roll of 30"), 30.0f, 30.0f);
	const FRead Always = Run(TEXT("a chance of 100 and a roll of 100"), 100.0f, 100.0f);
	if (!Control.bSetUp || !Under.bSetUp || !Equal.bSetUp || !Always.bSetUp)
	{
		return false;
	}

	// THE CONTROL: with no row the Burn ends and that is all.
	TestEqual(TEXT("control: with no row the last second takes one tick"), Control.LostAtTheEnd, Control.Tick, 0.05f);
	TestEqual(TEXT("control: and no explosion is announced"), Control.Explosions, 0);
	TestFalse(TEXT("control: and the Burn has ended"), Control.bStillBurning);

	TestEqual(TEXT("at a roll under the chance the last second takes one tick and what a blow of 80 takes: twice "
				   "the 40 the Burn was laid to deal"),
			  Under.LostAtTheEnd - Under.Tick, Under.SuchABlowTakes, 0.05f);
	TestEqual(TEXT("and one explosion is announced, on the carrier alone"), Under.Explosions, 1);
	TestFalse(TEXT("and the Burn has ended"), Under.bStillBurning);

	TestEqual(TEXT("at a roll equal to the chance the last second takes one tick and nothing else"),
			  Equal.LostAtTheEnd, Equal.Tick, 0.05f);
	TestEqual(TEXT("and no explosion is announced"), Equal.Explosions, 0);

	TestEqual(TEXT("a chance of 100 explodes at a roll of 100"), Always.LostAtTheEnd - Always.Tick,
			  Always.SuchABlowTakes, 0.05f);
	TestEqual(TEXT("and one explosion is announced"), Always.Explosions, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnExplodesReachTest,
	"Cataclysm.BurnExplodes.TheExplosionStrikesTheCarrierAndEveryEnemyWithinFiveMetresOfItAndNobodyBeyond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the wearer at the origin; the carrier 20 m along +X; one creature 3 m beyond it and one 3 m short of
 * it; one 8 m beyond it; the measured creature 40 m along +Y.
 *
 * THE CONTROL IS THE SAME SCENE WITH NO ROW, in a world of its own: nobody but the carrier loses anything.
 *
 * With the row, each of the two creatures 3 m from the carrier loses what a blow of 80 takes, the creature 8 m from
 * it loses nothing. The two struck creatures stand 17 m and 23 m from the wearer, so
 * the five metres are counted from the carrier and not from the applier.
 */
bool FCataclysmBurnExplodesReachTest::RunTest(const FString&)
{
	using namespace CataclysmBurnExplodesTest;

	struct FRead
	{
		bool bSetUp = false;
		float SuchABlowTakes = 0.0f;
		float CarrierLostAtTheEnd = 0.0f;
		float Tick = 0.0f;
		float NearLost = 0.0f;
		float OtherNearLost = 0.0f;
		float BeyondLost = 0.0f;
		float MeasuredLost = 0.0f;
		int32 Explosions = 0;
		int32 NotTheWearers = 0;
		int32 NotAsRuled = 0;
	};

	const auto Run = [this](const TCHAR* Who, float Chance) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a world"), Who), World))
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRollPin Pin(RollName, 0.0f);

		FWearer Wearer(World);
		ACataclysmEnemyCharacter* Carrier = CreatureAt(World, CarrierAt);
		ACataclysmEnemyCharacter* Near = CreatureAt(World, NearAt);
		ACataclysmEnemyCharacter* OtherNear = CreatureAt(World, OtherNearAt);
		ACataclysmEnemyCharacter* Beyond = CreatureAt(World, BeyondAt);
		ACataclysmEnemyCharacter* Measured = CreatureAt(World, MeasuredAt);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a player character on a player state"), Who),
					  Wearer.IsComplete())
			|| !TestTrue(*FString::Printf(TEXT("%s: set-up: the five creatures"), Who),
						 Carrier && Near && OtherNear && Beyond && Measured))
		{
			return Read;
		}
		Read.SuchABlowTakes = WhatSuchABlowTakes(Wearer, Measured, Explosion);
		Hold(Wearer.AbilitySystem, Chance);
		FListening Heard(UCataclysmCombatEvents::In(World), Carrier, Wearer.Character);

		const float CarrierAtFirst = HealthOf(Carrier);
		const float NearAtFirst = HealthOf(Near);
		const float OtherNearAtFirst = HealthOf(OtherNear);
		const float BeyondAtFirst = HealthOf(Beyond);
		const float MeasuredAtFirst = HealthOf(Measured);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the Burn is laid"), Who), LayBurn(Wearer.Character, Carrier)))
		{
			return Read;
		}
		CataclysmTestWorld::RunClock(World, 3.5f);
		const float CarrierBeforeTheEnd = HealthOf(Carrier);
		Read.Tick = (CarrierAtFirst - CarrierBeforeTheEnd) / 3.0f;
		const bool bNobodyElseYet = TestEqual(
			*FString::Printf(TEXT("%s: set-up: before the Burn ends nobody near it has lost anything"), Who),
			(NearAtFirst - HealthOf(Near)) + (OtherNearAtFirst - HealthOf(OtherNear)), 0.0f, 0.001f);
		CataclysmTestWorld::RunClock(World, 1.0f);

		Read.CarrierLostAtTheEnd = CarrierBeforeTheEnd - HealthOf(Carrier);
		Read.NearLost = NearAtFirst - HealthOf(Near);
		Read.OtherNearLost = OtherNearAtFirst - HealthOf(OtherNear);
		Read.BeyondLost = BeyondAtFirst - HealthOf(Beyond);
		Read.MeasuredLost = MeasuredAtFirst - HealthOf(Measured);
		Read.Explosions = Heard.Explosions;
		Read.NotTheWearers = Heard.ExplosionsNotTheWearers;
		Read.NotAsRuled = Heard.ExplosionsNotDeliveredAsRuled;
		Read.bSetUp = bNobodyElseYet
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: three ticks took something, %.2f each"), Who, Read.Tick),
						Read.Tick > 0.0f)
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: a blow of 80 takes something, %.2f"), Who,
										 Read.SuchABlowTakes), Read.SuchABlowTakes > 0.0f)
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: the wearer is alive at the end"), Who), Wearer.IsAlive());
		return Read;
	};

	const FRead Control = Run(TEXT("no row"), 0.0f);
	const FRead Worn = Run(TEXT("a chance of 100"), 100.0f);
	if (!Control.bSetUp || !Worn.bSetUp)
	{
		return false;
	}

	// THE CONTROL: nothing but the Burn's own last tick.
	TestEqual(TEXT("control: with no row the carrier's last second takes one tick"), Control.CarrierLostAtTheEnd,
			  Control.Tick, 0.05f);
	TestEqual(TEXT("control: the creature 3 m beyond the carrier loses nothing"), Control.NearLost, 0.0f, 0.001f);
	TestEqual(TEXT("control: nor the creature 3 m short of it"), Control.OtherNearLost, 0.0f, 0.001f);
	TestEqual(TEXT("control: and no explosion is announced"), Control.Explosions, 0);

	TestEqual(TEXT("the carrier is struck: its last second takes one tick and what a blow of 80 takes"),
			  Worn.CarrierLostAtTheEnd - Worn.Tick, Worn.SuchABlowTakes, 0.05f);
	TestEqual(TEXT("the creature 3 m beyond the carrier, 23 m from the wearer, takes what a blow of 80 takes"),
			  Worn.NearLost, Worn.SuchABlowTakes, 0.05f);
	TestEqual(TEXT("and so does the creature 3 m short of it"), Worn.OtherNearLost, Worn.SuchABlowTakes, 0.05f);
	TestEqual(TEXT("the creature 8 m from the carrier takes nothing"), Worn.BeyondLost, 0.0f, 0.001f);
	TestEqual(TEXT("the creature 40 m away takes nothing after the blow that measured it"), Worn.MeasuredLost, 0.0f,
			  0.001f);
	TestEqual(TEXT("three blows are announced as the explosion: the carrier and the two near it"), Worn.Explosions, 3);
	TestEqual(TEXT("each of them is credited to the wearer"), Worn.NotTheWearers, 0);
	TestEqual(TEXT("and each is area damage, not a critical strike and not a tick"), Worn.NotAsRuled, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnExplodesAfterTheLastTickTest,
	"Cataclysm.BurnExplodes.TheExplosionComesAfterTheBurnsLastTick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the wearer at the origin and the carrier 20 m along +X.
 *
 * Every blow announced on the carrier is written down in the order announced, T for a tick and E for the
 * explosion, while the clock runs five seconds from the Burn being laid. THE CONTROL IS THE WEARER WITH NO ROW:
 * four ticks and nothing else. With the row: the same four ticks and then the explosion, in that order.
 */
bool FCataclysmBurnExplodesAfterTheLastTickTest::RunTest(const FString&)
{
	using namespace CataclysmBurnExplodesTest;

	struct FRead
	{
		bool bSetUp = false;
		FString Order;
	};

	const auto Run = [this](const TCHAR* Who, float Chance) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a world"), Who), World))
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRollPin Pin(RollName, 0.0f);

		FWearer Wearer(World);
		ACataclysmEnemyCharacter* Carrier = CreatureAt(World, CarrierAt);
		UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a player character on a player state"), Who),
					  Wearer.IsComplete())
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: the carrier"), Who), Carrier)
			|| !TestNotNull(*FString::Printf(TEXT("%s: set-up: the announcements"), Who), Events))
		{
			return Read;
		}
		Hold(Wearer.AbilitySystem, Chance);
		FListening Heard(Events, Carrier, Wearer.Character);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the Burn is laid"), Who), LayBurn(Wearer.Character, Carrier)))
		{
			return Read;
		}
		CataclysmTestWorld::RunClock(World, 5.0f);
		Read.Order = Heard.OnWatched;
		Read.bSetUp = TestFalse(*FString::Printf(TEXT("%s: set-up: the Burn has ended"), Who), Burns(Carrier))
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: the wearer is alive at the end"), Who), Wearer.IsAlive());
		return Read;
	};

	const FRead Control = Run(TEXT("no row"), 0.0f);
	const FRead Worn = Run(TEXT("a chance of 100"), 100.0f);
	if (!Control.bSetUp || !Worn.bSetUp)
	{
		return false;
	}
	TestEqual(TEXT("control: with no row the carrier is told of four ticks and nothing else"), Control.Order,
			  FString(TEXT("TTTT")));
	TestEqual(TEXT("with the row the carrier is told of the same four ticks and then of the explosion"), Worn.Order,
			  FString(TEXT("TTTTE")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnExplodesOnlyByTimeTest,
	"Cataclysm.BurnExplodes.ABurnEndedByACleanseAReplacementTheBurstThatEndsItOrItsCarriersDeathDoesNotExplode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the wearer at the origin, the carrier 20 m along +X, a bystander 3 m beyond it and the measured creature
 * 40 m along +Y. The wearer holds a chance of 100 in every scene, so an explosion that may happen does.
 *
 * Each scene is a world of its own. The Burn is laid and a second and a half pass, in which one tick lands. Then:
 *
 *   - THE CONTROL: nothing is done. The Burn runs out and explodes: one explosion on each of the two creatures.
 *   - a cleanse of the carrier ends the Burn. No explosion then, and none in the four seconds after.
 *   - a stronger Burn, twenty a tick, replaces it. No explosion then. The new Burn runs out four seconds after it
 *     was laid and explodes for ITS whole: the bystander takes what a blow of 160 takes, once.
 *   - the burst that deals what is left and ends the Burn. No explosion then or after.
 *   - a blow kills the carrier. No explosion then or after.
 *   - the carrier is made with three and a half ticks' worth of health, so the Burn's own last tick kills it. No
 *     explosion: the bystander takes nothing.
 */
bool FCataclysmBurnExplodesOnlyByTimeTest::RunTest(const FString&)
{
	using namespace CataclysmBurnExplodesTest;

	enum class EEnding : uint8
	{
		RunsOut,
		Cleansed,
		Replaced,
		BurstAndEnded,
		CarrierKilledByABlow,
		CarrierKilledByTheLastTick,
	};

	struct FRead
	{
		bool bSetUp = false;
		/** Explosions announced by the moment just after the ending was made, and by the end of the scene. */
		int32 ExplosionsAtOnce = 0;
		int32 ExplosionsInAll = 0;
		float BystanderLost = 0.0f;
		/** What a blow of 80, and one of 160, delivered as the explosion's, takes from a creature. */
		float BlowOfTheFirstTakes = 0.0f;
		float BlowOfTheStrongerTakes = 0.0f;
		bool bCarrierGone = false;
	};

	const auto Run = [this](const TCHAR* Who, EEnding Ending) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a world"), Who), World))
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRollPin Pin(RollName, 0.0f);

		FWearer Wearer(World);
		ACataclysmEnemyCharacter* Bystander = CreatureAt(World, NearAt);
		ACataclysmEnemyCharacter* Measured = CreatureAt(World, MeasuredAt);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a player character on a player state"), Who),
					  Wearer.IsComplete())
			|| !TestTrue(*FString::Printf(TEXT("%s: set-up: the bystander and the measured creature"), Who),
						 Bystander && Measured))
		{
			return Read;
		}
		Read.BlowOfTheFirstTakes = WhatSuchABlowTakes(Wearer, Measured, Explosion);
		Read.BlowOfTheStrongerTakes = WhatSuchABlowTakes(Wearer, Measured, 2.0f * Explosion);

		// THE CARRIER HAS HEALTH TO SPARE IN EVERY SCENE BUT THE LAST, where it is made with three and a half
		// ticks' worth. What a tick takes is measured for that first, with no row worn, on the measured creature:
		// one tick of the same Burn, which a cleanse then ends.
		float CarrierHealth = Plenty;
		if (Ending == EEnding::CarrierKilledByTheLastTick)
		{
			const float MeasuredBefore = HealthOf(Measured);
			if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a Burn is laid on the measured creature"), Who),
						  LayBurn(Wearer.Character, Measured)))
			{
				return Read;
			}
			CataclysmTestWorld::RunClock(World, 1.5f);
			const float MeasuredTick = MeasuredBefore - HealthOf(Measured);
			UCataclysmDebuffs::Cleanse(Measured);
			if (!TestTrue(*FString::Printf(TEXT("%s: set-up: its one tick took something, %.2f"), Who, MeasuredTick),
						  MeasuredTick > 0.0f)
				|| !TestFalse(*FString::Printf(TEXT("%s: set-up: and a cleanse ended that Burn"), Who),
							  Burns(Measured)))
			{
				return Read;
			}
			CarrierHealth = 3.5f * MeasuredTick;
		}
		ACataclysmEnemyCharacter* Carrier = CreatureAt(World, CarrierAt, CarrierHealth);
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: the carrier"), Who), Carrier)
			|| !TestEqual(*FString::Printf(TEXT("%s: set-up: the carrier holds the health it was made with"), Who),
						  HealthOf(Carrier), CarrierHealth, 0.01f))
		{
			return Read;
		}
		Hold(Wearer.AbilitySystem, 100.0f);
		FListening Heard(UCataclysmCombatEvents::In(World), Carrier, Wearer.Character);

		const float CarrierAtFirst = HealthOf(Carrier);
		const float BystanderAtFirst = HealthOf(Bystander);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the Burn is laid"), Who), LayBurn(Wearer.Character, Carrier)))
		{
			return Read;
		}
		CataclysmTestWorld::RunClock(World, 1.5f);
		const float Tick = CarrierAtFirst - HealthOf(Carrier);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: one tick has landed, %.2f"), Who, Tick), Tick > 0.0f)
			|| !TestTrue(*FString::Printf(TEXT("%s: set-up: and the carrier burns"), Who), Burns(Carrier)))
		{
			return Read;
		}

		bool bEndingMade = true;
		switch (Ending)
		{
		case EEnding::RunsOut:
			break;
		case EEnding::Cleansed:
			UCataclysmDebuffs::Cleanse(Carrier);
			bEndingMade = TestFalse(*FString::Printf(TEXT("%s: set-up: the cleanse ended the Burn"), Who),
									Burns(Carrier));
			break;
		case EEnding::Replaced:
			bEndingMade = TestTrue(*FString::Printf(TEXT("%s: set-up: a Burn of twenty a tick is laid over it"), Who),
								   LayBurn(Wearer.Character, Carrier, 2.0f * PerTick))
				&& TestTrue(*FString::Printf(TEXT("%s: set-up: and the carrier burns"), Who), Burns(Carrier));
			break;
		case EEnding::BurstAndEnded:
			bEndingMade = TestEqual(
					*FString::Printf(TEXT("%s: set-up: the burst reaches one effect"), Who),
					UCataclysmSkillEffects::DealRemainingDamageOverTime(
						Wearer.Character, Carrier, 100.0f, UCataclysmSkillEffects::BurnTag(), /*bEndEach=*/true), 1)
				&& TestFalse(*FString::Printf(TEXT("%s: set-up: and ended the Burn"), Who), Burns(Carrier));
			break;
		case EEnding::CarrierKilledByABlow:
		{
			// A PLAIN BLOW, and not one delivered as the explosion's: that one would be counted as an explosion.
			FCataclysmHitDelivery Plain;
			Plain.bCannotCriticallyStrike = true;
			UCataclysmSkillEffects::ApplyDirectDamage(Wearer.Character, Carrier, 100000.0f, Plain);
			bEndingMade = TestTrue(*FString::Printf(TEXT("%s: set-up: the blow killed the carrier"), Who),
								   IsGone(Carrier));
			break;
		}
		case EEnding::CarrierKilledByTheLastTick:
			// NOTHING IS DONE. One tick has landed, so the carrier holds two and a half ticks' worth: the ticks at
			// two and at three seconds leave it alive, and the tick due as the Burn ends kills it.
			bEndingMade = TestEqual(
				*FString::Printf(TEXT("%s: set-up: the carrier holds two and a half ticks' worth"), Who),
				HealthOf(Carrier), 2.5f * Tick, 0.05f);
			break;
		}
		if (!bEndingMade)
		{
			return Read;
		}
		Read.ExplosionsAtOnce = Heard.Explosions;

		// FOUR AND A HALF SECONDS MORE: past the first Burn's own end, and past the end of the one laid over it.
		CataclysmTestWorld::RunClock(World, 2.0f);
		const bool bAliveBeforeItsLastTick = Ending != EEnding::CarrierKilledByTheLastTick
			|| TestFalse(*FString::Printf(TEXT("%s: set-up: at three and a half seconds the carrier is alive"), Who),
						 IsGone(Carrier));
		CataclysmTestWorld::RunClock(World, 2.5f);

		Read.ExplosionsInAll = Heard.Explosions;
		Read.BystanderLost = BystanderAtFirst - HealthOf(Bystander);
		Read.bCarrierGone = IsGone(Carrier);
		Read.bSetUp = bAliveBeforeItsLastTick
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: the wearer is alive at the end"), Who), Wearer.IsAlive())
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: no Burn is left on a carrier still standing"), Who),
						IsGone(Carrier) || !Burns(Carrier));
		return Read;
	};

	const FRead Control = Run(TEXT("runs out"), EEnding::RunsOut);
	const FRead Cleansed = Run(TEXT("cleansed"), EEnding::Cleansed);
	const FRead Replaced = Run(TEXT("replaced"), EEnding::Replaced);
	const FRead Burst = Run(TEXT("burst and ended"), EEnding::BurstAndEnded);
	const FRead Killed = Run(TEXT("carrier killed by a blow"), EEnding::CarrierKilledByABlow);
	const FRead LastTick = Run(TEXT("carrier killed by the last tick"), EEnding::CarrierKilledByTheLastTick);
	if (!Control.bSetUp || !Cleansed.bSetUp || !Replaced.bSetUp || !Burst.bSetUp || !Killed.bSetUp
		|| !LastTick.bSetUp)
	{
		return false;
	}

	// THE CONTROL: left alone, this Burn in this scene does explode.
	TestEqual(TEXT("control: a second and a half in, nothing has exploded"), Control.ExplosionsAtOnce, 0);
	TestEqual(TEXT("control: left to run out the Burn explodes, on the carrier and on the bystander"),
			  Control.ExplosionsInAll, 2);
	TestEqual(TEXT("control: and the bystander takes what a blow of 80 takes"), Control.BystanderLost,
			  Control.BlowOfTheFirstTakes, 0.05f);

	TestEqual(TEXT("a cleanse explodes nothing as it ends the Burn"), Cleansed.ExplosionsAtOnce, 0);
	TestEqual(TEXT("and nothing after it"), Cleansed.ExplosionsInAll, 0);
	TestEqual(TEXT("and the bystander of a cleansed Burn loses nothing"), Cleansed.BystanderLost, 0.0f, 0.001f);

	TestEqual(TEXT("a stronger Burn laid over it explodes nothing as it replaces the first"),
			  Replaced.ExplosionsAtOnce, 0);
	TestEqual(TEXT("the new Burn explodes once at its own end, on the carrier and on the bystander"),
			  Replaced.ExplosionsInAll, 2);
	TestEqual(TEXT("and for its own whole: the bystander takes what a blow of 160 takes, and no 80 before it"),
			  Replaced.BystanderLost, Replaced.BlowOfTheStrongerTakes, 0.05f);

	TestEqual(TEXT("the burst that ends the Burn explodes nothing as it ends it"), Burst.ExplosionsAtOnce, 0);
	TestEqual(TEXT("and nothing after it"), Burst.ExplosionsInAll, 0);
	TestEqual(TEXT("and the bystander of a Burn burst and ended loses nothing"), Burst.BystanderLost, 0.0f, 0.001f);

	TestEqual(TEXT("a blow that kills the carrier explodes nothing"), Killed.ExplosionsAtOnce, 0);
	TestEqual(TEXT("and nothing when the Burn it carried would have run out"), Killed.ExplosionsInAll, 0);
	TestEqual(TEXT("and the bystander of a carrier killed by a blow loses nothing"), Killed.BystanderLost, 0.0f,
			  0.001f);

	TestTrue(TEXT("the Burn's own last tick killed the carrier left with two and a half ticks' worth"),
			 LastTick.bCarrierGone);
	TestEqual(TEXT("and a Burn whose last tick kills its carrier explodes nothing"), LastTick.ExplosionsInAll, 0);
	TestEqual(TEXT("and the bystander of that carrier loses nothing"), LastTick.BystanderLost, 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnExplodesIsNotABlowStruckTest,
	"Cataclysm.BurnExplodes.TheExplosionLaysNoBurnRaisesNoneOfTheThreeOnHitEventsAndAKillByItIsTheAppliers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the wearer at the origin; the carrier 20 m along +X; a creature holding 1 health 3 m beyond it; a
 * creature with health to spare 3 m short of it; the measured creature 40 m along +Y.
 *
 * The wearer holds a chance of 100 and four hand-made rows, each a stack granted on one event, so a count of stacks
 * is a count of that event: `hit_dealt`, `first_hit_dealt`, `critical_strike` and `kill`. Every blow the wearer
 * strikes here is given a critical strike chance of 100, with the critical strike roll pinned to nought AFTER the
 * world is made.
 *
 * THE CONTROL IS A BLOW THE WEARER STRUCK, on the measured creature: it raises each of the first three events once
 * and `kill` not at all. Then the Burn runs out and explodes on three creatures and kills one of them. None of the
 * first three events is raised again, `kill` is raised once, and the death names the wearer as the killer. Nobody
 * the explosion struck burns afterwards. Then the wearer strikes, for real, the creature the explosion struck and
 * left alive, and that blow raises all three: the explosion was not recorded as the wearer's first blow on it.
 */
bool FCataclysmBurnExplodesIsNotABlowStruckTest::RunTest(const FString&)
{
	using namespace CataclysmBurnExplodesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// PINNED AFTER THE WORLD IS MADE: `MakeWorldThatHasBegunPlay` sets the critical strike roll to 100 itself.
	const CataclysmTestWorld::FScopedCritRoll RollOfNought(0.0f);
	const FRollPin Pin(RollName, 0.0f);

	FWearer Wearer(World);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	ACataclysmEnemyCharacter* Carrier = CreatureAt(World, CarrierAt);
	ACataclysmEnemyCharacter* Frail = CreatureAt(World, NearAt, 1.0f);
	ACataclysmEnemyCharacter* Caught = CreatureAt(World, OtherNearAt);
	ACataclysmEnemyCharacter* Measured = CreatureAt(World, MeasuredAt);
	if (!TestTrue(TEXT("set-up: a player character on a player state"), Wearer.IsComplete())
		|| !TestNotNull(TEXT("set-up: the announcements"), Events)
		|| !TestTrue(TEXT("set-up: the four creatures"), Carrier && Frail && Caught && Measured))
	{
		return false;
	}
	Hold(Wearer.AbilitySystem, 100.0f);
	const FName Hits(TEXT("Test:hits"));
	const FName FirstHits(TEXT("Test:first hits"));
	const FName Crits(TEXT("Test:crits"));
	const FName Kills(TEXT("Test:kills"));
	Wearer.AbilitySystem->SetPoolActions({CountOn(TEXT("hit_dealt"), Hits),
										  CountOn(TEXT("first_hit_dealt"), FirstHits),
										  CountOn(TEXT("critical_strike"), Crits), CountOn(TEXT("kill"), Kills)});
	FListening Heard(Events, Caught, Wearer.Character);

	const auto Strike = [&Wearer](ACataclysmEnemyCharacter* Target)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 100.0f;
		UCataclysmSkillEffects::ApplyHit(Wearer.Character, Target, /*DamagePercent=*/100.0f, FGameplayTagContainer(),
										 Delivery);
	};

	// THE CONTROL: a blow the wearer struck raises each of the three once, and no kill.
	Strike(Measured);
	if (!TestEqual(TEXT("control: a blow the wearer struck raises hit_dealt once"),
				   Wearer.AbilitySystem->OwnStacksHeld(Hits), 1)
		|| !TestEqual(TEXT("control: and first_hit_dealt once"), Wearer.AbilitySystem->OwnStacksHeld(FirstHits), 1)
		|| !TestEqual(TEXT("control: and critical_strike once"), Wearer.AbilitySystem->OwnStacksHeld(Crits), 1)
		|| !TestEqual(TEXT("control: and no kill"), Wearer.AbilitySystem->OwnStacksHeld(Kills), 0))
	{
		return false;
	}

	// THE BURN, LAID BY THE FUNCTION AND NOT BY A BLOW, SO LAYING IT RAISES NOTHING.
	const float CaughtAtFirst = HealthOf(Caught);
	if (!TestTrue(TEXT("set-up: the Burn is laid"), LayBurn(Wearer.Character, Carrier))
		|| !TestEqual(TEXT("set-up: laying it raises no hit_dealt: still one"),
					  Wearer.AbilitySystem->OwnStacksHeld(Hits), 1))
	{
		return false;
	}
	CataclysmTestWorld::RunClock(World, 5.0f);
	if (!TestEqual(TEXT("set-up: the explosion was announced on three creatures"), Heard.Explosions, 3)
		|| !TestTrue(TEXT("set-up: it struck the creature with health to spare"),
					 CaughtAtFirst - HealthOf(Caught) > 0.0f)
		|| !TestTrue(TEXT("set-up: the wearer is alive"), Wearer.IsAlive()))
	{
		return false;
	}

	TestEqual(TEXT("the Burn's ticks and its explosion raise no hit_dealt: still one"),
			  Wearer.AbilitySystem->OwnStacksHeld(Hits), 1);
	TestEqual(TEXT("and no first_hit_dealt, though the explosion struck two creatures the wearer never had: still "
				   "one"), Wearer.AbilitySystem->OwnStacksHeld(FirstHits), 1);
	TestEqual(TEXT("and no critical_strike, with the roll pinned so that every blow that may be one is: still one"),
			  Wearer.AbilitySystem->OwnStacksHeld(Crits), 1);

	// THE KILL IS THE APPLIER'S.
	TestTrue(TEXT("the explosion killed the creature that held 1 health"), IsGone(Frail));
	const AActor* const* Killer = Heard.Killers.Find(Frail);
	TestTrue(TEXT("its death names the wearer as the killer"), Killer && *Killer == Wearer.Character);
	TestEqual(TEXT("and the wearer's kill row acted once"), Wearer.AbilitySystem->OwnStacksHeld(Kills), 1);

	// IT LAYS NO BURN.
	TestFalse(TEXT("the carrier does not burn after the explosion"), Burns(Carrier));
	TestFalse(TEXT("nor does the creature it struck and left alive"), Burns(Caught));

	// AND IT IS NOT RECORDED AS THE WEARER'S FIRST BLOW ON WHAT IT STRUCK.
	TestEqual(TEXT("set-up: the one blow announced on that creature so far is the explosion"), Heard.OnWatched,
			  FString(TEXT("E")));
	Strike(Caught);
	if (!TestFalse(TEXT("set-up: that creature lives through the wearer's blow"), IsGone(Caught)))
	{
		return false;
	}
	TestEqual(TEXT("the wearer's real blow on the creature the explosion struck raises hit_dealt: two now"),
			  Wearer.AbilitySystem->OwnStacksHeld(Hits), 2);
	TestEqual(TEXT("and first_hit_dealt, because the explosion was not recorded as a first blow: two now"),
			  Wearer.AbilitySystem->OwnStacksHeld(FirstHits), 2);
	TestEqual(TEXT("and critical_strike: two now"), Wearer.AbilitySystem->OwnStacksHeld(Crits), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnExplodesImmuneEnemyTest,
	"Cataclysm.BurnExplodes.AnEnemyImmuneToTheApplierTakesNothingFromTheExplosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the wearer at the origin, the carrier 20 m along +X, a bystander 3 m beyond it and the measured creature
 * 40 m along +Y.
 *
 * The row of 2026-10-12 makes an enemy immune to its wearer's damage a number of seconds after the wearer's first
 * hit on it, while the wearer stays in combat. Here that number is 2. One second in, the wearer strikes the carrier
 * and lays the Burn; the Burn's ticks keep the wearer in combat. Four and a half seconds in the carrier's health is
 * read, and five and a half seconds in it is read again: in that second the Burn's last tick is due and the Burn
 * ends and explodes.
 *
 * THE CONTROL IS THE SAME SCENE WITHOUT THE IMMUNITY ROW, in a world of its own: the carrier's last second takes
 * one tick and what a blow of 80 takes.
 *
 * With the immunity row the carrier's last second takes nothing at all. The bystander, which the wearer never
 * struck, is not immune and takes what a blow of 80 takes in both scenes. In both, the bystander has no first hit of
 * the wearer's on record afterwards, where the carrier, which the wearer did strike, has one: the explosion is not
 * recorded as a first hit.
 */
bool FCataclysmBurnExplodesImmuneEnemyTest::RunTest(const FString&)
{
	using namespace CataclysmBurnExplodesTest;

	struct FRead
	{
		bool bSetUp = false;
		float SuchABlowTakes = 0.0f;
		float Tick = 0.0f;
		float CarrierLostAtTheEnd = 0.0f;
		float BystanderLost = 0.0f;
		bool bCarrierImmuneBeforeTheEnd = false;
		/** Seconds since the wearer's first hit of this combat on each, or below nought for none on record. */
		float CarrierEngagedFor = -2.0f;
		float BystanderEngagedFor = -2.0f;
	};

	const auto Run = [this](const TCHAR* Who, bool bWithTheImmunityRow) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a world"), Who), World))
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRollPin Pin(RollName, 0.0f);

		FWearer Wearer(World);
		ACataclysmEnemyCharacter* Carrier = CreatureAt(World, CarrierAt);
		ACataclysmEnemyCharacter* Bystander = CreatureAt(World, NearAt);
		ACataclysmEnemyCharacter* Measured = CreatureAt(World, MeasuredAt);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a player character on a player state"), Who),
					  Wearer.IsComplete())
			|| !TestTrue(*FString::Printf(TEXT("%s: set-up: the three creatures"), Who),
						 Carrier && Bystander && Measured))
		{
			return Read;
		}
		const UCataclysmAbilitySystemComponent* CarrierSystem =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Carrier));
		const UCataclysmAbilitySystemComponent* BystanderSystem =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Bystander));
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the two creatures' ability systems"), Who),
					  CarrierSystem && BystanderSystem))
		{
			return Read;
		}

		// THE MEASURE FIRST AND WITH NO ROW WORN, on a creature the rest of the scene leaves alone.
		Read.SuchABlowTakes = WhatSuchABlowTakes(Wearer, Measured, Explosion);
		TMap<FName, float> Others;
		if (bWithTheImmunityRow)
		{
			Others.Add(FName(UCataclysmAbilitySystemComponent::EnemiesImmuneAfterSecondsStat), 2.0f);
		}
		Hold(Wearer.AbilitySystem, 100.0f, Others);

		// ONE SECOND IN: the wearer's first hit on the carrier, and the Burn.
		CataclysmTestWorld::RunClock(World, 1.0f);
		FCataclysmHitDelivery Plain;
		Plain.bCannotCriticallyStrike = true;
		UCataclysmSkillEffects::ApplyDirectDamage(Wearer.Character, Carrier, 10.0f, Plain);
		const float BystanderAtFirst = HealthOf(Bystander);
		const float CarrierWhenLaid = HealthOf(Carrier);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the Burn is laid"), Who), LayBurn(Wearer.Character, Carrier)))
		{
			return Read;
		}
		CataclysmTestWorld::RunClock(World, 1.5f);
		Read.Tick = CarrierWhenLaid - HealthOf(Carrier);

		// FOUR AND A HALF SECONDS IN, three and a half after the first hit.
		CataclysmTestWorld::RunClock(World, 2.0f);
		Read.bCarrierImmuneBeforeTheEnd = CarrierSystem->IsImmuneToDamageOf(Wearer.AbilitySystem);
		const float CarrierBeforeTheEnd = HealthOf(Carrier);
		CataclysmTestWorld::RunClock(World, 1.0f);

		Read.CarrierLostAtTheEnd = CarrierBeforeTheEnd - HealthOf(Carrier);
		Read.BystanderLost = BystanderAtFirst - HealthOf(Bystander);
		Read.CarrierEngagedFor = CarrierSystem->SecondsSinceEngagedBy(Wearer.AbilitySystem);
		Read.BystanderEngagedFor = BystanderSystem->SecondsSinceEngagedBy(Wearer.AbilitySystem);
		Read.bSetUp = TestTrue(*FString::Printf(TEXT("%s: set-up: the Burn's first tick took something, %.2f"), Who,
												Read.Tick), Read.Tick > 0.0f)
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: a blow of 80 takes something, %.2f"), Who,
										 Read.SuchABlowTakes), Read.SuchABlowTakes > 0.0f)
			&& TestFalse(*FString::Printf(TEXT("%s: set-up: the Burn has ended"), Who), Burns(Carrier))
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: the wearer is alive at the end"), Who), Wearer.IsAlive());
		return Read;
	};

	const FRead Control = Run(TEXT("no immunity row"), false);
	const FRead Immune = Run(TEXT("the immunity row at 2 seconds"), true);
	if (!Control.bSetUp || !Immune.bSetUp)
	{
		return false;
	}

	// THE CONTROL: with no immunity row the explosion reaches the carrier.
	TestFalse(TEXT("control: without the immunity row the carrier is not immune to the wearer"),
			  Control.bCarrierImmuneBeforeTheEnd);
	TestEqual(TEXT("control: and its last second takes one tick and what a blow of 80 takes"),
			  Control.CarrierLostAtTheEnd - Control.Tick, Control.SuchABlowTakes, 0.05f);
	TestEqual(TEXT("control: and the bystander takes what a blow of 80 takes"), Control.BystanderLost,
			  Control.SuchABlowTakes, 0.05f);
	TestTrue(TEXT("control: the carrier, which the wearer struck, has the wearer's first hit on record"),
			 Control.CarrierEngagedFor >= 0.0f);
	TestTrue(TEXT("the explosion is not recorded as the wearer's first hit on the bystander: it has none on record"),
			 Control.BystanderEngagedFor < 0.0f);

	if (!TestTrue(TEXT("set-up: with the immunity row the carrier is immune to the wearer before the Burn ends"),
				  Immune.bCarrierImmuneBeforeTheEnd))
	{
		return false;
	}
	TestEqual(TEXT("the immune carrier's last second takes nothing: not the tick and not the explosion"),
			  Immune.CarrierLostAtTheEnd, 0.0f, 0.001f);
	TestEqual(TEXT("the bystander, never struck by the wearer, takes what a blow of 80 takes"), Immune.BystanderLost,
			  Immune.SuchABlowTakes, 0.05f);
	TestTrue(TEXT("and the explosion did not start the bystander's own count towards immunity: it has no first "
				  "hit of the wearer's on record"), Immune.BystanderEngagedFor < 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnExplodesWhoseRowTest,
	"Cataclysm.BurnExplodes.TheRowIsTheAppliersAndIsReadWhenTheBurnEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the wearer at the origin, the carrier 20 m along +X and a bystander 3 m beyond it. In the scene of the
 * Burn the wearer has on itself, a creature stands 3 m from the wearer.
 *
 * Each scene is a world of its own, and in each the roll is pinned so that any chance above nought explodes. The
 * count is of blows announced as the explosion.
 *
 *   - THE CONTROL: the row held from before the Burn is laid to after it ends. Two: the carrier and the bystander.
 *   - no row at any time: none.
 *   - no row when the Burn is laid, and the row put on two seconds in: two.
 *   - the row held when the Burn is laid, and taken off two seconds in: none.
 *   - the row held throughout, and the wearer killed two seconds in: none.
 *   - the row held throughout, and the effect is a Poison of the same figures: none. The row requires the Burn's
 *     tag, and the ended effect is asked about with the tag it granted.
 *   - the row held throughout, and the Burn is one the wearer has on itself: none. For this one only the
 *     announcements are counted, and no health is read.
 */
bool FCataclysmBurnExplodesWhoseRowTest::RunTest(const FString&)
{
	using namespace CataclysmBurnExplodesTest;

	enum class EScene : uint8
	{
		HeldThroughout,
		NeverHeld,
		PutOnAfterTheBurnWasLaid,
		TakenOffBeforeItsEnd,
		ApplierDeadAtItsEnd,
		APoisonAndNotABurn,
		ABurnOnTheWearerItself,
	};

	struct FRead
	{
		bool bSetUp = false;
		int32 Explosions = 0;
		float BystanderLost = 0.0f;
	};

	const FGameplayTag Poison =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Poison")), /*ErrorIfNotFound=*/false);
	if (!TestTrue(TEXT("set-up: the Poison tag and the Burn tag exist and are two tags"),
				  Poison.IsValid() && UCataclysmSkillEffects::BurnTag().IsValid()
					  && Poison != UCataclysmSkillEffects::BurnTag()))
	{
		return false;
	}

	const auto Run = [this, &Poison](const TCHAR* Who, EScene Scene) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a world"), Who), World))
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRollPin Pin(RollName, 0.0f);

		FWearer Wearer(World);
		ACataclysmEnemyCharacter* Carrier = CreatureAt(World, CarrierAt);
		const bool bOnItself = Scene == EScene::ABurnOnTheWearerItself;
		ACataclysmEnemyCharacter* Bystander = CreatureAt(World, bOnItself ? FVector(0.0f, 3.0f * M, 0.0f) : NearAt);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a player character on a player state"), Who),
					  Wearer.IsComplete())
			|| !TestTrue(*FString::Printf(TEXT("%s: set-up: the two creatures"), Who), Carrier && Bystander))
		{
			return Read;
		}
		const bool bHeldAtFirst = Scene != EScene::NeverHeld && Scene != EScene::PutOnAfterTheBurnWasLaid;
		Hold(Wearer.AbilitySystem, bHeldAtFirst ? 100.0f : 0.0f);
		FListening Heard(UCataclysmCombatEvents::In(World), Carrier, Wearer.Character);
		const float BystanderAtFirst = HealthOf(Bystander);

		bool bLaid = false;
		if (Scene == EScene::APoisonAndNotABurn)
		{
			bLaid = UCataclysmSkillEffects::ApplyDamageOverTime(Wearer.Character, Carrier, PerTick, BurnSeconds,
																Poison, /*bScalesWithInstigator=*/false);
		}
		else if (bOnItself)
		{
			bLaid = LayBurn(Wearer.Character, Wearer.Character);
		}
		else
		{
			bLaid = LayBurn(Wearer.Character, Carrier);
		}
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the effect is laid"), Who), bLaid))
		{
			return Read;
		}

		CataclysmTestWorld::RunClock(World, 2.0f);
		bool bAsMeant = true;
		if (Scene == EScene::PutOnAfterTheBurnWasLaid)
		{
			Hold(Wearer.AbilitySystem, 100.0f);
		}
		else if (Scene == EScene::TakenOffBeforeItsEnd)
		{
			Hold(Wearer.AbilitySystem, 0.0f);
		}
		else if (Scene == EScene::ApplierDeadAtItsEnd)
		{
			UCataclysmSkillEffects::ApplyDirectDamage(Carrier, Wearer.Character, 10000000.0f);
			bAsMeant = TestFalse(*FString::Printf(TEXT("%s: set-up: the wearer is dead"), Who), Wearer.IsAlive());
		}
		CataclysmTestWorld::RunClock(World, 3.0f);

		Read.Explosions = Heard.Explosions;
		Read.BystanderLost = BystanderAtFirst - HealthOf(Bystander);
		Read.bSetUp = bAsMeant
			&& TestFalse(*FString::Printf(TEXT("%s: set-up: the effect has ended on a carrier still standing"), Who),
						 Scene == EScene::APoisonAndNotABurn
							 ? UCataclysmSkillEffects::HasTag(Carrier, Poison)
							 : (bOnItself ? Burns(Wearer.Character) : Burns(Carrier)))
			&& (Scene == EScene::ApplierDeadAtItsEnd
				|| TestTrue(*FString::Printf(TEXT("%s: set-up: the wearer is alive at the end"), Who),
							Wearer.IsAlive()));
		return Read;
	};

	const FRead Control = Run(TEXT("held throughout"), EScene::HeldThroughout);
	const FRead Never = Run(TEXT("never held"), EScene::NeverHeld);
	const FRead PutOn = Run(TEXT("put on after the Burn was laid"), EScene::PutOnAfterTheBurnWasLaid);
	const FRead TakenOff = Run(TEXT("taken off before its end"), EScene::TakenOffBeforeItsEnd);
	const FRead Dead = Run(TEXT("applier dead at its end"), EScene::ApplierDeadAtItsEnd);
	const FRead APoison = Run(TEXT("a Poison"), EScene::APoisonAndNotABurn);
	const FRead OnItself = Run(TEXT("a Burn on the wearer itself"), EScene::ABurnOnTheWearerItself);
	if (!Control.bSetUp || !Never.bSetUp || !PutOn.bSetUp || !TakenOff.bSetUp || !Dead.bSetUp || !APoison.bSetUp
		|| !OnItself.bSetUp)
	{
		return false;
	}

	// THE CONTROL: with the row held throughout, this scene explodes.
	TestEqual(TEXT("control: with the row held throughout, the carrier and the bystander are struck"),
			  Control.Explosions, 2);
	TestTrue(TEXT("control: and the bystander loses health"), Control.BystanderLost > 0.0f);

	TestEqual(TEXT("an applier without the row gets no explosion"), Never.Explosions, 0);
	TestEqual(TEXT("and its bystander loses nothing"), Never.BystanderLost, 0.0f, 0.001f);

	TestEqual(TEXT("a row put on after the Burn was laid explodes it at its end"), PutOn.Explosions, 2);
	TestEqual(TEXT("for what the control's bystander lost"), PutOn.BystanderLost, Control.BystanderLost, 0.05f);

	TestEqual(TEXT("a row taken off before the Burn ends explodes nothing"), TakenOff.Explosions, 0);
	TestEqual(TEXT("and its bystander loses nothing"), TakenOff.BystanderLost, 0.0f, 0.001f);

	TestEqual(TEXT("an applier dead when its Burn ends gets no explosion"), Dead.Explosions, 0);
	TestEqual(TEXT("and its bystander loses nothing"), Dead.BystanderLost, 0.0f, 0.001f);

	TestEqual(TEXT("a Poison that runs out under a row requiring the Burn's tag explodes nothing"),
			  APoison.Explosions, 0);
	TestEqual(TEXT("and its bystander loses nothing"), APoison.BystanderLost, 0.0f, 0.001f);

	// ONLY THE ANNOUNCEMENTS ARE COUNTED FOR THIS ONE. A creature stands 3 m from the wearer here, and this test does
	// not say what the wearer or that creature does to the other at that range while the clock runs.
	TestEqual(TEXT("a Burn the wearer has on itself explodes nothing when it runs out, with a creature 3 m from it"),
			  OnItself.Explosions, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBurnExplodesMinionTest,
	"Cataclysm.BurnExplodes.AMinionsBurnIsTheMinionsAndExplodesOnlyForASummonerHoldingConduit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * STANDING: the summoner at the origin, its minion 3 m along -X, the carrier 20 m along +X and a bystander 3 m
 * beyond it.
 *
 * The summoner holds a chance of 100. The Burn is laid in the MINION's name, as a minion's own burn is. Ruled
 * 2026-09-17: a minion's ailment is the minion's, and the summoner's only while the summoner holds the keystone
 * Conduit, which is the one stat `minion_hits_count_as_yours`.
 *
 * THE CONTROL IS THE SUMMONER WITHOUT CONDUIT: the minion's Burn runs out and nothing explodes, though the summoner
 * holds the row. With Conduit it explodes, and each blow of the explosion is credited to the summoner.
 *
 * ONLY ANNOUNCEMENTS ARE COUNTED HERE, and no health is read: a minion thinks on a timer of its own and this test
 * does not say what it does while the clock runs.
 */
bool FCataclysmBurnExplodesMinionTest::RunTest(const FString&)
{
	using namespace CataclysmBurnExplodesTest;

	struct FRead
	{
		bool bSetUp = false;
		int32 Explosions = 0;
		int32 NotTheSummoners = 0;
	};

	const auto Run = [this](const TCHAR* Who, bool bConduit) -> FRead
	{
		FRead Read;
		UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: a world"), Who), World))
		{
			return Read;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const FRollPin Pin(RollName, 0.0f);

		FWearer Summoner(World);
		ACataclysmEnemyCharacter* Carrier = CreatureAt(World, CarrierAt);
		ACataclysmEnemyCharacter* Bystander = CreatureAt(World, NearAt);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: a player character on a player state"), Who),
					  Summoner.IsComplete())
			|| !TestTrue(*FString::Printf(TEXT("%s: set-up: the two creatures"), Who), Carrier && Bystander))
		{
			return Read;
		}
		TMap<FName, float> Others;
		if (bConduit)
		{
			Others.Add(FName(TEXT("minion_hits_count_as_yours")), 1.0f);
		}
		Hold(Summoner.AbilitySystem, 100.0f, Others);

		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(Summoner.Character, FVector(-3.0f * M, 0.0f, 0.0f),
														   /*Lifetime=*/60.0f, /*bBurns=*/false);
		ON_SCOPE_EXIT
		{
			if (IsValid(Minion))
			{
				Minion->Destroy();
			}
		};
		if (!TestNotNull(*FString::Printf(TEXT("%s: set-up: the summoner's minion"), Who), Minion)
			|| !TestTrue(*FString::Printf(TEXT("%s: set-up: its blows count as the summoner's only with Conduit"),
										  Who),
						 ACataclysmMinion::HitsCountAsTheSummoners(Minion) == bConduit))
		{
			return Read;
		}
		FListening Heard(UCataclysmCombatEvents::In(World), Carrier, Summoner.Character);
		if (!TestTrue(*FString::Printf(TEXT("%s: set-up: the minion's Burn is laid"), Who), LayBurn(Minion, Carrier)))
		{
			return Read;
		}
		CataclysmTestWorld::RunClock(World, 5.0f);

		Read.Explosions = Heard.Explosions;
		Read.NotTheSummoners = Heard.ExplosionsNotTheWearers;
		Read.bSetUp = TestFalse(*FString::Printf(TEXT("%s: set-up: the Burn has ended"), Who), Burns(Carrier))
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: the minion is still there at the end"), Who),
						IsValid(Minion) && !UCataclysmSkillEffects::IsDead(Minion))
			&& TestTrue(*FString::Printf(TEXT("%s: set-up: and the summoner is alive"), Who), Summoner.IsAlive());
		return Read;
	};

	const FRead Control = Run(TEXT("without Conduit"), false);
	const FRead Conduit = Run(TEXT("with Conduit"), true);
	if (!Control.bSetUp || !Conduit.bSetUp)
	{
		return false;
	}
	TestEqual(TEXT("control: a minion's Burn explodes nothing for a summoner who holds the row and not Conduit"),
			  Control.Explosions, 0);
	TestEqual(TEXT("with Conduit the minion's Burn explodes, on the carrier and on the bystander"),
			  Conduit.Explosions, 2);
	TestEqual(TEXT("and each blow of that explosion is credited to the summoner"), Conduit.NotTheSummoners, 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
