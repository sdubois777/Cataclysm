// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Void Splinter: a share of the target's current health on every tick. Issue #915.
 *
 * WHAT IS UNDER TEST. `game/Data/StatusEffects.csv` gives it "1% of current HP per
 * second over 4 seconds". `UCataclysmSkillEffects::ApplyShareOfHealthOverTime`
 * puts the share on the effect, and the target's `UCataclysmVitalAttributeSet`
 * turns it into damage from the health the target has when each tick lands. The
 * project owner's answer on #915 decides the rest: the damage over time damage
 * stat does not raise the share, frequency and duration still apply, and bosses
 * are protected. A chance to apply it, and a chance past 100% raising it, are
 * tested with the other ailments in `CataclysmAilmentTests.cpp`.
 *
 * THE CLOCK IS MOVED BY HAND, with `CataclysmTestWorld::RunClock`, because a
 * world built for a test is never ticked and the ticks are what is measured.
 *
 * EVERY TARGET STARTS AT 10,000 HEALTH with nothing between it and a tick, so 1%
 * of it is a round 100 and a tick takes exactly what it states.
 */
namespace CataclysmVoidSplinterTest
{
	using Combat = UCataclysmCombatAttributeSet;
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float StartingHealth = 10'000.0f;

	/** Requested by name, so a vocabulary that lost it reads as invalid. */
	FGameplayTag SplinterTag()
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("Keyword.DoT.VoidSplinter")), /*ErrorIfNotFound=*/false);
	}

	/**
	 * The largest number a running Void Splinter on this character carries
	 * under a name, or -1 when none is running.
	 */
	float CarriedOn(const UAbilitySystemComponent* AbilitySystem,
					const TCHAR* DataName)
	{
		float Carried = -1.0f;
		const FGameplayTag Tag = SplinterTag();
		if (!AbilitySystem || !Tag.IsValid())
		{
			return Carried;
		}

		for (const FActiveGameplayEffectHandle& Handle :
				AbilitySystem->GetActiveEffects(
					FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
						FGameplayTagContainer(Tag))))
		{
			if (const FActiveGameplayEffect* Active =
					AbilitySystem->GetActiveGameplayEffect(Handle))
			{
				Carried = FMath::Max(Carried, Active->Spec.GetSetByCallerMagnitude(
					FName(DataName), /*WarnIfNotFound=*/false, -1.0f));
			}
		}
		return Carried;
	}

	/** The share of current health one tick takes, as a fraction, or -1. */
	float ShareOn(const UAbilitySystemComponent* AbilitySystem)
	{
		return CarriedOn(AbilitySystem,
			UCataclysmSkillEffects::ShareOfCurrentHealthDataName);
	}

	/** What the running application stated, which is its share a second. */
	float StatedOn(const UAbilitySystemComponent* AbilitySystem)
	{
		return CarriedOn(AbilitySystem,
			UCataclysmSkillEffects::StatedMagnitudeDataName);
	}

	/** How long the running Void Splinter has left, or zero. */
	float SecondsLeftOn(const UAbilitySystemComponent* AbilitySystem)
	{
		float Longest = 0.0f;
		const FGameplayTag Tag = SplinterTag();
		if (AbilitySystem && Tag.IsValid())
		{
			for (const float Seconds : AbilitySystem->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
						 FGameplayTagContainer(Tag))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
		}
		return Longest;
	}

	float HealthOf(const UAbilitySystemComponent* AbilitySystem)
	{
		return AbilitySystem
			? AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute())
			: 0.0f;
	}

	/**
	 * A bare actor holding every attribute set, usable as either side.
	 *
	 * NAMED APART FROM THE OTHER FILES' HARNESSES, because the unity build puts
	 * several test files into one translation unit.
	 */
	struct FSplinterFighter
	{
		explicit FSplinterFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template
			// and a TObjectPtr deduces the wrapper rather than the set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAll =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAll);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			// The maximum first, because health is clamped to it.
			Set(Vital::GetMaxHealthAttribute(), StartingHealth);
			Set(Vital::GetHealthAttribute(), StartingHealth);
		}

		~FSplinterFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		/** Put a Void Splinter on the target, from this character. */
		bool Splinter(const FSplinterFighter& Target, float SharePerTick,
					  float DurationSeconds = 4.0f) const
		{
			return UCataclysmSkillEffects::ApplyShareOfHealthOverTime(
				Actor, Target.Actor, SharePerTick, DurationSeconds, SplinterTag());
		}

		float Health() const { return HealthOf(AbilitySystem); }

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/**
	 * A creature at a rung of the rarity ladder, at 10,000 health and with
	 * nothing between it and a tick.
	 *
	 * THE FIGURES ARE WRITTEN AFTER THE RARITY, AND DIRECTLY, because
	 * `SetRarityStep` rewrites the creature's stat block and scales its health,
	 * and every other setter the creature has does the same. So the two
	 * creatures a test compares differ in their rung and in nothing else.
	 *
	 * NO ARMOUR, NO RESISTANCE, NO EVASION AND NO BLOCK. Evasion and block are
	 * rolled for a tick of damage over time too, and a test depending on a
	 * random number is worse than none.
	 */
	ACataclysmEnemyCharacter* SpawnCreature(UWorld* World, int32 RarityStep,
											const FVector& Where)
	{
		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Creature)
		{
			return nullptr;
		}

		Creature->SetRarityStep(RarityStep);

		if (UAbilitySystemComponent* AbilitySystem =
				UCataclysmTargeting::AbilitySystemOf(Creature))
		{
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetMaxHealthAttribute(), StartingHealth);
			AbilitySystem->SetNumericAttributeBase(
				Vital::GetHealthAttribute(), StartingHealth);
			AbilitySystem->SetNumericAttributeBase(Combat::GetArmorAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(Combat::GetEvasionAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(
				Combat::GetBlockChanceAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}
		return Creature;
	}
}

