// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmLichCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Dungeon/CataclysmFloorPlan.h"
#include "Dungeon/CataclysmFloorPopulation.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the Lich, Death's caster, approved by the owner on 2026-11-26 ("I'm happy with those enemy designs").
 *
 * WHAT THESE GUARD:
 * 1. ITS THREE ABILITIES IN PRIORITY ORDER, and the raise chosen only when a death lies near. With nothing to raise its
 *    range is nought, so the brain never casts it at nothing; `CataclysmDeathModifierTests.cpp` holds the half where a
 *    death is near, because only the dungeon game mode keeps the record.
 * 2. NO FREE MELEE HIT AT ITS BOLT'S RANGE, the Succubus's and the Corrupted Sentinel's trap.
 * 3. GRAVE BURST CATCHING EVERYTHING IN ITS RING AND NOTHING OUTSIDE IT.
 * 4. GRAVE BOLT LEAVING AS A QUICK CAST'S PROJECTILE.
 * 5. ITS ART, with a skipped half where the owner's Lich pack is not installed (every worktree).
 * 6. THE ESCORT: a Death floor's is a Lich, a Demonic floor's the Succubus.
 *
 * The raise itself, its record and its refusals are tested beside the Death modifiers' floor rig, in
 * `CataclysmDeathModifierTests.cpp`, under this same group.
 */
namespace CataclysmLichTest
{
	static ACataclysmLichCharacter* SpawnLich(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACataclysmLichCharacter>(ACataclysmLichCharacter::StaticClass(), Where,
														  FRotator::ZeroRotator, Params);
	}

	/** Something with a great deal of health on the players' side. Spawned far off and moved, so no capsule pushes. */
	static ACataclysmEnemyCharacter* SpawnLichTarget(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmEnemyCharacter* Actor = World->SpawnActor<ACataclysmEnemyCharacter>(
			ACataclysmEnemyCharacter::StaticClass(), FVector(30000.0f, 30000.0f, 0.0f), FRotator::ZeroRotator, Params);
		if (!Actor)
		{
			return nullptr;
		}
		Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Actor->SetHealth(100000.0f);
		Actor->SetActorLocation(Where);
		return Actor;
	}

	static float LichTestHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}
}

#define CATACLYSM_LICH_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_LICH_TEST(FCataclysmLichAbilitiesTest,
	"Cataclysm.Lich.ItsAbilitiesAreTheRaiseTheBurstAndTheBoltInThatOrder")
{
	using namespace CataclysmLichTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmLichCharacter* Lich = SpawnLich(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("a Lich"), Lich))
	{
		return false;
	}
	const TArray<FCataclysmEnemyAbility> Abilities = Lich->EnemyAbilities();
	if (!TestEqual(TEXT("three abilities, the cap a basic creature has"), Abilities.Num(), 3))
	{
		return false;
	}

	const FCataclysmEnemyAbility& Raise = Abilities[ACataclysmLichCharacter::RaiseTheFallenAbility];
	TestEqual(TEXT("first, Raise the Fallen"), Raise.Name, FString(TEXT("Raise the Fallen")));
	TestTrue(TEXT("a Summon, which draws no marker"), Raise.Shape == ECataclysmSkillShape::Summon);
	TestEqual(TEXT("on a 10 s cooldown"), Raise.CooldownSeconds, 10.0f);
	// THE CONTROL HALF OF J3: no dungeon keeps a record here, so no death is near and the raise cannot be chosen.
	TestEqual(TEXT("with nothing near to raise, its range is nought and the brain never picks it"), Raise.MaxRangeCm,
			  0.0f);

	const FCataclysmEnemyAbility& Burst = Abilities[ACataclysmLichCharacter::GraveBurstAbility];
	TestEqual(TEXT("second, Grave Burst"), Burst.Name, FString(TEXT("Grave Burst")));
	TestTrue(TEXT("a Strike"), Burst.Shape == ECataclysmSkillShape::Strike);
	TestEqual(TEXT("chosen within its 3 m ring"), Burst.MaxRangeCm, 300.0f);
	TestEqual(TEXT("and marking that ring"), Burst.MarkerRadiusCm, 300.0f);
	TestFalse(TEXT("with a marker drawn"), Burst.bDrawsNoMarker);
	TestEqual(TEXT("for 0.4 + 3.0 / 3.5 seconds"), Burst.WindUpSeconds, 0.4f + 3.0f / 3.5f, 0.001f);
	TestEqual(TEXT("on an 8 s cooldown"), Burst.CooldownSeconds, 8.0f);

	const FCataclysmEnemyAbility& Bolt = Abilities[ACataclysmLichCharacter::GraveBoltAbility];
	TestEqual(TEXT("last, Grave Bolt"), Bolt.Name, FString(TEXT("Grave Bolt")));
	TestTrue(TEXT("a Projectile"), Bolt.Shape == ECataclysmSkillShape::Projectile);
	TestTrue(TEXT("its basic attack"), Bolt.bIsBasicAttack);
	TestTrue(TEXT("a quick cast, with no marker"), Bolt.bDrawsNoMarker);
	TestEqual(TEXT("cast in the quick cast's half second"), Bolt.WindUpSeconds, FCataclysmEnemyAbility::QuickCastSeconds);
	TestEqual(TEXT("reaching 8 m"), Bolt.MaxRangeCm, 800.0f);
	TestEqual(TEXT("with no cooldown"), Bolt.CooldownSeconds, 0.0f);

	// ITS FIGURES, from the approved design and the archetype row.
	TestEqual(TEXT("its archetype row is Lich"), Lich->ArchetypeRow, FName(TEXT("Lich")));
	TestEqual(TEXT("it holds at its bolt's 8 m"), Lich->AttackReachCm(), 800.0f);
	TestEqual(TEXT("a bolt every second"), Lich->DesignedSecondsBetweenAttacks(), 1.0f);
	TestEqual(TEXT("25% resistance"), Lich->ResistancePercent, 25.0f);
	TestEqual(TEXT("a shield of 30% of its health"), Lich->EnergyShieldFraction, 0.30f);
	TestEqual(TEXT("no evasion"), Lich->EvasionPercent, 0.0f);
	return true;
}

