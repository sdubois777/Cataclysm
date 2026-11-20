// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Templates/UniquePtr.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A ranged basic attack restores its mana when its shot lands. Issue #2722, approved by the coordinating session on
 * 2026-11-19.
 *
 * THE DESIGN ALREADY DECIDED IT: "The basic attack returns 6 mana each time it lands" (`docs/Cataclysm_GDD_v2.md`, "The
 * Basic Attack Restores Mana, and This Is Not a Generator"), with no exception for a weapon that shoots, and the
 * simulation's `Character.mana_income` counts the same income for every character. The game paid it only through
 * `UCataclysmSkillTemplate::HitTargets`, and the four ranged basic attacks -- the Wand's, the Crossbow's, the Staff's and
 * the Two-Handed Crossbow's, each one shot with a speed in `game/Data/ItemBases.csv` -- land through
 * `ACataclysmProjectile::HitOne`, which never passes through it. So a ranged character's basic attack paid nothing.
 *
 * EVERY SKILL HERE IS GRANTED AT LEVEL 100 and fires the Wand's own row, `Range=8.4; Radius=0.6; Pierce=0; Speed=2600`,
 * or that row with one key changed. At level 100 the slot's mana on hit is the 6 the sheet states, as
 * `Cataclysm.BasicAttack.ALandedBasicAttackPutsTheManaOnTheCharacter` reads it for a melee swing through `HitTargets`;
 * that test is the melee figure these are compared with. A shot is flown by hand, as
 * `CataclysmStalkerKitMechanismTests.cpp` flies one: a test world is never ticked.
 *
 * WRITTEN WITHOUT A BUILD. Phase 1 of the slice: every figure below is reasoned from the code, and the run that proves
 * them is registered in `docs/DECISIONS.md` before it happens.
 */
namespace CataclysmRangedManaTest
{
	constexpr float M = 100.0f;

	/** The Wand's basic attack, as `game/Data/ItemBases.csv` writes it. */
	constexpr const TCHAR* WandShot = TEXT("Range=8.4; Radius=0.6; Pierce=0; Speed=2600");

	/** The tags a ranged attack's shot carries, as the projectile range tests give one. */
	constexpr const TCHAR* ShotTags = TEXT("Type.Ranged, Type.Projectile");

	/** The 6 the Basic row of `SkillSlots.csv` states at level 100. */
	constexpr float ManaOnHitAtLevel100 = 6.0f;

	/**
	 * An actor a sphere overlap can find, holding the attribute sets a skill reads and pays into. The shape of
	 * `FKitFighter` in `CataclysmStalkerKitMechanismTests.cpp`, under its own name so the two files can share a unity
	 * build. It belongs to no team, which reads as hostile to everyone, so one fighter's shot strikes another.
	 */
	struct FRangedManaFighter
	{
		FRangedManaFighter(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<AActor>(Where, FRotator::ZeroRotator);
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

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FRangedManaFighter()
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

		float Get(const FGameplayAttribute& Attribute) const { return AbilitySystem->GetNumericAttribute(Attribute); }
		float Health() const { return Get(UCataclysmVitalAttributeSet::GetHealthAttribute()); }
		float Mana() const { return Get(UCataclysmVitalAttributeSet::GetManaAttribute()); }

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A projectile skill granted in `Slot` at level 100, its cell parsed as the game parses a row's. */
	UCataclysmProjectileSkill* GrantShot(const FRangedManaFighter& Caster, ECataclysmAbilitySlot Slot,
										 const FString& ParamText)
	{
		const FGameplayAbilitySpecHandle Handle =
			Caster.AbilitySystem->GiveAbilityInSlot(UCataclysmProjectileSkill::StaticClass(), Slot, /*Level=*/100,
													Caster.Actor);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? Caster.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmProjectileSkill* Instance = Spec ? Cast<UCataclysmProjectileSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = TEXT("Basic Attack");
			Instance->Params = UCataclysmSkillShapes::ParseParams(ParamText);
			Instance->SkillTags = UCataclysmSkillShapes::TagsFromCell(ShotTags);
		}
		return Instance;
	}

	/** Use the skill once. Whether it ran. */
	bool UseShot(const FRangedManaFighter& Caster, UCataclysmProjectileSkill* Skill)
	{
		return Skill && Caster.AbilitySystem->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(),
																 /*bAllowRemoteActivation=*/false);
	}

	/** Step every projectile of the skill's last use, one step each in the order fired, until all have finished. */
	void FlyEveryShot(const UCataclysmProjectileSkill* Skill)
	{
		for (int32 Steps = 0; Skill && Steps < 600; ++Steps)
		{
			bool bAnyFlew = false;
			for (ACataclysmProjectile* Shot : Skill->FiredThisUse)
			{
				if (Shot && !Shot->bFinished)
				{
					Shot->Step(1.0f / 60.0f);
					bAnyFlew = true;
				}
			}
			if (!bAnyFlew)
			{
				return;
			}
		}
	}
}

#define CATACLYSM_RANGED_MANA_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * THE WAND'S BASIC ATTACK RESTORES ITS MANA WHEN ITS SHOT LANDS, AND NOT BEFORE. A caster at no mana uses it at a target
 * four metres out: the shot is in the air with nothing paid, and when it has landed, hurting the target, the caster
 * holds the 6 a melee swing pays. THE CONTROL is the same shot from the Heavy slot, whose row states no mana on hit: it
 * lands, hurting its target, and the caster's mana is what the use left it.
 */
CATACLYSM_RANGED_MANA_TEST(FCataclysmRangedManaLandsTest,
	"Cataclysm.RangedManaOnHit.AWandsBasicAttackRestoresItsManaWhenItLandsAndNotBefore")
{
	using namespace CataclysmRangedManaTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE CONTROL FIRST, in its own lane.
	{
		FRangedManaFighter Caster(World, FVector(0.0f, 30.0f * M, 0.0f));
		FRangedManaFighter Target(World, FVector(4.0f * M, 30.0f * M, 0.0f));
		Caster.Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		UCataclysmProjectileSkill* Heavy = GrantShot(Caster, ECataclysmAbilitySlot::Heavy, WandShot);
		const float Before = Target.Health();
		if (!TestNotNull(TEXT("set-up: the Heavy slot's shot"), Heavy)
			|| !TestTrue(TEXT("set-up: it is used"), UseShot(Caster, Heavy)))
		{
			return false;
		}
		const float AfterTheUse = Caster.Mana();
		FlyEveryShot(Heavy);
		if (!TestTrue(TEXT("CONTROL: the Heavy shot landed, so its target is hurt"), Target.Health() < Before))
		{
			return false;
		}
		TestEqual(TEXT("CONTROL: and a slot stating no mana on hit restores none"), Caster.Mana(), AfterTheUse, 0.01f);
	}

	FRangedManaFighter Caster(World, FVector::ZeroVector);
	FRangedManaFighter Target(World, FVector(4.0f * M, 0.0f, 0.0f));
	UCataclysmProjectileSkill* Basic = GrantShot(Caster, ECataclysmAbilitySlot::BasicAttack, WandShot);
	const float Before = Target.Health();
	if (!TestNotNull(TEXT("set-up: the basic attack's shot"), Basic)
		|| !TestTrue(TEXT("set-up: it is used"), UseShot(Caster, Basic))
		|| !TestNotNull(TEXT("set-up: at a speed, a shot is in the air"), Basic->InFlight.Get()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("RANGED MANA: the slot's figure %.2f; in the air %.2f mana"), Basic->GetManaOnHit(),
							Caster.Mana()));
	TestEqual(TEXT("nothing is paid while the shot is in the air"), Caster.Mana(), 0.0f, 0.01f);
	FlyEveryShot(Basic);
	if (!TestTrue(TEXT("set-up: the shot landed, so its target is hurt"), Target.Health() < Before))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("RANGED MANA: landed, %.2f mana"), Caster.Mana()));
	TestEqual(TEXT("a landed shot restores the 6 a swing does"), Caster.Mana(), ManaOnHitAtLevel100, 0.05f);
	return true;
}