#define CATACLYSM_VOID_SPLINTER_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_VOID_SPLINTER_TEST(FCataclysmVoidSplinterTickArithmeticTest,
	"Cataclysm.VoidSplinter.ATickIsAShareOfCurrentHealthAndStopsABossAtHalf")
{
	// THE ARITHMETIC ALONE, which the target's attribute set asks for on every
	// tick. Written out rather than worked out, so a slip in the rule fails here
	// instead of agreeing with itself.
	struct FRow
	{
		const TCHAR* Case;
		float Share;
		float Health;
		float MaxHealth;
		bool bIsBoss;
		float Expected;
	};
	const FRow Rows[] = {
		{ TEXT("1% of a full 10,000 is 100"),
		  0.01f, 10'000.0f, 10'000.0f, false, 100.0f },
		{ TEXT("1% of the 4,000 left is 40, and not 1% of the maximum"),
		  0.01f, 4'000.0f, 10'000.0f, false, 40.0f },
		{ TEXT("a boss well above half takes the whole share"),
		  0.01f, 10'000.0f, 10'000.0f, true, 100.0f },
		{ TEXT("a tick that would cross half stops a boss at it"),
		  0.30f, 7'000.0f, 10'000.0f, true, 2'000.0f },
		{ TEXT("a boss at half takes nothing"),
		  0.30f, 5'000.0f, 10'000.0f, true, 0.0f },
		{ TEXT("nor does a boss already below it"),
		  0.30f, 4'000.0f, 10'000.0f, true, 0.0f },
		{ TEXT("while anything else below half still takes its share"),
		  0.30f, 4'000.0f, 10'000.0f, false, 1'200.0f },
		{ TEXT("no share takes nothing"),
		  0.0f, 10'000.0f, 10'000.0f, false, 0.0f },
		{ TEXT("and nothing is taken from nothing"),
		  0.01f, 0.0f, 10'000.0f, false, 0.0f },
	};

	for (const FRow& Row : Rows)
	{
		TestEqual(Row.Case,
			UCataclysmSkillEffects::ShareOfHealthTick(Row.Share, Row.Health,
				Row.MaxHealth, Row.bIsBoss),
			Row.Expected, 0.001f);
	}

	TestEqual(TEXT("a boss is held at half its maximum"),
		UCataclysmSkillEffects::BossFloorShareOfMaxHealth, 0.5f);
	return true;
}

CATACLYSM_VOID_SPLINTER_TEST(FCataclysmVoidSplinterTicksTest,
	"Cataclysm.VoidSplinter.EachTickTakesAShareOfTheHealthLeftWhenItLands")
{
	using namespace CataclysmVoidSplinterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	if (!TestTrue(TEXT("the vocabulary has Keyword.DoT.VoidSplinter"),
				  SplinterTag().IsValid()))
	{
		return false;
	}

	const FSplinterFighter Attacker(World);
	const FSplinterFighter Target(World);

	// THE ROW'S OWN FIGURES: 1% a tick for four seconds.
	TestTrue(TEXT("a Void Splinter of 1% a tick lands"),
		Attacker.Splinter(Target, 0.01f));
	TestEqual(TEXT("it carries its share"),
		ShareOn(Target.AbilitySystem), 0.01f, 0.0001f);
	TestEqual(TEXT("and states 1% a second, for the next application to be "
				   "compared with"),
		StatedOn(Target.AbilitySystem), 0.01f, 0.0001f);

	// NOTHING AS IT LANDS. The first tick is a second in, as for every other
	// damage over time.
	TestEqual(TEXT("nothing is taken as it lands"),
		Target.Health(), StartingHealth, 0.01f);

	// THE FIRST TICK, AT ONE SECOND: 1% of 10,000.
	CataclysmTestWorld::RunClock(World, 1.5f);
	const float AfterFirst = Target.Health();
	TestEqual(TEXT("the first tick takes 1% of 10,000, which is 100"),
		StartingHealth - AfterFirst, 100.0f, 0.01f);

	// THE SECOND, AT TWO SECONDS: 1% of the 9,900 left, which is 99. A share of
	// the maximum, or an amount worked out when the effect landed, would take
	// 100 again, and the damage of one the effect carries would take 1.
	CataclysmTestWorld::RunClock(World, 1.0f);
	TestEqual(TEXT("the second takes 1% of the 9,900 left, which is 99"),
		AfterFirst - Target.Health(), 99.0f, 0.01f);
	return true;
}

CATACLYSM_VOID_SPLINTER_TEST(FCataclysmVoidSplinterStatsTest,
	"Cataclysm.VoidSplinter.TheDamageStatLeavesTheShareAloneWhileFrequencyAndDurationApply")
{
	using namespace CataclysmVoidSplinterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE PROJECT OWNER'S ANSWER ON #915, 2026-09-11: "The damage stat does not
	// raise the 1%, while tick speed and duration still apply". Each attacker
	// below raises one of the three, where 100 is unchanged.
	const FSplinterFighter Harder(World);
	Harder.Set(Combat::GetDotDamageAttribute(), 300.0f);
	const FSplinterFighter Faster(World);
	Faster.Set(Combat::GetDotFrequencyAttribute(), 200.0f);
	const FSplinterFighter Longer(World);
	Longer.Set(Combat::GetDotDurationAttribute(), 200.0f);

	const FSplinterFighter HitHarder(World);
	const FSplinterFighter HitFaster(World);
	const FSplinterFighter HitLonger(World);

	TestTrue(TEXT("the three land"),
		Harder.Splinter(HitHarder, 0.01f) && Faster.Splinter(HitFaster, 0.01f)
		&& Longer.Splinter(HitLonger, 0.01f));

	// THE DAMAGE STAT MOVES NOTHING. Three times the damage over time damage
	// would make it 3% a tick if it multiplied the share, as it multiplies
	// every flat amount a tick.
	TestEqual(TEXT("300% damage over time damage still carries 1% a tick"),
		ShareOn(HitHarder.AbilitySystem), 0.01f, 0.0001f);

	// FREQUENCY HALVES THE GAP BETWEEN TICKS, so the same 1% a tick is 2% a
	// second.
	TestEqual(TEXT("200% frequency still takes 1% a tick"),
		ShareOn(HitFaster.AbilitySystem), 0.01f, 0.0001f);
	TestEqual(TEXT("and so states 2% a second"),
		StatedOn(HitFaster.AbilitySystem), 0.02f, 0.0001f);

	// DURATION DOUBLES THE ROW'S FOUR SECONDS.
	TestEqual(TEXT("200% duration runs for eight seconds"),
		SecondsLeftOn(HitLonger.AbilitySystem), 8.0f, 0.05f);

	// AND WHAT THE TICKS TAKE SAYS THE SAME. In 1.25 seconds the ordinary
	// interval ticks once, at one second, and the halved one twice.
	CataclysmTestWorld::RunClock(World, 1.25f);
	TestEqual(TEXT("the 300% damage attacker's one tick takes 100, not 300"),
		StartingHealth - HitHarder.Health(), 100.0f, 0.01f);
	TestEqual(TEXT("the 200% frequency attacker's two ticks take 100 and 99"),
		StartingHealth - HitFaster.Health(), 199.0f, 0.01f);
	return true;
}

CATACLYSM_VOID_SPLINTER_TEST(FCataclysmVoidSplinterStrongestTest,
	"Cataclysm.VoidSplinter.TheStrongestApplicationWinsByShareASecond")
{
	using namespace CataclysmVoidSplinterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FSplinterFighter Attacker(World);
	const FSplinterFighter Target(World);

	TestTrue(TEXT("2% a tick lands"), Attacker.Splinter(Target, 0.02f));

	// A WEAKER ONE LEAVES THE RUNNING ONE ALONE AND ONLY REFRESHES HOW LONG IT
	// RUNS, the rule of issue #1503 that every damage over time keeps. Two
	// seconds in, the running one has two seconds left, and the weaker one's
	// four carry it back to four.
	CataclysmTestWorld::RunClock(World, 2.0f);
	TestEqual(TEXT("two seconds in, two are left"),
		SecondsLeftOn(Target.AbilitySystem), 2.0f, 0.1f);
	TestTrue(TEXT("a weaker 1% a tick is accepted"),
		Attacker.Splinter(Target, 0.01f));
	TestEqual(TEXT("and the running 2% stands"),
		ShareOn(Target.AbilitySystem), 0.02f, 0.0001f);
	TestEqual(TEXT("for four more seconds"),
		SecondsLeftOn(Target.AbilitySystem), 4.0f, 0.1f);

	// A STRONGER ONE REPLACES IT.
	TestTrue(TEXT("a stronger 3% a tick lands"), Attacker.Splinter(Target, 0.03f));
	TestEqual(TEXT("and replaces it"),
		ShareOn(Target.AbilitySystem), 0.03f, 0.0001f);

	// AND STRONGER IS MEASURED A SECOND, NOT A TICK. 1% a tick from an attacker
	// with twice the frequency is 2% a second, which beats 1.5% a tick at the
	// ordinary frequency.
	const FSplinterFighter Faster(World);
	Faster.Set(Combat::GetDotFrequencyAttribute(), 200.0f);
	const FSplinterFighter Other(World);
	TestTrue(TEXT("1% a tick twice a second lands"), Faster.Splinter(Other, 0.01f));
	TestTrue(TEXT("1.5% a tick once a second is accepted"),
		Attacker.Splinter(Other, 0.015f));
	TestEqual(TEXT("and the 2% a second stands against its 1.5%"),
		ShareOn(Other.AbilitySystem), 0.01f, 0.0001f);
	return true;
}

CATACLYSM_VOID_SPLINTER_TEST(FCataclysmVoidSplinterBossTest,
	"Cataclysm.VoidSplinter.ABossIsHeldAtHalfItsMaximumHealthAndAHeraldIsNot")
{
	using namespace CataclysmVoidSplinterTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE OWNER ASKED FOR BOSSES TO BE PROTECTED, on #915, and a floor at half
	// is the judgement `docs/DECISIONS.md` records. A boss is what
	// `ACataclysmEnemyCharacter::IsBoss` says, rung 4 and up. The Herald, one
	// rung below, is the creature it is compared with, because a threshold
	// checked from one side only cannot fail off by one.
	ACataclysmEnemyCharacter* Herald = SpawnCreature(World,
		ACataclysmEnemyCharacter::FirstBossRarityStep - 1, FVector(300.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Boss = SpawnCreature(World,
		ACataclysmEnemyCharacter::FirstBossRarityStep, FVector(600.0f, 0.0f, 0.0f));
	ON_SCOPE_EXIT
	{
		if (IsValid(Herald))
		{
			Herald->Destroy();
		}
		if (IsValid(Boss))
		{
			Boss->Destroy();
		}
	};
	if (!TestNotNull(TEXT("a Herald"), Herald) || !TestNotNull(TEXT("and a boss"), Boss))
	{
		return false;
	}
	TestFalse(TEXT("a Herald is not a boss"), Herald->IsBoss());
	TestTrue(TEXT("rung 4 is a boss"), Boss->IsBoss());

	const UAbilitySystemComponent* HeraldSystem =
		UCataclysmTargeting::AbilitySystemOf(Herald);
	const UAbilitySystemComponent* BossSystem =
		UCataclysmTargeting::AbilitySystemOf(Boss);

	// THE PRECONDITION: the two start level.
	TestEqual(TEXT("the Herald starts at 10,000"),
		HealthOf(HeraldSystem), StartingHealth, 0.01f);
	TestEqual(TEXT("and so does the boss"),
		HealthOf(BossSystem), StartingHealth, 0.01f);

	// 30% A TICK FOR TEN SECONDS, thirty times the row's share, so the second
	// tick already crosses half: 10,000, then 7,000, then 4,900.
	const FSplinterFighter Attacker(World);
	TestTrue(TEXT("a Void Splinter lands on the Herald"),
		UCataclysmSkillEffects::ApplyShareOfHealthOverTime(
			Attacker.Actor, Herald, 0.3f, 10.0f, SplinterTag()));
	TestTrue(TEXT("and on the boss"),
		UCataclysmSkillEffects::ApplyShareOfHealthOverTime(
			Attacker.Actor, Boss, 0.3f, 10.0f, SplinterTag()));

	// THE BOSS'S LOWEST IS WATCHED THROUGHOUT rather than read at the end, so a
	// tick taking it below half cannot be hidden by anything raising it again.
	float BossLowest = StartingHealth;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		CataclysmTestWorld::RunClock(World, 0.1f);
		BossLowest = FMath::Min(BossLowest, HealthOf(BossSystem));
	}

	// PRINTED EVERY RUN, because a check that passes writes nothing to the log.
	const float HeraldLeft = HealthOf(HeraldSystem);
	AddInfo(FString::Printf(
		TEXT("In six seconds the Herald fell to %.0f and the boss's lowest was "
			 "%.0f."), HeraldLeft, BossLowest));

	const float Half = StartingHealth * 0.5f;
	TestTrue(FString::Printf(TEXT("the ticks take the Herald below half, to %.0f"),
							 HeraldLeft),
		HeraldLeft < Half);
	TestEqual(TEXT("and hold the boss at half, no lower"), BossLowest, Half, 0.5f);
	return true;
}

#undef CATACLYSM_VOID_SPLINTER_TEST

#endif // WITH_AUTOMATION_TESTS
