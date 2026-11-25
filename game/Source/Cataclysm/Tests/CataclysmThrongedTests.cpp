// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * THRONGED. The Ravager tree rework of 2026-11-24.
 *
 * The third Onslaught's second option, which replaced Weight Against Them: "Your Ultimate costs 5 less Fervour for each
 * enemy within 4 metres of you when you use it." Its row is `ultimate_fervour_cost_reduction`, flat 5, scaled by
 * `enemies_in_reach` at 4 metres. `UCataclysmGameplayAbility::FervourCostFor`, the one place a Fervour cost is worked
 * out, asks it with the skill's tags and the character's state and takes it off the cost, floored at nought.
 *
 * THE CASTER IS A CREATURE ON THE PLAYERS' SIDE, not a bare actor, for the reason
 * `ProbeScaledFervourPerEnemyInReach` gives: the walk that counts nearby enemies measures from the avatar and refuses
 * anything that is not an `ACataclysmCharacterBase`. It is given a Fervour pool and an Ultimate, as
 * `CataclysmUltimateFervourTests.cpp` gives its caster one. Nothing here states a count to the pipeline: the creatures
 * are spawned and the game counts them, the rule `CataclysmEnemiesInReachTests.cpp` records.
 *
 * THE ULTIMATE'S OWN COST IS READ, NOT ASSUMED: the cost with no row is measured first and every expectation is that
 * figure less the reduction, so a change to the slot table or the level factor does not break these.
 *
 * WRITTEN WITHOUT A BUILD, from the source, on 2026-11-24.
 */
namespace CataclysmThrongedTest
{
	constexpr float ThrongedMetre = 100.0f;

	/** A caster on the players' side, with a Fervour pool and an Ultimate. */
	struct FThrongedCaster
	{
		explicit FThrongedCaster(UWorld* World)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Actor->SetHealth(1000000.0f);
			Actor->SetAttackDamage(0.0f);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent());
			if (AbilitySystem)
			{
				AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
				AbilitySystem->SetNumericAttributeBase(
					UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
				AbilitySystem->SetNumericAttributeBase(
					UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
				const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
					UCataclysmSelfBuffSkill::StaticClass(), ECataclysmAbilitySlot::Ultimate, /*Level=*/100, Actor);
				FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
				Ultimate = Spec ? Cast<UCataclysmSelfBuffSkill>(Spec->GetPrimaryInstance()) : nullptr;
				if (Ultimate)
				{
					Ultimate->SkillName = TEXT("Test Ultimate");
					Ultimate->Params = UCataclysmSkillShapes::ParseParams(TEXT("Duration=6"));
				}
			}
		}

		~FThrongedCaster()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		bool IsComplete() const { return Actor && AbilitySystem && Ultimate; }

		/** Thronged's row: `PerEnemy` Fervour off for each enemy within four metres. Nothing at all for nought. */
		void HoldThronged(float PerEnemy) const
		{
			TMap<FName, FCataclysmStatInputs> Inputs;
			if (PerEnemy > 0.0f)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = PerEnemy;
				Row.Scale = ECataclysmStatScale::PerEnemyInReach;
				Row.ScaleStep = 1.0f;
				Row.ReachMetres = 4.0f;
				FCataclysmStatInputs& Line =
					Inputs.FindOrAdd(FName(UCataclysmGameplayAbility::UltimateFervourCostReductionStat));
				Line.Base = 0.0f;
				Line.Modifiers = {Row};
			}
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		float Cost() const { return Ultimate->FervourCostFor(AbilitySystem); }

		ACataclysmEnemyCharacter* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmSelfBuffSkill* Ultimate = nullptr;
	};

	/** A harmless monster, that many metres along the x axis. */
	ACataclysmEnemyCharacter* ThrongedMonsterAt(UWorld* World, float Metres)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(
			FVector(Metres * ThrongedMetre, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetRarityStep(0);
			Made->SetHealth(1000000.0f);
			Made->SetAttackDamage(0.0f);
		}
		return Made;
	}
}

