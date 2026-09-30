// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * Projectile speed, a skill's range, and a gadget's attack range. Issue #1833,
 * ruled 2026-09-30 under the owner's delegation: `projectile_speed` quickens a
 * skill's projectile, `skill_range` lengthens the range a skill states, and
 * `minion_range` lengthens a minion's reach and notice radius at the summoning.
 * Each is asked with the tags of what it applies to, so a row's Required Tags
 * decide which skills and which minions it reaches.
 *
 * EACH TEST WRITES THE STAT LINES DIRECTLY, because what is under test is what
 * the engine does with the stats. The rows are measured in
 * CataclysmEnchantmentEffectTests.cpp, where a real enchantment is worn.
 */
namespace CataclysmProjectileRangeTest
{
	/** A character that can be found by an overlap, with mana to cast. */
	struct FCaster
	{
		explicit FCaster(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			check(Actor);

			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Sphere->InitSphereRadius(34.0f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->SetRootComponent(Sphere);
			Sphere->RegisterComponent();

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FCaster()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		/** Replace the stat lines with one increase to this stat, scoped by a tag cell. */
		void Increase(const TCHAR* Stat, float Percent, const TCHAR* RequiredTags) const
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Increased;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Percent;
			Row.RequiredTags = UCataclysmSkillShapes::TagsFromCell(RequiredTags);
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Row};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		/** A projectile skill granted in the Special slot with these params and tags. */
		UCataclysmProjectileSkill* Projectile(const TCHAR* ParamText, const TCHAR* TagCell) const
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				UCataclysmProjectileSkill::StaticClass(), ECataclysmAbilitySlot::Special,
				/*Level=*/1, Actor);
			FGameplayAbilitySpec* Spec =
				Handle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmProjectileSkill* Skill =
				Spec ? Cast<UCataclysmProjectileSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				Skill->SkillName = TEXT("Test Bolt");
				Skill->Params = UCataclysmSkillShapes::ParseParams(ParamText);
				Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
			}
			return Skill;
		}

		/** Fire a granted skill, and the projectile it put in the air or null. */
		ACataclysmProjectile* Fire(UCataclysmProjectileSkill* Skill) const
		{
			if (!Skill || !AbilitySystem->TryActivateAbility(
					Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false))
			{
				return nullptr;
			}
			return Skill->InFlight;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	constexpr const TCHAR* RangedBolt = TEXT("Range=10; Radius=1; Speed=2000");
	constexpr const TCHAR* RangedTags = TEXT("Type.Ranged, Type.Projectile");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmProjectileSpeedTest,
	"Cataclysm.ProjectileRange.ProjectileSpeedQuickensTheShotsItsTagsReachAndIsFloored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmProjectileSpeedTest::RunTest(const FString&)
{
	using namespace CataclysmProjectileRangeTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// FIRED, NOT ONLY ASKED: the projectile in the air carries the speed the
	// firing site passed, so a site that ignored the stat fails here.
	{
		FCaster Caster(World);
		Caster.Increase(UCataclysmSkillTemplate::ProjectileSpeedStat, 50.0f, TEXT("Type.Ranged"));
		ACataclysmProjectile* Shot = Caster.Fire(Caster.Projectile(RangedBolt, RangedTags));
		if (TestNotNull(TEXT("a ranged bolt is in the air"), Shot))
		{
			TestEqual(TEXT("50% increased projectile speed: twenty metres a second fly at thirty"),
					  Shot->SpeedCmPerSecond, 3000.0f, 0.01f);
		}
	}

	{
		FCaster Caster(World);
		Caster.Increase(UCataclysmSkillTemplate::ProjectileSpeedStat, 50.0f, TEXT("Type.Ranged"));
		const UCataclysmProjectileSkill* Thrown =
			Caster.Projectile(RangedBolt, TEXT("Type.Melee, Type.Projectile"));
		if (TestNotNull(TEXT("a thrown skill"), Thrown))
		{
			TestEqual(TEXT("a row scoped to ranged skills leaves another skill's speed alone"),
					  Thrown->ScaledProjectileSpeed(), 2000.0f, 0.01f);
		}
	}

	// THE FLOOR: a projectile's speed of nought is a beam, so enough "slower"
	// must not make one instant.
	{
		FCaster Caster(World);
		Caster.Increase(UCataclysmSkillTemplate::ProjectileSpeedStat, -95.0f, TEXT(""));
		const UCataclysmProjectileSkill* Slowed = Caster.Projectile(RangedBolt, RangedTags);
		const UCataclysmProjectileSkill* Beam =
			Caster.Projectile(TEXT("Range=10; Radius=1; Speed=0"), RangedTags);
		if (TestNotNull(TEXT("a slowed bolt"), Slowed) && TestNotNull(TEXT("a beam"), Beam))
		{
			TestEqual(TEXT("95% reduced is floored at a tenth of the stated speed"),
					  Slowed->ScaledProjectileSpeed(), 200.0f, 0.01f);
			TestEqual(TEXT("and a beam, stating no speed, stays a beam"),
					  Beam->ScaledProjectileSpeed(), 0.0f, 0.01f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillRangeTest,
	"Cataclysm.ProjectileRange.SkillRangeLengthensTheSkillsItsTagsReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillRangeTest::RunTest(const FString&)
{
	using namespace CataclysmProjectileRangeTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// WITH NO PLAYER CONTROLLER A SKILL IS AIMED ITS WHOLE RANGE ALONG THE
	// CASTER'S FACING, so the distance the shot has left to fly is its range.
	{
		FCaster Caster(World);
		Caster.Increase(UCataclysmSkillTemplate::SkillRangeStat, 50.0f, TEXT("Type.Ranged"));
		UCataclysmProjectileSkill* Bolt = Caster.Projectile(RangedBolt, RangedTags);
		if (!TestNotNull(TEXT("a ranged bolt"), Bolt))
		{
			return false;
		}
		TestEqual(TEXT("50% increased range: ten metres stated reach fifteen"),
				  Bolt->ScaledRangeCm(), 1500.0f, 0.01f);
		TestEqual(TEXT("and the reach a condition is judged over with it"),
				  Bolt->RequirementReachCm(), 1500.0f, 0.01f);
		ACataclysmProjectile* Shot = Caster.Fire(Bolt);
		if (TestNotNull(TEXT("the bolt is in the air"), Shot))
		{
			TestEqual(TEXT("and the shot is aimed fifteen metres out"),
					  Shot->RemainingRangeCm, 1500.0f, 0.5f);
		}
	}

	{
		FCaster Caster(World);
		Caster.Increase(UCataclysmSkillTemplate::SkillRangeStat, 50.0f, TEXT("Type.Ranged"));
		const UCataclysmProjectileSkill* Thrown =
			Caster.Projectile(RangedBolt, TEXT("Type.Melee, Type.Projectile"));
		if (TestNotNull(TEXT("a thrown skill"), Thrown))
		{
			TestEqual(TEXT("a row scoped to ranged skills leaves another skill's range alone"),
					  Thrown->ScaledRangeCm(), 1000.0f, 0.01f);
		}
	}

	// A SKILL NO CHARACTER HOLDS YET answers its stated range, and raises no
	// ensure for having no actor information.
	{
		UCataclysmProjectileSkill* Loose =
			NewObject<UCataclysmProjectileSkill>(GetTransientPackage());
		Loose->Params = UCataclysmSkillShapes::ParseParams(RangedBolt);
		TestEqual(TEXT("with no owner, the stated range"), Loose->ScaledRangeCm(), 1000.0f, 0.01f);
		TestEqual(TEXT("and the stated speed"), Loose->ScaledProjectileSpeed(), 2000.0f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGadgetRangeTest,
	"Cataclysm.ProjectileRange.GadgetRangeLengthensAMachinesReachAndNoticeAndNotAnImps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmGadgetRangeTest::RunTest(const FString&)
{
	using namespace CataclysmProjectileRangeTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FCaster Plain(World);
	FCaster Geared(World);
	Geared.Increase(TEXT("minion_range"), 40.0f, TEXT("Type.Deployable"));

	const auto Summon = [&](const FCaster& Summoner, const TCHAR* Type) -> ACataclysmMinion*
	{
		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(
			Summoner.Actor, FVector(300.0f, 0.0f, 0.0f), /*Lifetime=*/20.0f,
			/*bBurns=*/false, Type);
		if (Minion && Minion->TypeName != FString(Type))
		{
			AddError(FString::Printf(TEXT("DT_MinionTypes could not supply the %s row. Run "
										  "tools/generate_datatable_assets.py"), Type));
			return nullptr;
		}
		return Minion;
	};

	ACataclysmMinion* PlainTurret = Summon(Plain, TEXT("BoltTurret"));
	ACataclysmMinion* GearedTurret = Summon(Geared, TEXT("BoltTurret"));
	ACataclysmMinion* PlainImp = Summon(Plain, TEXT("Imp"));
	ACataclysmMinion* GearedImp = Summon(Geared, TEXT("Imp"));
	ON_SCOPE_EXIT
	{
		for (ACataclysmMinion* Each : {PlainTurret, GearedTurret, PlainImp, GearedImp})
		{
			if (IsValid(Each))
			{
				Each->Destroy();
			}
		}
	};
	if (!TestNotNull(TEXT("a turret"), PlainTurret) || !TestNotNull(TEXT("a second turret"), GearedTurret)
		|| !TestNotNull(TEXT("an imp"), PlainImp) || !TestNotNull(TEXT("a second imp"), GearedImp))
	{
		return false;
	}

	TestEqual(TEXT("40% increased gadget range: the turret reaches 40% further"),
			  GearedTurret->ReachCm, PlainTurret->ReachCm * 1.4f, 0.01f);
	TestEqual(TEXT("and notices 40% further, so it can fire at what it now reaches"),
			  GearedTurret->NoticeRadiusCm, PlainTurret->NoticeRadiusCm * 1.4f, 0.01f);
	TestEqual(TEXT("an imp, not a gadget, keeps its reach"),
			  GearedImp->ReachCm, PlainImp->ReachCm, 0.01f);
	TestEqual(TEXT("and its notice radius"),
			  GearedImp->NoticeRadiusCm, PlainImp->NoticeRadiusCm, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