/**
 * AN EVADED SHOT RESTORES NOTHING: "each time it lands", and evasion says the shot never connected (the rule
 * `HitTargets` keeps, issue #1156). A target holding 100 evasion evades the Wand's shot, is not hurt, and its caster's
 * mana stays at nought. THE CONTROL is the same shot at a target with none: it lands and pays the 6.
 */
CATACLYSM_RANGED_MANA_TEST(FCataclysmRangedManaEvadedTest,
	"Cataclysm.RangedManaOnHit.AnEvadedShotRestoresNoMana")
{
	using namespace CataclysmRangedManaTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	{
		FRangedManaFighter Caster(World, FVector(0.0f, 30.0f * M, 0.0f));
		FRangedManaFighter Target(World, FVector(4.0f * M, 30.0f * M, 0.0f));
		UCataclysmProjectileSkill* Basic = GrantShot(Caster, ECataclysmAbilitySlot::BasicAttack, WandShot);
		const float Before = Target.Health();
		if (!TestNotNull(TEXT("set-up: the control's shot"), Basic)
			|| !TestTrue(TEXT("set-up: it is used"), UseShot(Caster, Basic)))
		{
			return false;
		}
		FlyEveryShot(Basic);
		if (!TestTrue(TEXT("CONTROL: the shot at a target with no evasion landed"), Target.Health() < Before))
		{
			return false;
		}
		TestEqual(TEXT("CONTROL: and restores the 6"), Caster.Mana(), ManaOnHitAtLevel100, 0.05f);
	}

	FRangedManaFighter Caster(World, FVector::ZeroVector);
	FRangedManaFighter Target(World, FVector(4.0f * M, 0.0f, 0.0f));
	Target.Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 100.0f);
	UCataclysmProjectileSkill* Basic = GrantShot(Caster, ECataclysmAbilitySlot::BasicAttack, WandShot);
	const float Before = Target.Health();
	if (!TestNotNull(TEXT("set-up: the basic attack's shot"), Basic)
		|| !TestTrue(TEXT("set-up: it is used"), UseShot(Caster, Basic)))
	{
		return false;
	}
	FlyEveryShot(Basic);
	if (!TestEqual(TEXT("set-up: the target evaded, so it is not hurt"), Target.Health(), Before, 0.01f))
	{
		return false;
	}
	TestEqual(TEXT("an evaded shot restores nothing"), Caster.Mana(), 0.0f, 0.01f);
	return true;
}

