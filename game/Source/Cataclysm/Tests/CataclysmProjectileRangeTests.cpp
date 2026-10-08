// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
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

	// A WORLD THAT HAS BEGUN PLAY, so a summoned minion has its attribute sets
	// before its health is written. The first run of this test used a bare
	// world and failed on an ensure in ACataclysmMinion::Spawn.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
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

/**
 * Ricochets and pierce from a worn row. Ruled 2026-10-07 under the owner's delegation: `projectile_bounces` is a
 * whole number added to the bounces a projectile skill states, and `projectile_pierce_all` is a flag by which a
 * projectile skill pierces all. Each is asked with the skill's tags.
 *
 * EVERY CASE HAS ITS OWN LANE, 60 m from the next, and a user at the lane's start facing along it. A skill used
 * with no player controller is aimed its whole range along its user's facing. Each case reports what each enemy
 * LOST, and every amount is compared with what an enemy of a user with no row lost in the same test.
 */
namespace CataclysmRicochetPierceTest
{
	constexpr float Metre = 100.0f;
	constexpr float Pool = 100000.0f;
	constexpr float LaneApart = 60.0f * Metre;

	/** A character an overlap finds, standing where it was spawned, with health, mana and 100 attack damage. */
	struct FBody
	{
		FBody(UWorld* World, const FVector& Where)
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

			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Pool);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Pool);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FBody()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		/** Replace the stat lines with one flat line of this stat, as a row's, scoped by a tag cell. */
		void CarryFlatLine(const TCHAR* Stat, float Value, const TCHAR* RequiredTags) const
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::Enchantment;
			Row.Value = Value;
			Row.RequiredTags = UCataclysmSkillShapes::TagsFromCell(RequiredTags);
			TMap<FName, FCataclysmStatInputs> Inputs;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Row};
			AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}

		/** A projectile skill granted in a slot with these params and tags, dealing 100% of weapon damage. */
		UCataclysmProjectileSkill* Skill(ECataclysmAbilitySlot Slot, const TCHAR* ParamText,
										 const TCHAR* TagCell) const
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				UCataclysmProjectileSkill::StaticClass(), Slot, /*Level=*/1, Actor);
			FGameplayAbilitySpec* Spec =
				Handle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmProjectileSkill* Granted =
				Spec ? Cast<UCataclysmProjectileSkill>(Spec->GetPrimaryInstance()) : nullptr;
			if (Granted)
			{
				Granted->SkillName = TEXT("Test Shot");
				Granted->Params = UCataclysmSkillShapes::ParseParams(ParamText);
				Granted->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
				Granted->DamagePercentOverride = 100.0f;
			}
			return Granted;
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Use a skill, and step whatever it put in the air until that says it has finished. False when the skill did
	 * not run. A beam puts nothing in the air and has landed by the time the use returns.
	 */
	bool UseAndFly(const FBody& User, UCataclysmProjectileSkill* Skill)
	{
		if (!Skill || !User.AbilitySystem->TryActivateAbility(
				Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false))
		{
			return false;
		}
		ACataclysmProjectile* Shot = Skill->InFlight.Get();
		for (int32 Steps = 0; Shot && Steps < 600 && !Shot->bFinished; ++Steps)
		{
			Shot->Step(1.0f / 60.0f);
		}
		return true;
	}

	/** What each of these has lost from a full pool, in order. */
	TArray<float> LostBy(const TArray<TUniquePtr<FBody>>& Enemies)
	{
		TArray<float> Lost;
		for (const TUniquePtr<FBody>& Enemy : Enemies)
		{
			Lost.Add(Pool - Enemy->Health());
		}
		return Lost;
	}

	constexpr const TCHAR* AttackTags = TEXT("Type.Ranged, Type.Projectile");
	constexpr const TCHAR* SpellTags = TEXT("Type.Spell, Type.Projectile, Type.Ranged");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmRicochetRowAddsBouncesTest,
	"Cataclysm.ProjectileRange.ARowsRicochetsAreAddedToTheBouncesAProjectileSkillStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmRicochetRowAddsBouncesTest::RunTest(const FString&)
{
	using namespace CataclysmRicochetPierceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	// ONE LANE: the user at its start, and enemies 3 m apart along it, the first 3 m from the user. The shot is
	// aimed 10 m along the lane and a glance looks 10 m, the skill's range, so every enemy is within a glance of
	// the one before it and the only thing that stops the shot is running out of bounces.
	int32 LanesUsed = 0;
	const auto LostInALane = [&](const TCHAR* ParamText, float Ricochets, const TCHAR* RowTags, int32 Enemies,
								 bool& bOutRan)
	{
		const float Y = LaneApart * LanesUsed++;
		FBody User(World, FVector(0.0f, Y, 0.0f));
		TArray<TUniquePtr<FBody>> Row;
		for (int32 Index = 0; Index < Enemies; ++Index)
		{
			Row.Add(MakeUnique<FBody>(World, FVector((3.0f + 3.0f * Index) * Metre, Y, 0.0f)));
		}
		if (Ricochets > 0.0f)
		{
			User.CarryFlatLine(UCataclysmProjectileSkill::ProjectileBouncesStat, Ricochets, RowTags);
		}
		bOutRan = UseAndFly(User, User.Skill(ECataclysmAbilitySlot::Special, ParamText, AttackTags));
		return LostBy(Row);
	};

	const TCHAR* Glancing = TEXT("Range=10; Radius=1; Speed=2000; Bounces=1");
	const TCHAR* Plain = TEXT("Range=10; Radius=1; Speed=2000");
	bool bRan = false;

	// CONTROL: a skill stating one bounce, no row. It strikes two enemies.
	const TArray<float> Stated = LostInALane(Glancing, 0.0f, TEXT(""), 5, bRan);
	if (!TestTrue(TEXT("set-up: the control's shot ran"), bRan)
		|| !TestTrue(TEXT("set-up: and its first enemy lost health"), Stated[0] > 0.0f))
	{
		return false;
	}
	const float OneHit = Stated[0];
	TestEqual(TEXT("control: one stated bounce strikes the second enemy for what the first took"),
			  Stated[1], OneHit, 0.01f);
	TestEqual(TEXT("control: and not the third"), Stated[2], 0.0f, 0.01f);

	// THE SAME SKILL, its user carrying 2 ricochets scoped to projectile skills: three bounces, four enemies.
	const TArray<float> Added = LostInALane(Glancing, 2.0f, TEXT("Type.Projectile"), 5, bRan);
	TestTrue(TEXT("the carrying user's shot ran"), bRan);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("2 ricochets on 1 stated bounce: enemy %d is struck once, for what a "
										"control enemy took"), Index + 1),
				  Added[Index], OneHit, 0.01f);
	}
	TestEqual(TEXT("and the fifth is not struck, because the bounces ran out"), Added[4], 0.0f, 0.01f);

	// A ROW SCOPED TO SPELLS DOES NOT REACH AN ATTACK: the lookup is asked with the skill's tags.
	const TArray<float> Scoped = LostInALane(Glancing, 2.0f, TEXT("Type.Spell"), 5, bRan);
	TestTrue(TEXT("the shot of a user whose row names spells ran"), bRan);
	TestEqual(TEXT("a row scoped to spells leaves an attack's second enemy as the control's"),
			  Scoped[1], OneHit, 0.01f);
	TestEqual(TEXT("and its third unstruck, as the control's"), Scoped[2], 0.0f, 0.01f);

	// A SKILL THAT STATES NO BOUNCE. Control: it stops at the first enemy and goes off in its 1 m radius, which
	// the second enemy, 3 m on, is outside.
	const TArray<float> PlainControl = LostInALane(Plain, 0.0f, TEXT(""), 5, bRan);
	TestTrue(TEXT("the plain control's shot ran"), bRan);
	TestEqual(TEXT("control: a skill stating no bounce strikes its first enemy"), PlainControl[0], OneHit, 0.01f);
	TestEqual(TEXT("control: and not its second"), PlainControl[1], 0.0f, 0.01f);

	const TArray<float> PlainAdded = LostInALane(Plain, 2.0f, TEXT("Type.Projectile"), 5, bRan);
	TestTrue(TEXT("the carrying user's plain shot ran"), bRan);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("2 ricochets on no stated bounce: enemy %d is struck once, for what a "
										"control enemy took"), Index + 1),
				  PlainAdded[Index], OneHit, 0.01f);
	}
	TestEqual(TEXT("and the fourth is not struck"), PlainAdded[3], 0.0f, 0.01f);

	// NO SECOND ENEMY IN REACH: the one enemy is struck once, as a control's is, and nothing more happens.
	const TArray<float> AloneControl = LostInALane(Plain, 0.0f, TEXT(""), 1, bRan);
	TestTrue(TEXT("the lone control's shot ran"), bRan);
	const TArray<float> Alone = LostInALane(Plain, 2.0f, TEXT("Type.Projectile"), 1, bRan);
	TestTrue(TEXT("the lone carrying user's shot ran"), bRan);
	TestEqual(TEXT("control: a lone enemy is struck once"), AloneControl[0], OneHit, 0.01f);
	TestEqual(TEXT("with ricochets and nothing to glance to, the lone enemy is struck once and not twice"),
			  Alone[0], AloneControl[0], 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPierceAllRowOnAFlyingSpellTest,
	"Cataclysm.ProjectileRange.ARowMakesAFlyingSpellPierceEveryEnemyOnItsLineAndLeavesAnAttackAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPierceAllRowOnAFlyingSpellTest::RunTest(const FString&)
{
	using namespace CataclysmRicochetPierceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	// A BOLT STATING A 2 M RADIUS, aimed 12 m along its lane. THREE ENEMIES ON THE LINE, 3 m, 6 m and 9 m from the
	// user, and ONE BESIDE THE FIRST, 2.1 m to its side. A bolt that does not pierce stops at the first enemy it
	// touches and goes off there: the blast reaches the one beside it, because a blast counts a body's own 34 cm,
	// and neither of the two further on. A bolt that pierces is a line with a half-width of the same 2 m, measured
	// to a body's centre, so the one beside the first is 10 cm outside it.
	const TCHAR* Bolt = TEXT("Range=12; Radius=2; Speed=2000");
	struct FLane
	{
		FLane(UWorld* InWorld, float LaneY)
			: User(InWorld, FVector(0.0f, LaneY, 0.0f))
		{
			Enemies.Add(MakeUnique<FBody>(InWorld, FVector(3.0f * Metre, LaneY, 0.0f)));
			Enemies.Add(MakeUnique<FBody>(InWorld, FVector(6.0f * Metre, LaneY, 0.0f)));
			Enemies.Add(MakeUnique<FBody>(InWorld, FVector(9.0f * Metre, LaneY, 0.0f)));
			Enemies.Add(MakeUnique<FBody>(InWorld, FVector(3.0f * Metre, LaneY + 2.1f * Metre, 0.0f)));
		}
		FBody User;
		TArray<TUniquePtr<FBody>> Enemies;
	};

	FLane Control(World, 0.0f);
	FLane Wearing(World, LaneApart);
	Wearing.User.CarryFlatLine(UCataclysmProjectileSkill::ProjectilePierceAllStat, 1.0f, TEXT("Type.Spell"));

	if (!TestTrue(TEXT("set-up: the control's spell ran"),
			UseAndFly(Control.User, Control.User.Skill(ECataclysmAbilitySlot::Special, Bolt, SpellTags))))
	{
		return false;
	}
	const TArray<float> Blast = LostBy(Control.Enemies);
	if (!TestTrue(TEXT("set-up: the control's first enemy lost health"), Blast[0] > 0.0f))
	{
		return false;
	}
	const float OneHit = Blast[0];
	TestEqual(TEXT("control: the spell does not reach the second enemy on the line"), Blast[1], 0.0f, 0.01f);
	TestEqual(TEXT("control: nor the third"), Blast[2], 0.0f, 0.01f);
	TestEqual(TEXT("control: its blast reaches the enemy beside the first"), Blast[3], OneHit, 0.01f);

	if (!TestTrue(TEXT("the wearer's spell ran"),
			UseAndFly(Wearing.User, Wearing.User.Skill(ECataclysmAbilitySlot::Special, Bolt, SpellTags))))
	{
		return false;
	}
	const TArray<float> Line = LostBy(Wearing.Enemies);
	TestEqual(TEXT("the wearer's spell strikes the first enemy once, for what a control enemy took"),
			  Line[0], OneHit, 0.01f);
	TestEqual(TEXT("and the second on the line, once"), Line[1], OneHit, 0.01f);
	TestEqual(TEXT("and the third on the line, once"), Line[2], OneHit, 0.01f);
	TestEqual(TEXT("and not the enemy beside the first: no blast went off there"), Line[3], 0.0f, 0.01f);

	// THE SAME WEARER'S ATTACK, the same bolt in another slot without the spell tag, down the same lane. It is
	// what the control's spell was: the first enemy and the one beside it lose one more hit, the other two none.
	if (!TestTrue(TEXT("the wearer's attack ran"),
			UseAndFly(Wearing.User, Wearing.User.Skill(ECataclysmAbilitySlot::Heavy, Bolt, AttackTags))))
	{
		return false;
	}
	const TArray<float> After = LostBy(Wearing.Enemies);
	TestEqual(TEXT("the wearer's attack strikes the first enemy"), After[0] - Line[0], OneHit, 0.01f);
	TestEqual(TEXT("it does not pierce to the second"), After[1] - Line[1], 0.0f, 0.01f);
	TestEqual(TEXT("nor the third"), After[2] - Line[2], 0.0f, 0.01f);
	TestEqual(TEXT("and it goes off where it stopped, reaching the enemy beside the first"),
			  After[3] - Line[3], OneHit, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmPierceAllRowOnASpellBeamTest,
	"Cataclysm.ProjectileRange.ARowMakesASpellBeamStrikeAlongItsLineAndNotWhereItWasAimed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmPierceAllRowOnASpellBeamTest::RunTest(const FString&)
{
	using namespace CataclysmRicochetPierceTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	// A BEAM, WHICH STATES NO SPEED, aimed 10 m along its lane with a 2 m radius. THREE ENEMIES ON THE LINE, 3 m,
	// 6.5 m and 10 m from the user, the last where the beam is aimed, and ONE BESIDE THAT POINT, 2.1 m to its side.
	// Without the row the beam lands in its radius at the aimed point and reaches the last enemy and the one
	// beside it. With it the beam is a line from the user to that point, 2 m to each side measured to a body's
	// centre, so the one beside the aimed point is 10 cm outside it.
	const TCHAR* Beam = TEXT("Range=10; Radius=2; Speed=0");
	const auto LostInALane = [&](float Y, bool bCarries, bool& bOutRan)
	{
		FBody User(World, FVector(0.0f, Y, 0.0f));
		TArray<TUniquePtr<FBody>> Enemies;
		Enemies.Add(MakeUnique<FBody>(World, FVector(3.0f * Metre, Y, 0.0f)));
		Enemies.Add(MakeUnique<FBody>(World, FVector(6.5f * Metre, Y, 0.0f)));
		Enemies.Add(MakeUnique<FBody>(World, FVector(10.0f * Metre, Y, 0.0f)));
		Enemies.Add(MakeUnique<FBody>(World, FVector(10.0f * Metre, Y + 2.1f * Metre, 0.0f)));
		if (bCarries)
		{
			User.CarryFlatLine(UCataclysmProjectileSkill::ProjectilePierceAllStat, 1.0f, TEXT("Type.Spell"));
		}
		bOutRan = UseAndFly(User, User.Skill(ECataclysmAbilitySlot::Special, Beam, SpellTags));
		return LostBy(Enemies);
	};

	bool bRan = false;
	const TArray<float> Landed = LostInALane(0.0f, /*bCarries=*/false, bRan);
	if (!TestTrue(TEXT("set-up: the control's beam ran"), bRan)
		|| !TestTrue(TEXT("set-up: and the enemy where it was aimed lost health"), Landed[2] > 0.0f))
	{
		return false;
	}
	const float OneHit = Landed[2];
	TestEqual(TEXT("control: the beam does not strike the first enemy on its line"), Landed[0], 0.0f, 0.01f);
	TestEqual(TEXT("control: nor the second"), Landed[1], 0.0f, 0.01f);
	TestEqual(TEXT("control: it reaches the enemy beside where it was aimed"), Landed[3], OneHit, 0.01f);

	const TArray<float> Line = LostInALane(LaneApart, /*bCarries=*/true, bRan);
	TestTrue(TEXT("the wearer's beam ran"), bRan);
	TestEqual(TEXT("the wearer's beam strikes the first enemy on its line once, for what a control enemy took"),
			  Line[0], OneHit, 0.01f);
	TestEqual(TEXT("and the second, once"), Line[1], OneHit, 0.01f);
	TestEqual(TEXT("and the one where it was aimed, once"), Line[2], OneHit, 0.01f);
	TestEqual(TEXT("and not the enemy beside where it was aimed: it did not land in a radius"),
			  Line[3], 0.0f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
