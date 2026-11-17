// Copyright Stephen Dubois. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDamageTakenMultiplier.h"
#include "AbilitySystem/CataclysmGroundEffect.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTimeLockRift.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Chronomancer's Time-Lock's ten-piece bonus, the mechanism: the temporal rift. The owner's rulings of 2026-10-08 (the
 * freeze acts like a stun) and 2026-10-17 (#10: the rift lengthens 1 s per enemy caught, at most 10 s, and an enemy
 * already caught in it is not frozen again), and the coordinating session's rulings of 2026-10-19 (K1-K9).
 *
 * "Every 30 seconds, a "temporal rift" appears around you. All enemies that enter the rift are frozen in time for 5
 * seconds and take 500% more damage from all sources. Every enemy caught in it extends the duration by 1s"
 *
 * THE ROWS ARE GIVEN AS STAT LINES, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION: the seven rift stats at a
 * flat 30, 5, 5, 1, 10, 5 and 500, each with no tags. The real rows, worn at ten pieces with a control at nine, are the
 * enchantment session's test. The 2-piece's `debuff_duration` is NOT worn here, so a stun lasts its stated 5 s.
 *
 * EVERY ACTOR HAS A REAL TEAM. The wearer and the other player are player pawns with their ability system on a player
 * state, as the game wires them, on the players' side; they are never refreshed, so they hold no class line. The imp
 * is the other player's. The enemies are creatures of the monsters' side with no armour, evasion, block or
 * resistance and health enough that nothing here kills one. The two tests that press a skill or ask a skill's
 * condition use a bare caster, which a pressed Strike does not wait on an animation for, OWNED BY A PLAYER PAWN SO IT
 * IS ON THE PLAYERS' SIDE. Every body stands at least 1 m from every other.
 *
 * NO WAIT ON A TICK FOR THE CLOCK: the world's clock is moved by hand and `UCataclysmTimeLockRift::Step` called
 * directly, as the regeneration step would call it; `CataclysmTestWorld::RunClock`, which fires timers, is used for a
 * burn's ticks and for the one test of the regeneration step itself, and ends between ticks. Critical strikes are
 * silenced by the world helper, but in the one test whose subject is a critical strike.
 */
namespace CataclysmSetBonusChronoTenTest
{
	using Vital = UCataclysmVitalAttributeSet;

	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** A creature's health, large enough that nothing here empties it. */
	constexpr float ChronoTenEnemyHealth = 100000.0f;

	/** The ruled figures. */
	constexpr float ChronoTenEverySeconds = 30.0f;
	constexpr float ChronoTenRadiusMetres = 5.0f;
	constexpr float ChronoTenBaseSeconds = 5.0f;
	constexpr float ChronoTenSecondsPerEnemy = 1.0f;
	constexpr float ChronoTenMostSeconds = 10.0f;
	constexpr float ChronoTenStunSeconds = 5.0f;
	constexpr float ChronoTenMorePercent = 500.0f;

	/** What a frozen enemy's blows are multiplied by: one and 500 per cent. */
	constexpr float ChronoTenMultiplier = 6.0f;

	/** How far a float clock may fall from a figure it was built to equal, in seconds. */
	constexpr float ChronoTenClockAllowance = 0.001f;

	/** The blow every test deals, before any multiplier. */
	constexpr float ChronoTenBlow = 100.0f;