/**
 * A SHOT THAT PIERCES TWO ENEMIES RESTORES ITS MANA ONCE: the 6 is per use, which is the design's own arithmetic, 6 x
 * 1.3 attacks a second = 7.8, and `HitTargets` pays a swing that strikes five once. The Wand's row with `Pierce=99`, at
 * enemies three and six metres out: both are hurt and the caster holds 6, not 12.
 */
CATACLYSM_RANGED_MANA_TEST(FCataclysmRangedManaPierceTest,
	"Cataclysm.RangedManaOnHit.AShotThroughTwoEnemiesRestoresItsManaOnce")
{
	using namespace CataclysmRangedManaTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRangedManaFighter Caster(World, FVector::ZeroVector);
	FRangedManaFighter First(World, FVector(3.0f * M, 0.0f, 0.0f));
	FRangedManaFighter Second(World, FVector(6.0f * M, 0.0f, 0.0f));
	UCataclysmProjectileSkill* Basic = GrantShot(Caster, ECataclysmAbilitySlot::BasicAttack,
												 TEXT("Range=8.4; Radius=0.6; Pierce=99; Speed=2600"));
	const float FirstBefore = First.Health();
	const float SecondBefore = Second.Health();
	if (!TestNotNull(TEXT("set-up: the piercing shot"), Basic)
		|| !TestTrue(TEXT("set-up: it is used"), UseShot(Caster, Basic)))
	{
		return false;
	}
	FlyEveryShot(Basic);
	if (!TestTrue(TEXT("set-up: the shot passed through both, hurting both"),
				  First.Health() < FirstBefore && Second.Health() < SecondBefore))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("RANGED MANA PIERCE: %.2f mana"), Caster.Mana()));
	TestEqual(TEXT("two enemies landed on by one shot restore the 6 once"), Caster.Mana(), ManaOnHitAtLevel100, 0.05f);
	return true;
}

