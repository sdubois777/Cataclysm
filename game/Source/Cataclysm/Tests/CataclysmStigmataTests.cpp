// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmStigmata.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Stigmata, the Flagellant limb's first node, as the project owner approved it
 * on 2026-11-24: "When you set an enemy alight, you are set alight as well for 4
 * seconds, taking 1% of your maximum health per second. +3% increased damage
 * over time per point."
 *
 * WHAT IS TESTED HERE IS THE FIRST SENTENCE, through the one door every burn
 * goes through: `UCataclysmSkillEffects::ApplyDamageOverTime`, which asks
 * `UCataclysmStigmata::AfterSettingAlight` for each damage over time laid on
 * another character. The second sentence is a plain increase, held by
 * `Cataclysm.PlainRows.EveryPlainDemonicRowReachesARealPlayerAtItsFigureTimesThePoints`.
 *
 * TWO FIGHTERS OF 1,000 HEALTH WITH NO RESISTANCE, so the self-burn's ticks land
 * whole: 10 a second, for four seconds.
 */
namespace CataclysmStigmataTest
{
	using Vital = UCataclysmVitalAttributeSet;

	struct FStigmataFighter
	{
		explicit FStigmataFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResist =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewAllResist);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 1000.0f);
		}

		~FStigmataFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Stigmata's switch, as its row records it at six points: a flat 6. */
		void HoldStigmata() const
		{
			FCataclysmStatModifier Switch;
			Switch.Bucket = ECataclysmStatBucket::Flat;
			Switch.Source = ECataclysmModifierSource::PassiveKeystone;
			Switch.Value = 6.0f;
			TMap<FName, FCataclysmStatInputs> Inputs;
			Inputs.FindOrAdd(FName(UCataclysmStigmata::SelfBurnStat)).Modifiers = {Switch};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		bool Burning() const
		{
			const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
			return Burn.IsValid() && AbilitySystem->HasMatchingGameplayTag(Burn);
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Set one fighter alight from another: 25 a second for 4 seconds, as a skill's burn. */
	bool SetAlight(const FStigmataFighter& From, const FStigmataFighter& To)
	{
		return UCataclysmSkillEffects::ApplyDamageOverTime(From.Actor, To.Actor, 25.0f, 4.0f,
														   UCataclysmSkillEffects::BurnTag());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStigmataSetsItsHolderAlightTest,
	"Cataclysm.Stigmata.SettingAnEnemyAlightSetsTheHolderAlightForFourSecondsAtOnePercentASecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The holder burns the enemy and burns too: 10 of its 1,000 health a second, for
 * four seconds, and then the burn is gone. The self-burn is a debuff the
 * Flagellant nodes count.
 */
bool FCataclysmStigmataSetsItsHolderAlightTest::RunTest(const FString&)
{
	using namespace CataclysmStigmataTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FStigmataFighter Holder(World);
	FStigmataFighter Enemy(World);
	Holder.HoldStigmata();
	TestEqual(TEXT("1% of 1,000 is 10 a tick"), UCataclysmStigmata::TickFor(1000.0f), 10.0f, 0.001f);

	if (!TestTrue(TEXT("set-up: the enemy is set alight"), SetAlight(Holder, Enemy))
		|| !TestTrue(TEXT("set-up: and is burning"), Enemy.Burning()))
	{
		return false;
	}
	TestTrue(TEXT("the holder is set alight as well"), Holder.Burning());
	TestEqual(TEXT("and carries it as a debuff the Flagellant nodes count"),
			  UCataclysmDebuffs::CountOn(Holder.AbilitySystem), 1);

	CataclysmTestWorld::RunClock(World, 1.05f);
	TestEqual(TEXT("its first tick takes 1% of its maximum health: 990"), Holder.Health(), 990.0f, 0.5f);

	CataclysmTestWorld::RunClock(World, 3.5f);
	TestEqual(TEXT("four ticks in four seconds: 960"), Holder.Health(), 960.0f, 0.5f);
	TestFalse(TEXT("and the burn has run out"), Holder.Burning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStigmataRefreshedTest,
	"Cataclysm.Stigmata.SettingTwoEnemiesAlightRefreshesOneSelfBurnRatherThanStackingTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Two enemies set alight in the same moment: one self-burn, ticking 10 a second,
 *  not two ticking 20. */
bool FCataclysmStigmataRefreshedTest::RunTest(const FString&)
{
	using namespace CataclysmStigmataTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FStigmataFighter Holder(World);
	FStigmataFighter First(World);
	FStigmataFighter Second(World);
	Holder.HoldStigmata();
	if (!TestTrue(TEXT("set-up: two enemies are set alight"), SetAlight(Holder, First) && SetAlight(Holder, Second)))
	{
		return false;
	}

	CataclysmTestWorld::RunClock(World, 1.05f);
	TestEqual(TEXT("one tick of one burn: 990, not 980"), Holder.Health(), 990.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStigmataOnlyABurnTest,
	"Cataclysm.Stigmata.ABleedSetsNothingAlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** "When you set an enemy alight": a bleed laid by the holder is no burn and
 *  lays none on the holder. */
bool FCataclysmStigmataOnlyABurnTest::RunTest(const FString&)
{
	using namespace CataclysmStigmataTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FStigmataFighter Holder(World);
	FStigmataFighter Enemy(World);
	Holder.HoldStigmata();
	if (!TestTrue(TEXT("set-up: the enemy is made to bleed"),
				  UCataclysmSkillEffects::ApplyDamageOverTime(Holder.Actor, Enemy.Actor, 25.0f, 4.0f,
															  UCataclysmDebuffs::BleedTag())))
	{
		return false;
	}
	TestFalse(TEXT("and the holder is not set alight"), Holder.Burning());
	TestEqual(TEXT("nor carrying anything"), UCataclysmDebuffs::CountOn(Holder.AbilitySystem), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStigmataControlTest,
	"Cataclysm.Stigmata.WithoutItSettingAnEnemyAlightLeavesTheSetterUnhurt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** THE CONTROL: the same burn from a fighter without Stigmata burns the enemy
 *  and nobody else. */
bool FCataclysmStigmataControlTest::RunTest(const FString&)
{
	using namespace CataclysmStigmataTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FStigmataFighter Setter(World);
	FStigmataFighter Enemy(World);
	if (!TestTrue(TEXT("set-up: the enemy is set alight"), SetAlight(Setter, Enemy))
		|| !TestTrue(TEXT("set-up: and is burning"), Enemy.Burning()))
	{
		return false;
	}
	TestFalse(TEXT("the setter is not set alight"), Setter.Burning());
	CataclysmTestWorld::RunClock(World, 1.05f);
	TestEqual(TEXT("and loses nothing"), Setter.Health(), 1000.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