#define CATACLYSM_THRONGED_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------------------------------------------------
// TH1. Five less for each enemy within four metres, and nothing for one outside them.
// --------------------------------------------------------------------------------------------------------------------
/** Prints "THRONGED COST". */
CATACLYSM_THRONGED_TEST(FCataclysmThrongedFiveLessPerEnemyTest,
	"Cataclysm.Thronged.TheUltimateCostsFiveLessFervourForEachEnemyWithinFourMetres")
{
	using namespace CataclysmThrongedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FThrongedCaster Caster(World);
	if (!TestTrue(TEXT("set-up: a caster with a pool and an Ultimate"), Caster.IsComplete()))
	{
		return false;
	}

	Caster.HoldThronged(0.0f);
	const float Stated = Caster.Cost();
	if (!TestTrue(TEXT("set-up: the Ultimate costs Fervour at all, above ten"), Stated > 10.0f))
	{
		return false;
	}
	Caster.HoldThronged(5.0f);
	const float Alone = Caster.Cost();

	ACataclysmEnemyCharacter* Outside = ThrongedMonsterAt(World, 5.0f);
	const float WithOneOutside = Caster.Cost();
	ACataclysmEnemyCharacter* Near = ThrongedMonsterAt(World, 2.0f);
	ACataclysmEnemyCharacter* AlsoNear = ThrongedMonsterAt(World, -3.0f);
	if (!TestTrue(TEXT("set-up: three monsters"), Outside && Near && AlsoNear))
	{
		return false;
	}
	const float WithTwoNear = Caster.Cost();

	Caster.HoldThronged(0.0f);
	const float WithoutTheRowInACrowd = Caster.Cost();
	AddInfo(FString::Printf(TEXT("THRONGED COST: stated %.2f; alone %.2f; one at 5 m %.2f; two near %.2f; no row in "
								 "the crowd %.2f"),
							Stated, Alone, WithOneOutside, WithTwoNear, WithoutTheRowInACrowd));

	TestEqual(TEXT("**alone, the row takes nothing off**"), Alone, Stated, 0.001f);
	TestEqual(TEXT("**an enemy five metres away is not within four, so nothing still**"), WithOneOutside, Stated, 0.001f);
	TestEqual(TEXT("**two within four metres take ten off**"), WithTwoNear, Stated - 10.0f, 0.001f);
	TestEqual(TEXT("CONTROL: without the row the crowd changes nothing"), WithoutTheRowInACrowd, Stated, 0.001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// TH2. Never below nought.
// --------------------------------------------------------------------------------------------------------------------
/**
 * A crowd worth more than the cost makes it free and does not pay the caster for casting. A row of 100 a head with two
 * near is far past any Ultimate's cost. Prints "THRONGED FLOOR".
 */
CATACLYSM_THRONGED_TEST(FCataclysmThrongedFloorTest,
	"Cataclysm.Thronged.ACrowdWorthMoreThanTheCostMakesItFreeAndNeverNegative")
{
	using namespace CataclysmThrongedTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FThrongedCaster Caster(World);
	if (!TestTrue(TEXT("set-up: a caster with a pool and an Ultimate"), Caster.IsComplete()))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: two monsters near"), ThrongedMonsterAt(World, 1.0f) && ThrongedMonsterAt(World, -1.0f)))
	{
		return false;
	}
	Caster.HoldThronged(100.0f);
	const float Free = Caster.Cost();
	AddInfo(FString::Printf(TEXT("THRONGED FLOOR: %.3f"), Free));
	TestEqual(TEXT("**the cost is nought, not below it**"), Free, 0.0f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// TH3. The stat is one the engine records with no attribute.
// --------------------------------------------------------------------------------------------------------------------
CATACLYSM_THRONGED_TEST(FCataclysmThrongedStatIsRecordedTest,
	"Cataclysm.Thronged.TheStatIsOneTheEngineRecordsWithNoAttribute")
{
	TestTrue(TEXT("ultimate_fervour_cost_reduction is on the exemption list"),
			 UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(
				 FString(UCataclysmGameplayAbility::UltimateFervourCostReductionStat)));
	TestEqual(TEXT("and is spelled as the row spells it"),
			  FString(UCataclysmGameplayAbility::UltimateFervourCostReductionStat),
			  FString(TEXT("ultimate_fervour_cost_reduction")));
	return true;
}

#undef CATACLYSM_THRONGED_TEST

#endif // WITH_AUTOMATION_TESTS