/**
 * A USE OF THREE PROJECTILES RESTORES ITS MANA ONCE. A row of the user's adds projectiles to a use (ruled 2026-10-12):
 * the caster carries 2 on `projectile_count`, so the Wand's shot is fired three times, ten degrees apart, at three
 * enemies six metres out on those three lines. All three are hurt and the caster holds 6, not 18: the first landing of
 * the USE pays, by the share record the projectiles of one use keep (`ACataclysmProjectile::LandedOnInItsUse`). A
 * narrower shot than the Wand's, 20 cm, so each projectile reaches only the enemy on its own line.
 */
CATACLYSM_RANGED_MANA_TEST(FCataclysmRangedManaThreeProjectilesTest,
	"Cataclysm.RangedManaOnHit.AUseOfThreeProjectilesRestoresItsManaOnce")
{
	using namespace CataclysmRangedManaTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FRangedManaFighter Caster(World, FVector::ZeroVector);
	FCataclysmStatModifier Row;
	Row.Bucket = ECataclysmStatBucket::Flat;
	Row.Source = ECataclysmModifierSource::Enchantment;
	Row.Value = 2.0f;
	Row.RequiredTags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.Projectile"));
	TMap<FName, FCataclysmStatInputs> Inputs;
	FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(UCataclysmProjectileSkill::ProjectileCountStat));
	Line.Base = 0.0f;
	Line.Modifiers = {Row};
	Caster.AbilitySystem->SetStatInputs(MoveTemp(Inputs));

	const float Degrees = UCataclysmProjectileSkill::FanDegreesBetweenProjectiles;
	TArray<TUniquePtr<FRangedManaFighter>> Enemies;
	for (const float Angle : {-Degrees, 0.0f, Degrees})
	{
		const float Radians = FMath::DegreesToRadians(Angle);
		Enemies.Add(MakeUnique<FRangedManaFighter>(
			World, FVector(6.0f * M * FMath::Cos(Radians), 6.0f * M * FMath::Sin(Radians), 0.0f)));
	}
	UCataclysmProjectileSkill* Basic = GrantShot(Caster, ECataclysmAbilitySlot::BasicAttack,
												 TEXT("Range=8.4; Radius=0.2; Pierce=0; Speed=2600"));
	TArray<float> Before;
	for (const TUniquePtr<FRangedManaFighter>& Enemy : Enemies)
	{
		Before.Add(Enemy->Health());
	}
	if (!TestNotNull(TEXT("set-up: the shot"), Basic) || !TestTrue(TEXT("set-up: it is used"), UseShot(Caster, Basic))
		|| !TestEqual(TEXT("set-up: one use fires three projectiles"), Basic->FiredThisUse.Num(), 3))
	{
		return false;
	}
	FlyEveryShot(Basic);
	int32 Hurt = 0;
	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		Hurt += Enemies[Index]->Health() < Before[Index] ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("RANGED MANA THREE: %d of 3 hurt; %.2f mana"), Hurt, Caster.Mana()));
	if (!TestEqual(TEXT("set-up: each projectile landed on the enemy on its line"), Hurt, 3))
	{
		return false;
	}
	TestEqual(TEXT("three projectiles of one use restore the 6 once"), Caster.Mana(), ManaOnHitAtLevel100, 0.05f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