	/** The seven rift stats at the ruled figures, with any of them replaced, as flat stat lines. Nought leaves one out. */
	TMap<FName, FCataclysmStatInputs> ChronoTenRows(const TMap<FName, float>& Changed = TMap<FName, float>())
	{
		TMap<FName, float> Flats = {
			{FName(UCataclysmTimeLockRift::EverySecondsStat), ChronoTenEverySeconds},
			{FName(UCataclysmTimeLockRift::RadiusMetresStat), ChronoTenRadiusMetres},
			{FName(UCataclysmTimeLockRift::BaseSecondsStat), ChronoTenBaseSeconds},
			{FName(UCataclysmTimeLockRift::SecondsPerEnemyStat), ChronoTenSecondsPerEnemy},
			{FName(UCataclysmTimeLockRift::MostSecondsStat), ChronoTenMostSeconds},
			{FName(UCataclysmTimeLockRift::StunSecondsStat), ChronoTenStunSeconds},
			{FName(UCataclysmTimeLockRift::DamageTakenMorePercentStat), ChronoTenMorePercent}};
		for (const TPair<FName, float>& Each : Changed)
		{
			Flats.Add(Each.Key, Each.Value);
		}
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const TPair<FName, float>& Each : Flats)
		{
			if (Each.Value == 0.0f)
			{
				continue;
			}
			FCataclysmStatModifier Modifier;
			Modifier.Bucket = ECataclysmStatBucket::Flat;
			Modifier.Source = ECataclysmModifierSource::Enchantment;
			Modifier.Value = Each.Value;
			FCataclysmStatInputs Inputs;
			Inputs.Base = 0.0f;
			Inputs.Modifiers.Add(Modifier);
			Stats.Add(Each.Key, Inputs);
		}
		return Stats;
	}

	/** A player pawn with its ability system on a player state, as `CataclysmAoeStrikesAlliesTests.cpp` makes one. */
	ACataclysmPlayerCharacter* ChronoTenPlayerAt(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Made = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!State || !Made)
		{
			return nullptr;
		}
		Made->SetPlayerState(State);
		Made->OnRep_PlayerState();
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Made))
		{
			System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
			System->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 1000.0f);
			System->SetNumericAttributeBase(Vital::GetManaAttribute(), 1000.0f);
		}
		return Made;
	}

	/** The wearer: a player pawn at `Where`, wearing the rows when told to. */
	struct FChronoTenWearer
	{
		FChronoTenWearer(UWorld* World, const FVector& Where)
		{
			Pawn = ChronoTenPlayerAt(World, Where);
			AbilitySystem = Pawn ? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Pawn))
								 : nullptr;
		}

		~FChronoTenWearer()
		{
			if (IsValid(Pawn))
			{
				Pawn->Destroy();
			}
		}

		bool IsMade() const { return Pawn && AbilitySystem; }

		/** The seven stats, all at once, since `SetStatInputs` replaces what was there. */
		void Wear(const TMap<FName, float>& Changed = TMap<FName, float>()) const
		{
			AbilitySystem->SetStatInputs(ChronoTenRows(Changed));
		}

		/** One regeneration step's worth of the rift: how many enemies it caught. */
		int32 Step() const { return UCataclysmTimeLockRift::Step(Pawn); }

		float SecondsUntilReady() const { return UCataclysmTimeLockRift::SecondsUntilReady(AbilitySystem); }
		ACataclysmGroundZone* Rift() const { return UCataclysmTimeLockRift::RiftOf(AbilitySystem); }
		float RiftLength() const { return UCataclysmTimeLockRift::RiftLengthSeconds(AbilitySystem); }
		int32 Caught() const { return UCataclysmTimeLockRift::CaughtBy(AbilitySystem); }

		ACataclysmPlayerCharacter* Pawn = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * A bare caster at `Where`, OWNED BY `Keeper`, a player pawn, so that it is on the players' side: a sphere a search
	 * can find, an ability system, health, 1000 mana, the class resource set and a weapon damage of 100. The shape of
	 * `FScopedSwinger` in `CataclysmStatExemptionTests.cpp`, whose pressed Strike swings at once.
	 */
	struct FChronoTenCaster
	{
		FChronoTenCaster(UWorld* World, const FVector& Where, AActor* Keeper)
		{
			FActorSpawnParameters Spawning;
			Spawning.Owner = Keeper;
			Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Where, FRotator::ZeroRotator, Spawning);
			check(Actor);

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
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FChronoTenCaster()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		int32 Step() const { return UCataclysmTimeLockRift::Step(Actor); }
		ACataclysmGroundZone* Rift() const { return UCataclysmTimeLockRift::RiftOf(AbilitySystem); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * A creature of the monsters' side at `Where`, with nothing to meet a blow with, on the rarity rung asked for.
	 * THE RUNG FIRST, because setting it applies the creature's starting attributes again.
	 */
	ACataclysmEnemyCharacter* ChronoTenEnemyAt(UWorld* World, const FVector& Where, int32 RarityStep = 0)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			if (RarityStep > 0)
			{
				Made->SetRarityStep(RarityStep);
			}
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(ChronoTenEnemyHealth);
			Made->SetArmour(0.0f);
			UAbilitySystemComponent* Its = Made->GetAbilitySystemComponent();
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Its->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Made;
	}

	float ChronoTenHealthOf(const AActor* Enemy)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Enemy);
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : -1.0f;
	}

	/** What a creature has lost since it was made. */
	float ChronoTenLost(const AActor* Enemy)
	{
		return ChronoTenEnemyHealth - ChronoTenHealthOf(Enemy);
	}

	/** What one direct blow of 100 from `From` took from `To`. */
	float ChronoTenBlowFrom(AActor* From, AActor* To)
	{
		const float Before = ChronoTenHealthOf(To);
		UCataclysmSkillEffects::ApplyDirectDamage(From, To, ChronoTenBlow);
		return Before - ChronoTenHealthOf(To);
	}

	/** What one swing of `Imp` at `To` took from it. */
	float ChronoTenImpBlow(ACataclysmMinion* Imp, AActor* To)
	{
		const float Before = ChronoTenHealthOf(To);
		Imp->AttackTarget(To);
		return Before - ChronoTenHealthOf(To);
	}

	/** The longest time left on a stun on `Enemy`, or nought. */
	float ChronoTenStunLeft(const AActor* Enemy)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Enemy);
		const FGameplayTag Stunned = UCataclysmSkillEffects::StunnedTag();
		float Longest = 0.0f;
		if (System && Stunned.IsValid())
		{
			for (const float Seconds : System->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Stunned))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
		}
		return Longest;
	}

	bool ChronoTenIsStunned(const AActor* Enemy)
	{
		return UCataclysmSkillEffects::HasTag(Enemy, UCataclysmSkillEffects::StunnedTag());
	}

	/** What every blow on `Enemy` is multiplied by now. */
	float ChronoTenMultiplierOn(const AActor* Enemy)
	{
		return UCataclysmDamageTakenMultiplier::On(UCataclysmTargeting::AbilitySystemOf(Enemy));
	}

	/** How many times one event is raised on a component while this is alive. */
	struct FChronoTenEventCount
	{
		FChronoTenEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
			: System(InSystem), Event(InEvent)
		{
			Handle = System->OnActionEvent.AddLambda([this](FName Raised)
			{
				Count += Raised == Event ? 1 : 0;
			});
		}
		~FChronoTenEventCount()
		{
			System->OnActionEvent.Remove(Handle);
		}
		UCataclysmAbilitySystemComponent* System = nullptr;
		FName Event;
		int32 Count = 0;
		FDelegateHandle Handle;
	};

	/** Destroy the actors still standing at the end of a case. */
	void ChronoTenClear(const TArray<AActor*>& Standing)
	{
		for (AActor* Each : Standing)
		{
			if (IsValid(Each))
			{
				Each->Destroy();
			}
		}
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a `using
// namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenClockTest,
	"Cataclysm.SetBonus.Chrono10.NoRiftWithTheStatsAtNoughtAndOneThirtySecondsAfterTheyAreWornAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The clock, K4. An enemy stands 3 m off from the start. THE CONTROL IS READ FIRST: with the stats at nought no clock
 * is started and 30 s on no rift appears. Worn, the first step starts the clock at 30 s; a quarter of a second before
 * 30 s nothing is caught; at 30 s the rift appears where the wearer stands, catches the enemy, and the clock is 30 s
 * again.
 */
bool FCataclysmChronoTenClockTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Enemy = ChronoTenEnemyAt(World, FVector(3 * M, 0, 0));
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade()) || !TestNotNull(TEXT("set-up: an enemy 3 m off"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({Enemy}); };

	TestEqual(TEXT("control: with the stats at nought the first step catches nothing"), Wearer.Step(), 0);
	TestEqual(TEXT("control: and starts no clock"), Wearer.SecondsUntilReady(), -1.0f, ChronoTenClockAllowance);
	World->TimeSeconds += ChronoTenEverySeconds;
	TestEqual(TEXT("control: 30 s on it still catches nothing"), Wearer.Step(), 0);
	TestNull(TEXT("control: and no rift stands"), Wearer.Rift());

	Wearer.Wear();
	TestEqual(TEXT("worn, the first step catches nothing"), Wearer.Step(), 0);
	TestEqual(TEXT("and starts the clock at 30 s"), Wearer.SecondsUntilReady(), ChronoTenEverySeconds,
			  ChronoTenClockAllowance);

	World->TimeSeconds += ChronoTenEverySeconds - 0.25f;
	TestEqual(TEXT("29.75 s on, with an enemy 3 m off, nothing is caught"), Wearer.Step(), 0);
	TestNull(TEXT("and no rift stands"), Wearer.Rift());

	World->TimeSeconds += 0.25f;
	TestEqual(TEXT("at 30 s the rift appears and catches the enemy"), Wearer.Step(), 1);
	const ACataclysmGroundZone* Rift = Wearer.Rift();
	if (!TestNotNull(TEXT("a rift stands"), Rift))
	{
		return false;
	}
	TestTrue(TEXT("where the wearer stands"),
			 Rift->GetActorLocation().Equals(Wearer.Pawn->GetActorLocation(), 1.0f));
	TestEqual(TEXT("and the clock restarts as it appears: 30 s"), Wearer.SecondsUntilReady(), ChronoTenEverySeconds,
			  ChronoTenClockAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenWaitsTest,
	"Cataclysm.SetBonus.Chrono10.ReadyWithNoEnemyWithinFiveMetresItWaitsAndAppearsTheFirstStepOneIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The wait, K4. An enemy stands 7 m off, outside the 5 m. THE CONTROL IS READ FIRST: at 30 s it is ready and no rift
 * appears. Fifteen seconds later it still waits. A second enemy placed 4 m off is caught at the next step, the one at
 * 7 m is not, and the clock restarts.
 */
bool FCataclysmChronoTenWaitsTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Far = ChronoTenEnemyAt(World, FVector(7 * M, 0, 0));
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade()) || !TestNotNull(TEXT("set-up: an enemy 7 m off"), Far))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({Far}); };
	Wearer.Wear();
	Wearer.Step();

	World->TimeSeconds += ChronoTenEverySeconds;
	TestEqual(TEXT("control: at 30 s, with the only enemy 7 m off, nothing is caught"), Wearer.Step(), 0);
	TestNull(TEXT("and no rift stands"), Wearer.Rift());
	TestEqual(TEXT("and it is ready"), Wearer.SecondsUntilReady(), 0.0f, ChronoTenClockAllowance);

	World->TimeSeconds += 15.0f;
	TestEqual(TEXT("15 s later it still waits"), Wearer.Step(), 0);
	TestNull(TEXT("with no rift"), Wearer.Rift());

	ACataclysmEnemyCharacter* Near = ChronoTenEnemyAt(World, FVector(0, 4 * M, 0));
	if (!TestNotNull(TEXT("set-up: an enemy placed 4 m off"), Near))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({Near}); };
	TestEqual(TEXT("the next step catches the one 4 m off"), Wearer.Step(), 1);
	TestNotNull(TEXT("in a rift that now stands"), Wearer.Rift());
	TestTrue(TEXT("and stuns it"), ChronoTenIsStunned(Near));
	TestFalse(TEXT("and not the one 7 m off"), ChronoTenIsStunned(Far));
	TestEqual(TEXT("and the clock restarts as it appears: 30 s"), Wearer.SecondsUntilReady(), ChronoTenEverySeconds,
			  ChronoTenClockAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenFreezeTest,
	"Cataclysm.SetBonus.Chrono10.EachEnemyInsideOrWalkingInIsStunnedOnceForFiveSecondsByTheWearerAndABossIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The freeze, K5 and #10. An enemy 3 m east and a boss 3 m west stand inside when the rift appears; a third, 8 m north,
 * walks in after. THE CONTROL IS READ FIRST: before the rift nobody is stunned and the wearer has raised no crowd
 * control. Each common enemy caught is stunned for 5 s by the wearer, raising `crowd_control` on it once; the boss is
 * caught and not stunned, and raises none. One that stays inside, or walks out and back in, is not caught again.
 */
bool FCataclysmChronoTenFreezeTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* First = ChronoTenEnemyAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Boss = ChronoTenEnemyAt(World, FVector(-3 * M, 0, 0), /*RarityStep=*/4);
	ACataclysmEnemyCharacter* Walker = ChronoTenEnemyAt(World, FVector(0, 8 * M, 0));
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade())
		|| !TestNotNull(TEXT("set-up: an enemy 3 m east"), First)
		|| !TestNotNull(TEXT("set-up: a creature 3 m west"), Boss)
		|| !TestNotNull(TEXT("set-up: an enemy 8 m north"), Walker))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({First, Boss, Walker}); };
	if (!TestTrue(TEXT("set-up: the Boss rung is a boss"), Boss->IsBoss()))
	{
		return false;
	}
	FChronoTenEventCount CrowdControl(Wearer.AbilitySystem, TEXT("crowd_control"));
	Wearer.Wear();
	Wearer.Step();

	TestFalse(TEXT("control: before the rift the enemy 3 m east is not stunned"), ChronoTenIsStunned(First));
	TestEqual(TEXT("control: and the wearer has raised no crowd control"), CrowdControl.Count, 0);

	World->TimeSeconds += ChronoTenEverySeconds;
	TestEqual(TEXT("the rift catches the two inside it"), Wearer.Step(), 2);
	TestEqual(TEXT("the enemy 3 m east is stunned for 5 s"), ChronoTenStunLeft(First), ChronoTenStunSeconds, 0.01f);
	TestFalse(TEXT("the boss is not stunned, as any stun refuses one"), ChronoTenIsStunned(Boss));
	TestEqual(TEXT("and both are caught"), Wearer.Caught(), 2);
	TestEqual(TEXT("one crowd control is raised on the wearer, for the one stun that landed"), CrowdControl.Count, 1);
	TestFalse(TEXT("the enemy 8 m off is not stunned"), ChronoTenIsStunned(Walker));

	Walker->SetActorLocation(FVector(0, 4 * M, 0));
	TestEqual(TEXT("walking in to 4 m, it is caught at the next step"), Wearer.Step(), 1);
	TestEqual(TEXT("and stunned for 5 s"), ChronoTenStunLeft(Walker), ChronoTenStunSeconds, 0.01f);
	TestEqual(TEXT("and a second crowd control is raised on the wearer"), CrowdControl.Count, 2);

	World->TimeSeconds += 1.0f;
	TestEqual(TEXT("a step 1 s later catches nobody: the three inside were caught already"), Wearer.Step(), 0);
	TestEqual(TEXT("and the stun of the one 3 m east was not laid again: 4 s are left"), ChronoTenStunLeft(First),
			  ChronoTenStunSeconds - 1.0f, 0.01f);

	First->SetActorLocation(FVector(10 * M, 0, 0));
	TestEqual(TEXT("walked out to 10 m, it is caught by nothing"), Wearer.Step(), 0);
	First->SetActorLocation(FVector(3 * M, 0, 0));
	TestEqual(TEXT("and walked back in, it is not caught again"), Wearer.Step(), 0);
	TestEqual(TEXT("the rift has caught three in all"), Wearer.Caught(), 3);
	TestEqual(TEXT("and the wearer has raised two crowd controls in all"), CrowdControl.Count, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenLengthTest,
	"Cataclysm.SetBonus.Chrono10.TheRiftLastsASecondLongerForEachEnemyCaughtABossIncludedAndTenSecondsAtMost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The length, K1, K2, K6 and #10. Six enemies are brought onto a ring 3 m round the wearer, each 3 m from the next: one
 * stands there when the rift appears, a boss walks in second, four more after. THE CONTROL IS READ FIRST: before the
 * rift none stands and it has no length. One caught gives 6 s; the boss, whose stun is refused, gives 7; the next three
 * 8, 9 and 10; the sixth leaves it at 10, its whole length at most 10. Its zone is told each length, and the rift
 * stands 9.9 s after it appeared and not 10.1 s.
 */
bool FCataclysmChronoTenLengthTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade()))
	{
		return false;
	}

	// SIX PLACES ON A RING OF 3 M, 60 DEGREES APART, so each is 3 m from its neighbours. Every enemy is made 20 m off,
	// 2 m from the next, and moved onto its place when its turn comes. The second is the boss.
	TArray<FVector> Ring;
	TArray<ACataclysmEnemyCharacter*> Six;
	TArray<AActor*> Made;
	ON_SCOPE_EXIT { ChronoTenClear(Made); };
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const float Radians = FMath::DegreesToRadians(60.0f * Index);
		Ring.Add(FVector(3 * M * FMath::Cos(Radians), 3 * M * FMath::Sin(Radians), 0));
		ACataclysmEnemyCharacter* One = ChronoTenEnemyAt(
			World, Index == 0 ? Ring[0] : FVector(20 * M + 2 * M * Index, 20 * M, 0), Index == 1 ? 4 : 0);
		if (!TestNotNull(*FString::Printf(TEXT("set-up: enemy %d"), Index), One))
		{
			return false;
		}
		Six.Add(One);
		Made.Add(One);
	}
	if (!TestTrue(TEXT("set-up: the second is a boss"), Six[1]->IsBoss()))
	{
		return false;
	}
	Wearer.Wear();
	Wearer.Step();

	TestEqual(TEXT("control: before the rift none stands and it has no length"), Wearer.RiftLength(), 0.0f,
			  ChronoTenClockAllowance);

	World->TimeSeconds += ChronoTenEverySeconds;
	TestEqual(TEXT("the rift catches the one on the ring"), Wearer.Step(), 1);
	ACataclysmGroundZone* Rift = Wearer.Rift();
	if (!TestNotNull(TEXT("a rift stands"), Rift))
	{
		return false;
	}
	TestEqual(TEXT("with one enemy caught it lasts 6 s: its 5 and 1"), Wearer.RiftLength(),
			  ChronoTenBaseSeconds + ChronoTenSecondsPerEnemy, ChronoTenClockAllowance);
	TestEqual(TEXT("and its zone lasts 6 s"), Rift->GetLifeSpan(), ChronoTenBaseSeconds + ChronoTenSecondsPerEnemy,
			  0.01f);

	Six[1]->SetActorLocation(Ring[1]);
	TestEqual(TEXT("the boss walking in is caught"), Wearer.Step(), 1);
	TestFalse(TEXT("and not stunned"), ChronoTenIsStunned(Six[1]));
	TestEqual(TEXT("and still lengthens the rift to 7 s"), Wearer.RiftLength(), 7.0f, ChronoTenClockAllowance);

	for (int32 Index = 2; Index < 5; ++Index)
	{
		Six[Index]->SetActorLocation(Ring[Index]);
		Wearer.Step();
		TestEqual(*FString::Printf(TEXT("enemy %d walking in makes it %d s"), Index, 6 + Index), Wearer.RiftLength(),
				  6.0f + Index, ChronoTenClockAllowance);
	}

	Six[5]->SetActorLocation(Ring[5]);
	TestEqual(TEXT("the sixth walking in is caught"), Wearer.Step(), 1);
	TestEqual(TEXT("and leaves it at 10 s: its whole length is at most 10"), Wearer.RiftLength(), ChronoTenMostSeconds,
			  ChronoTenClockAllowance);
	TestEqual(TEXT("and its zone lasts 10 s"), Rift->GetLifeSpan(), ChronoTenMostSeconds, 0.01f);
	TestEqual(TEXT("the rift has caught six"), Wearer.Caught(), 6);

	World->TimeSeconds += ChronoTenMostSeconds - 0.1f;
	TestNotNull(TEXT("9.9 s after it appeared it still stands"), Wearer.Rift());
	World->TimeSeconds += 0.2f;
	TestNull(TEXT("10.1 s after, it has ended"), Wearer.Rift());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenMoreDamageTest,
	"Cataclysm.SetBonus.Chrono10.ACaughtEnemyTakesSixTimesTheWearersAnotherPlayersAndAMinionsBlowWhileFrozen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The 500% more, K7, from three sources. An enemy 3 m east and a boss 3 m west are caught; an enemy 20 m off is not.
 * THE CONTROL IS READ FIRST: before the rift, a direct blow of 100 from the wearer, one from another player and one
 * swing of that player's imp, on each of the three. Inside the frozen time the caught enemy takes six times its own
 * control from each, the boss its own control, the enemy not caught its own control; 5.25 s on, the freeze over, the
 * caught enemy takes its control again.
 */
bool FCataclysmChronoTenMoreDamageTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	// THE CRITICAL STRIKE ROLL PINNED NEVER TO CRIT, ruled 2026-10-19 by the coordinating session: a comparison of two
	// blows' figures must not rest on neither of them critically striking.
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned to 100, so no blow here critically strikes"),
			NeverCritical.Variable))
	{
		return false;
	}

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Other = ChronoTenPlayerAt(World, FVector(0, -10 * M, 0));
	ACataclysmEnemyCharacter* Caught = ChronoTenEnemyAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Boss = ChronoTenEnemyAt(World, FVector(-3 * M, 0, 0), /*RarityStep=*/4);
	ACataclysmEnemyCharacter* Apart = ChronoTenEnemyAt(World, FVector(0, 20 * M, 0));
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade()) || !TestNotNull(TEXT("set-up: another player"), Other)
		|| !TestNotNull(TEXT("set-up: an enemy 3 m east"), Caught)
		|| !TestNotNull(TEXT("set-up: a creature 3 m west"), Boss)
		|| !TestNotNull(TEXT("set-up: an enemy 20 m off"), Apart))
	{
		return false;
	}
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Other, FVector(0, -12 * M, 0), /*Lifetime=*/60.0f,
													/*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
	ON_SCOPE_EXIT { ChronoTenClear({Caught, Boss, Apart, Imp, Other}); };
	if (!TestNotNull(TEXT("set-up: the other player's imp"), Imp)
		|| !TestTrue(TEXT("set-up: the Boss rung is a boss"), Boss->IsBoss()))
	{
		return false;
	}
	Wearer.Wear();
	Wearer.Step();

	// THE CONTROLS, each enemy's own figure from each source before any rift.
	AActor* const Three[3] = {Caught, Boss, Apart};
	float Direct[3];
	float Players[3];
	float Minions[3];
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AActor* Each = Three[Index];
		Direct[Index] = ChronoTenBlowFrom(Wearer.Pawn, Each);
		Players[Index] = ChronoTenBlowFrom(Other, Each);
		Minions[Index] = ChronoTenImpBlow(Imp, Each);
	}
	TestTrue(FString::Printf(TEXT("control: before the rift the wearer's blow takes %.2f from the enemy to be caught"),
							 Direct[0]),
			 Direct[0] > 0.0f);
	TestTrue(FString::Printf(TEXT("control: the other player's %.2f"), Players[0]), Players[0] > 0.0f);
	TestTrue(FString::Printf(TEXT("control: the imp's %.2f"), Minions[0]), Minions[0] > 0.0f);

	World->TimeSeconds += ChronoTenEverySeconds;
	TestEqual(TEXT("the rift catches the two inside it"), Wearer.Step(), 2);

	TestEqual(TEXT("a direct blow of the wearer's takes six times its control from the caught enemy"),
			  ChronoTenBlowFrom(Wearer.Pawn, Caught), ChronoTenMultiplier * Direct[0], 0.05f);
	TestEqual(TEXT("and its control from the boss the stun refused"), ChronoTenBlowFrom(Wearer.Pawn, Boss), Direct[1],
			  0.05f);
	TestEqual(TEXT("and its control from the enemy not caught"), ChronoTenBlowFrom(Wearer.Pawn, Apart), Direct[2],
			  0.05f);
	TestEqual(TEXT("another player's blow takes six times its control from the caught enemy"),
			  ChronoTenBlowFrom(Other, Caught), ChronoTenMultiplier * Players[0], 0.05f);
	TestEqual(TEXT("and its control from the boss"), ChronoTenBlowFrom(Other, Boss), Players[1], 0.05f);
	TestEqual(TEXT("a minion's blow takes six times its control from the caught enemy"), ChronoTenImpBlow(Imp, Caught),
			  ChronoTenMultiplier * Minions[0], 0.05f);
	TestEqual(TEXT("and its control from the boss"), ChronoTenImpBlow(Imp, Boss), Minions[1], 0.05f);

	World->TimeSeconds += ChronoTenStunSeconds + 0.25f;
	TestEqual(TEXT("5.25 s on, the freeze over, the wearer's blow takes its control from the caught enemy again"),
			  ChronoTenBlowFrom(Wearer.Pawn, Caught), Direct[0], 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenTickTest,
	"Cataclysm.SetBonus.Chrono10.ADamageOverTimeTickOnACaughtEnemyIsSixTimesTheTickOnOneNotCaught",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The 500% more on a tick, K7. An enemy 3 m east is caught; an enemy 20 m off is not. Both are given the same burn by
 * the wearer, 10 a tick with none of its stats, and the clock runs 2.5 s: two ticks each. THE CONTROL IS READ FIRST:
 * the enemy not caught lost something. The caught one lost six times as much.
 */
bool FCataclysmChronoTenTickTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	// THE CRITICAL STRIKE ROLL PINNED NEVER TO CRIT, ruled 2026-10-19 by the coordinating session: a comparison of two
	// blows' figures must not rest on neither of them critically striking.
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned to 100, so no blow here critically strikes"),
			NeverCritical.Variable))
	{
		return false;
	}

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Caught = ChronoTenEnemyAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Apart = ChronoTenEnemyAt(World, FVector(0, 20 * M, 0));
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade()) || !TestNotNull(TEXT("set-up: an enemy 3 m east"), Caught)
		|| !TestNotNull(TEXT("set-up: an enemy 20 m off"), Apart))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({Caught, Apart}); };
	Wearer.Wear();
	Wearer.Step();
	World->TimeSeconds += ChronoTenEverySeconds;
	if (!TestEqual(TEXT("set-up: the rift catches the one inside it"), Wearer.Step(), 1))
	{
		return false;
	}

	for (ACataclysmEnemyCharacter* Each : {Caught, Apart})
	{
		TestTrue(TEXT("set-up: a burn is laid"),
				 UCataclysmSkillEffects::ApplyDamageOverTime(Wearer.Pawn, Each, /*DamagePerTick=*/10.0f,
															/*DurationSeconds=*/4.0f, UCataclysmSkillEffects::BurnTag(),
															/*bScalesWithInstigator=*/false));
	}
	CataclysmTestWorld::RunClock(World, 2.5f);

	const float Ordinary = ChronoTenLost(Apart);
	TestTrue(FString::Printf(TEXT("control: the enemy not caught lost %.2f to its burn"), Ordinary), Ordinary > 0.0f);
	TestEqual(TEXT("the caught enemy lost six times as much to the same burn"), ChronoTenLost(Caught),
			  ChronoTenMultiplier * Ordinary, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenResistedTest,
	"Cataclysm.SetBonus.Chrono10.AnEnemyWithCrowdControlResistanceTakesSixTimesOnlyForItsShortenedStun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The 500% more lasts the stun as it was laid, the coordinating session's reading of K7. An enemy 3 m west holds 50
 * crowd control resistance; one 3 m east holds none. THE CONTROL IS READ FIRST: before the rift a blow of 100 on each,
 * and no hold on either. Caught, the plain one is stunned for 5 s and the resisting one for 2.5 s, and each is held to
 * take more for its own stun's seconds. 3 s on, a blow takes its control from the resisting one and six times its
 * control from the plain one; 5.25 s on, its control from both.
 */
bool FCataclysmChronoTenResistedTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	// THE CRITICAL STRIKE ROLL PINNED NEVER TO CRIT, ruled 2026-10-19 by the coordinating session: a comparison of two
	// blows' figures must not rest on neither of them critically striking.
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	if (!TestNotNull(TEXT("set-up: the critical strike roll is pinned to 100, so no blow here critically strikes"),
			NeverCritical.Variable))
	{
		return false;
	}

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Plain = ChronoTenEnemyAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Resisting = ChronoTenEnemyAt(World, FVector(-3 * M, 0, 0));
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade()) || !TestNotNull(TEXT("set-up: an enemy 3 m east"), Plain)
		|| !TestNotNull(TEXT("set-up: an enemy 3 m west"), Resisting))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({Plain, Resisting}); };
	Resisting->GetAbilitySystemComponent()->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetCrowdControlResistanceAttribute(), 50.0f);
	Wearer.Wear();
	Wearer.Step();

	const float PlainControl = ChronoTenBlowFrom(Wearer.Pawn, Plain);
	const float ResistingControl = ChronoTenBlowFrom(Wearer.Pawn, Resisting);
	TestTrue(FString::Printf(TEXT("control: before the rift a blow takes %.2f from the plain enemy"), PlainControl),
			 PlainControl > 0.0f);
	TestEqual(TEXT("control: and neither is held to take more"),
			  UCataclysmDamageTakenMultiplier::SecondsLeftOn(Plain)
				  + UCataclysmDamageTakenMultiplier::SecondsLeftOn(Resisting),
			  0.0f, ChronoTenClockAllowance);

	World->TimeSeconds += ChronoTenEverySeconds;
	TestEqual(TEXT("the rift catches both"), Wearer.Step(), 2);
	TestEqual(TEXT("the plain enemy is stunned for 5 s"), ChronoTenStunLeft(Plain), ChronoTenStunSeconds, 0.01f);
	TestEqual(TEXT("the one holding 50 crowd control resistance for 2.5 s"), ChronoTenStunLeft(Resisting),
			  ChronoTenStunSeconds * 0.5f, 0.01f);
	TestEqual(TEXT("the plain one is held to take more for 5 s"), UCataclysmDamageTakenMultiplier::SecondsLeftOn(Plain),
			  ChronoTenStunSeconds, 0.01f);
	TestEqual(TEXT("and the resisting one for its 2.5 s and not for 5"),
			  UCataclysmDamageTakenMultiplier::SecondsLeftOn(Resisting), ChronoTenStunSeconds * 0.5f, 0.01f);

	World->TimeSeconds += 3.0f;
	TestEqual(TEXT("3 s on, a blow takes its control from the resisting one"), ChronoTenBlowFrom(Wearer.Pawn, Resisting),
			  ResistingControl, 0.05f);
	TestEqual(TEXT("and six times its control from the plain one"), ChronoTenBlowFrom(Wearer.Pawn, Plain),
			  ChronoTenMultiplier * PlainControl, 0.05f);

	World->TimeSeconds += 2.25f;
	TestEqual(TEXT("5.25 s on, its control from the plain one too"), ChronoTenBlowFrom(Wearer.Pawn, Plain), PlainControl,
			  0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenDrawnTest,
	"Cataclysm.SetBonus.Chrono10.TheRiftIsDrawnAsAGroundZoneAtItsRadiusForItsLengthAndDrawnAgainWhenItGrows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * What the player sees, K9: a ground zone at its radius for its length, and nothing else. Read through what
 * `UCataclysmGroundEffect::PlayFor` was last asked, since no automation test can see a drawing. THE CONTROL IS READ
 * FIRST: an ordinary zone of the wearer's is drawn once when it is placed and is a persistent area of its owner's.
 * The rift is drawn once as it appears, 5 m in radius for its 6 s, where it stands; it deals nothing and is no persistent
 * area. An enemy caught 1 s later ends that drawing and asks for one for the 6 s then left of its 7, and its zone lasts
 * those 6 s.
 */
bool FCataclysmChronoTenDrawnTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* First = ChronoTenEnemyAt(World, FVector(3 * M, 0, 0));
	ACataclysmEnemyCharacter* Walker = ChronoTenEnemyAt(World, FVector(0, 8 * M, 0));
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade()) || !TestNotNull(TEXT("set-up: an enemy 3 m east"), First)
		|| !TestNotNull(TEXT("set-up: an enemy 8 m north"), Walker))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({First, Walker}); };

	const int32 AskedForTheOrdinary = UCataclysmGroundEffect::TimesAsked;
	ACataclysmGroundZone* Ordinary =
		ACataclysmGroundZone::Spawn(Wearer.Pawn, FVector(0, -6 * M, 0), 1 * M, /*Duration=*/10.0f, /*DamagePerTick=*/1.0f);
	if (!TestNotNull(TEXT("set-up: an ordinary zone of the wearer's"), Ordinary))
	{
		return false;
	}
	TestEqual(TEXT("control: an ordinary zone is drawn once as it is placed"),
			  UCataclysmGroundEffect::TimesAsked - AskedForTheOrdinary, 1);
	TestFalse(TEXT("control: and is a persistent area of its owner's"), Ordinary->bIsNotAPersistentArea);
	Ordinary->Destroy();

	Wearer.Wear();
	Wearer.Step();
	World->TimeSeconds += ChronoTenEverySeconds;
	const int32 Asked = UCataclysmGroundEffect::TimesAsked;
	const int32 Ended = UCataclysmGroundEffect::TimesEndAsked;
	UCataclysmGroundEffect::LastRadiusCm = 0.0f;
	UCataclysmGroundEffect::LastDuration = 0.0f;
	TestEqual(TEXT("the rift appears and catches one"), Wearer.Step(), 1);
	ACataclysmGroundZone* Rift = Wearer.Rift();
	if (!TestNotNull(TEXT("a rift stands"), Rift))
	{
		return false;
	}
	TestEqual(TEXT("it is drawn once as it appears"), UCataclysmGroundEffect::TimesAsked - Asked, 1);
	TestEqual(TEXT("5 m in radius"), UCataclysmGroundEffect::LastRadiusCm, ChronoTenRadiusMetres * M, 0.01f);
	TestEqual(TEXT("for its 6 s"), UCataclysmGroundEffect::LastDuration, ChronoTenBaseSeconds + ChronoTenSecondsPerEnemy,
			  0.01f);
	TestTrue(TEXT("where it stands"), UCataclysmGroundEffect::LastStart.Equals(Rift->GetActorLocation(), 1.0f));
	TestEqual(TEXT("it deals nothing"), Rift->DamagePerTick, 0.0f, 0.0001f);
	TestTrue(TEXT("and is no persistent area of its owner's"), Rift->bIsNotAPersistentArea);

	World->TimeSeconds += 1.0f;
	Walker->SetActorLocation(FVector(0, 4 * M, 0));
	TestEqual(TEXT("an enemy walking in 1 s later is caught"), Wearer.Step(), 1);
	TestEqual(TEXT("and ends the rift's drawing"), UCataclysmGroundEffect::TimesEndAsked - Ended, 1);
	TestEqual(TEXT("and asks for one more"), UCataclysmGroundEffect::TimesAsked - Asked, 2);
	TestEqual(TEXT("for the 6 s left of its 7"), UCataclysmGroundEffect::LastDuration, 6.0f, 0.01f);
	TestEqual(TEXT("and its zone lasts those 6 s"), Rift->GetLifeSpan(), 6.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenNotYourZoneTest,
	"Cataclysm.SetBonus.Chrono10.AnEnemyStandingInTheRiftIsNotStandingInOneOfYourPersistentAreas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The rift is not one of its owner's persistent areas for `target_in_your_zone` ("You deal 15%-30% increased damage to
 * enemies standing in your persistent AOE zones"). The caster holds the rift's rows and a critical strike chance of 100
 * only while the target stands in its zone, with every roll pinned to nought, so a blow critically strikes exactly when
 * the condition holds, as `ABonusForEnemiesStandingInYourZonesReachesOnlyThoseInAZoneYouOwn` reads it. THE CONTROL IS
 * READ FIRST: with an ordinary zone of the caster's under the enemy in the rift, a blow on it critically strikes. With
 * that zone gone and only the rift under it, it does not.
 */
bool FCataclysmChronoTenNotYourZoneTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	// AFTER THE WORLD, which pins the roll to 100 itself.
	const CataclysmTestWorld::FScopedCritRoll Critical(0.0f);

	ACataclysmPlayerCharacter* Keeper = ChronoTenPlayerAt(World, FVector(0, 30 * M, 0));
	if (!TestNotNull(TEXT("set-up: a player pawn to own the caster"), Keeper))
	{
		return false;
	}
	FChronoTenCaster Caster(World, FVector::ZeroVector, Keeper);
	ACataclysmEnemyCharacter* Enemy = ChronoTenEnemyAt(World, FVector(3 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an enemy 3 m off"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({Enemy, Keeper}); };

	TMap<FName, FCataclysmStatInputs> Stats = ChronoTenRows();
	FCataclysmStatModifier InYourZone;
	InYourZone.Bucket = ECataclysmStatBucket::Flat;
	InYourZone.Source = ECataclysmModifierSource::Enchantment;
	InYourZone.Value = 100.0f;
	InYourZone.Condition = ECataclysmStatCondition::TargetStandsInYourZone;
	FCataclysmStatInputs& Chance = Stats.FindOrAdd(FName(TEXT("crit_chance")));
	Chance.Base = 0.0f;
	Chance.Modifiers = {InYourZone};
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Stats));

	Caster.Step();
	World->TimeSeconds += ChronoTenEverySeconds;
	if (!TestEqual(TEXT("set-up: the caster's rift catches the enemy"), Caster.Step(), 1)
		|| !TestNotNull(TEXT("set-up: and stands"), Caster.Rift()))
	{
		return false;
	}

	const auto Strike = [&Caster, Enemy]()
	{
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Caster.Actor, Enemy, 100.0f, FGameplayTagContainer(), FCataclysmHitDelivery(),
										 &Result);
		return Result.bWasCritical;
	};
	ACataclysmGroundZone* Ordinary =
		ACataclysmGroundZone::Spawn(Caster.Actor, Enemy->GetActorLocation(), 1 * M, /*Duration=*/10.0f, 1.0f);
	if (!TestNotNull(TEXT("set-up: an ordinary zone of the caster's under the enemy"), Ordinary))
	{
		return false;
	}
	TestTrue(TEXT("control: with an ordinary zone of the caster's under it, a blow on the enemy carries the bonus"),
			 Strike());
	Ordinary->Destroy();
	TestTrue(TEXT("set-up: the rift still covers the enemy"), Caster.Rift()->Covers(Enemy->GetActorLocation()));
	TestFalse(TEXT("with only the rift under it, a blow on it does not: the rift is no persistent area of the caster's"),
			  Strike());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenOnlyOneTest,
	"Cataclysm.SetBonus.Chrono10.YourOnlyPersistentAreaRuleEndsYourEarlierZoneAndNotTheRift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The rift is not one of its owner's persistent areas for "You can only have 1 persistent AOE effect active at a
 * time". The caster holds the rift's rows, `only_one_persistent_area` and `zone_at_impact_seconds` at 3, and presses a
 * Strike all round of 3 m that is a spell, which then leaves a zone under its user, as
 * `ARowGivesAZoneOnlyToASkillThatStatesNoGround` presses it. THE CASTER HOLDS EVERY RESOURCE THE STRIKE COSTS: 1000
 * mana and the class resource set, as the caster of that test does. THE CONTROL IS READ FIRST: the earlier ordinary zone
 * of the caster's is ended by the Strike's new one. The rift is not.
 */
bool FCataclysmChronoTenOnlyOneTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Keeper = ChronoTenPlayerAt(World, FVector(0, 30 * M, 0));
	if (!TestNotNull(TEXT("set-up: a player pawn to own the caster"), Keeper))
	{
		return false;
	}
	FChronoTenCaster Caster(World, FVector::ZeroVector, Keeper);
	ACataclysmEnemyCharacter* Enemy = ChronoTenEnemyAt(World, FVector(4 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an enemy 4 m off"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({Enemy, Keeper}); };
	Caster.AbilitySystem->SetStatInputs(ChronoTenRows({
		{FName(UCataclysmDamageCalculation::OnlyOnePersistentAreaStat), 1.0f},
		{FName(UCataclysmDamageCalculation::ZoneAtImpactSecondsStat), 3.0f}}));

	Caster.Step();
	World->TimeSeconds += ChronoTenEverySeconds;
	if (!TestEqual(TEXT("set-up: the caster's rift catches the enemy"), Caster.Step(), 1))
	{
		return false;
	}
	ACataclysmGroundZone* Rift = Caster.Rift();
	ACataclysmGroundZone* Ordinary =
		ACataclysmGroundZone::Spawn(Caster.Actor, FVector(0, 3 * M, 0), 1 * M, /*Duration=*/10.0f, 1.0f);
	if (!TestNotNull(TEXT("set-up: the rift stands"), Rift)
		|| !TestNotNull(TEXT("set-up: an earlier ordinary zone of the caster's"), Ordinary))
	{
		return false;
	}

	const FGameplayAbilitySpecHandle Handle = Caster.AbilitySystem->GiveAbilityInSlot(
		UCataclysmStrikeSkill::StaticClass(), ECataclysmAbilitySlot::Movement, /*Level=*/100, Caster.Actor);
	FGameplayAbilitySpec* Spec = Handle.IsValid() ? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
	UCataclysmSkillTemplate* Skill = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a Strike"), Skill))
	{
		return false;
	}
	Skill->SkillName = TEXT("A Strike a row gives a zone");
	Skill->Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=3; Angle=360"));
	Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Item.Weapon.Wand, Element.Demonic, Type.Spell"));
	if (!TestTrue(TEXT("set-up: the Strike is used"), Caster.AbilitySystem->TryActivateAbility(Handle)))
	{
		return false;
	}

	TestFalse(TEXT("control: the caster's earlier ordinary zone is ended by the Strike's new one"), IsValid(Ordinary));
	TestTrue(TEXT("and the rift is not: it is no persistent area of the caster's"), IsValid(Rift));
	TestNotNull(TEXT("and still stands for the caster"), Caster.Rift());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmChronoTenRegenerationStepTest,
	"Cataclysm.SetBonus.Chrono10.TheRegenerationStepPlacesTheRiftWithNoCallByHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The call on the regeneration step, which every test above makes by hand. The wearer is a player pawn, whose
 * regeneration timer `CataclysmTestWorld::RunClock` fires every quarter of a second; an enemy stands 3 m off. THE
 * CONTROL IS READ FIRST: before the clock runs there is no clock. 29.5 s of the world's clock later the steps have
 * started it and no rift stands, since the first step came a quarter of a second in; 1.5 s after that a rift stands
 * and has caught the enemy.
 */
bool FCataclysmChronoTenRegenerationStepTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusChronoTenTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FChronoTenWearer Wearer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Enemy = ChronoTenEnemyAt(World, FVector(3 * M, 0, 0));
	if (!TestTrue(TEXT("set-up: a wearer"), Wearer.IsMade()) || !TestNotNull(TEXT("set-up: an enemy 3 m off"), Enemy))
	{
		return false;
	}
	ON_SCOPE_EXIT { ChronoTenClear({Enemy}); };
	Wearer.Wear();

	TestEqual(TEXT("control: before the clock runs there is no clock"), Wearer.SecondsUntilReady(), -1.0f,
			  ChronoTenClockAllowance);
	CataclysmTestWorld::RunClock(World, ChronoTenEverySeconds - 0.5f);
	TestTrue(FString::Printf(TEXT("29.5 s on the steps have started the clock: %.2f s left"), Wearer.SecondsUntilReady()),
			 Wearer.SecondsUntilReady() > 0.0f);
	TestNull(TEXT("and no rift stands"), Wearer.Rift());
	CataclysmTestWorld::RunClock(World, 1.5f);
	TestNotNull(TEXT("1.5 s after that a rift stands"), Wearer.Rift());
	TestEqual(TEXT("and has caught the enemy"), Wearer.Caught(), 1);
	TestTrue(TEXT("which is stunned"), ChronoTenIsStunned(Enemy));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