CATACLYSM_LICH_TEST(FCataclysmLichNoMeleeTest,
	"Cataclysm.Lich.ItDealsNoMeleeHitAtItsBoltsRange")
{
	using namespace CataclysmLichTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmLichCharacter* Lich = SpawnLich(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Target = SpawnLichTarget(World, FVector(700.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("a Lich"), Lich) || !TestNotNull(TEXT("a target"), Target))
	{
		return false;
	}
	Lich->SetAttackDamage(100.0f);
	const float Before = LichTestHealthOf(Target);
	Lich->AttackTarget(Target);
	TestEqual(TEXT("its basic attack is its bolt, so the base class's melee hit at 7 m takes nothing"),
			  LichTestHealthOf(Target), Before);

	// THE CONTROL: the same target in its ring takes the burst, so this rig can see damage at all.
	Target->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	Lich->UseEnemyAbility(ACataclysmLichCharacter::GraveBurstAbility, Target, Target->GetActorLocation());
	TestTrue(TEXT("CONTROL: within 3 m the burst takes health"), LichTestHealthOf(Target) < Before);
	return true;
}

CATACLYSM_LICH_TEST(FCataclysmLichBurstTest,
	"Cataclysm.Lich.GraveBurstCatchesEverythingInItsRingAndNothingOutside")
{
	using namespace CataclysmLichTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmLichCharacter* Lich = SpawnLich(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Inside = SpawnLichTarget(World, FVector(250.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Behind = SpawnLichTarget(World, FVector(-250.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Outside = SpawnLichTarget(World, FVector(0.0f, 450.0f, 0.0f));
	if (!TestNotNull(TEXT("a Lich"), Lich) || !Inside || !Behind || !Outside)
	{
		return false;
	}
	Lich->SetAttackDamage(100.0f);
	const float InsideBefore = LichTestHealthOf(Inside);
	const float BehindBefore = LichTestHealthOf(Behind);
	const float OutsideBefore = LichTestHealthOf(Outside);

	Lich->UseEnemyAbility(ACataclysmLichCharacter::GraveBurstAbility, Inside, Inside->GetActorLocation());

	TestEqual(TEXT("two were caught"), Lich->LastBurstCaught, 2);
	TestTrue(TEXT("the one in front, 2.5 m off, took damage"), LichTestHealthOf(Inside) < InsideBefore);
	TestTrue(TEXT("and the one behind: the ring is all the way round"), LichTestHealthOf(Behind) < BehindBefore);
	TestEqual(TEXT("the one 4.5 m off took nothing"), LichTestHealthOf(Outside), OutsideBefore);
	return true;
}

CATACLYSM_LICH_TEST(FCataclysmLichBoltTest,
	"Cataclysm.Lich.GraveBoltLeavesAsAProjectile")
{
	using namespace CataclysmLichTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmLichCharacter* Lich = SpawnLich(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Target = SpawnLichTarget(World, FVector(600.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("a Lich"), Lich) || !TestNotNull(TEXT("a target"), Target))
	{
		return false;
	}
	TestNull(TEXT("CONTROL: nothing fired yet"), Lich->LastShotFired.Get());
	Lich->UseEnemyAbility(ACataclysmLichCharacter::GraveBoltAbility, Target, Target->GetActorLocation());
	TestNotNull(TEXT("Grave Bolt fired a projectile"), Lich->LastShotFired.Get());
	TestEqual(TEXT("and raised nothing"), Lich->LastRaised, 0);
	return true;
}

CATACLYSM_LICH_TEST(FCataclysmLichArtTest,
	"Cataclysm.Lich.ItWearsTheLichPackWhenItIsInstalled")
{
	using namespace CataclysmLichTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmLichCharacter* Lich = SpawnLich(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("a Lich"), Lich))
	{
		return false;
	}
	// THE HALF THAT RUNS EVERYWHERE: the four hit clips named in the pack's folder, two for four sides.
	TestTrue(TEXT("its hit clips are named in /Game/Lich/Animations"),
			 FString(ACataclysmLichCharacter::AnimationFolder) == TEXT("/Game/Lich/Animations"));

	if (!Lich->ResolveBody(/*bIncludeAnimation=*/true))
	{
		CataclysmTestSkip::ReportSkippedHalf(*this,
			TEXT("the owner's Lich pack is not installed, so the mesh, the five clips and the death clip were not "
				 "exercised. See game/docs/enemy-source-assets.md."));
		return true;
	}
	USkeletalMeshComponent* Mesh = Lich->GetMesh();
	TestTrue(TEXT("it wears SK_Lich"), Mesh && Mesh->GetSkeletalMeshAsset()
		&& Mesh->GetSkeletalMeshAsset()->GetName() == TEXT("SK_Lich"));
	TestNotNull(TEXT("its idle loaded"), Lich->IdleAnimation.Get());
	TestNotNull(TEXT("its walk loaded"), Lich->JogAnimation.Get());
	TestNotNull(TEXT("Grave Bolt's clip loaded"), Lich->GraveBoltAnimation.Get());
	TestNotNull(TEXT("Grave Burst's clip loaded"), Lich->GraveBurstAnimation.Get());
	TestNotNull(TEXT("Raise the Fallen's clip loaded"), Lich->RaiseAnimation.Get());
	if (Lich->GraveBoltAnimation)
	{
		TestTrue(TEXT("the bolt's clip fits a bolt every second"),
				 Lich->GraveBoltAnimation->GetPlayLength() <= 1.0f + UE_KINDA_SMALL_NUMBER);
	}
	if (Lich->RaiseAnimation)
	{
		TestTrue(TEXT("the raise's clip is long enough to reach its release"),
				 Lich->RaiseAnimation->GetPlayLength() > ACataclysmLichCharacter::RaiseReleaseSeconds);
	}
	return true;
}

CATACLYSM_LICH_TEST(FCataclysmLichEscortTest,
	"Cataclysm.Lich.ADeathFloorsEscortIsALichAndADemonicFloorsIsTheSuccubus")
{
	// THE OWNER'S RULING 2 OF 2026-11-26: "the Lich replaces the Succubus as the escort that joins one group in four"
	// in Death dungeons. Every other sender keeps the Succubus.
	TestTrue(TEXT("Death's escort is the Lich"),
			 FCataclysmFloorPopulator::EscortFor(ECataclysmType::Death) == ECataclysmDungeonCreature::Lich);
	TestTrue(TEXT("CONTROL: Demonic's escort is the Succubus"),
			 FCataclysmFloorPopulator::EscortFor(ECataclysmType::Demonic) == ECataclysmDungeonCreature::Succubus);
	TestTrue(TEXT("CONTROL: a dungeon nobody sent keeps the Succubus"),
			 FCataclysmFloorPopulator::EscortFor(ECataclysmType::None) == ECataclysmDungeonCreature::Succubus);

	// AND ON REAL FLOORS: the same plans populated for Death and for Demonic.
	FCataclysmFloorBrief ByDeath;
	ByDeath.Cataclysm = ECataclysmType::Death;
	FCataclysmFloorBrief ByDemonic;
	ByDemonic.Cataclysm = ECataclysmType::Demonic;
	int32 DeathLiches = 0;
	int32 DeathSuccubi = 0;
	int32 DemonicLiches = 0;
	int32 DemonicSuccubi = 0;
	for (int32 Seed = 1; Seed <= 12; ++Seed)
	{
		FCataclysmFloorRequest Request;
		Request.DungeonSeed = Seed;
		Request.FloorNumber = 2;
		Request.Layout = ECataclysmFloorLayout::Halls;
		const FCataclysmFloorPlan Plan = FCataclysmFloorGenerator::Generate(Request);
		if (!Plan.IsBuilt())
		{
			continue;
		}
		DeathLiches += FCataclysmFloorPopulator::Populate(Plan, 1.0f, ByDeath).HowMany(ECataclysmDungeonCreature::Lich);
		DeathSuccubi +=
			FCataclysmFloorPopulator::Populate(Plan, 1.0f, ByDeath).HowMany(ECataclysmDungeonCreature::Succubus);
		DemonicLiches +=
			FCataclysmFloorPopulator::Populate(Plan, 1.0f, ByDemonic).HowMany(ECataclysmDungeonCreature::Lich);
		DemonicSuccubi +=
			FCataclysmFloorPopulator::Populate(Plan, 1.0f, ByDemonic).HowMany(ECataclysmDungeonCreature::Succubus);
	}
	TestTrue(FString::Printf(TEXT("Death's floors hold Liches (%d)"), DeathLiches), DeathLiches > 0);
	TestEqual(TEXT("and no Succubus"), DeathSuccubi, 0);
	TestTrue(FString::Printf(TEXT("CONTROL: Demonic's floors hold Succubi (%d)"), DemonicSuccubi), DemonicSuccubi > 0);
	TestEqual(TEXT("CONTROL: and no Lich"), DemonicLiches, 0);
	return true;
}

#undef CATACLYSM_LICH_TEST

#endif // WITH_AUTOMATION_TESTS
