// Copyright Stephen Dubois. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Staff's Quarry mark jumps to the nearest living enemy when its quarry dies. Issue #1520.
 *
 * THE DESIGN: "If the marked enemy dies first, the mark jumps to the nearest living enemy" (the Debuffs sheet), and the
 * skill row says the same and states `OnDeath=SpreadDebuff; OnDeathRange=8`. The jump is
 * `UCataclysmCurseSpread::SpreadFromDying`, which copies the mark through `UCataclysmSkillEffects::CopyDebuffsTo`, and a
 * copy lasts the effect's OWN duration from the Status Effects sheet. `Debuff_Quarry` stated 0.0 there until issue
 * #1520, and a copy lasting no time is not made, so until then the mark never jumped. The cast mark itself always lasted
 * twelve seconds, because the skill states `EffectDuration=12`.
 *
 * THE SHIPPED ROW, READ FROM THE GENERATED WEAPON SKILL TABLE, and the mark's duration from the generated Status
 * Effects table. This test is about the designed figures reaching play, so it writes none of its own.
 *
 * THE CASTER IS A BARE ACTOR AND THE ENEMIES ARE REAL CHARACTERS, as in `CataclysmCommandTests.cpp`:
 * `UCataclysmCommand::QuarryOf` searches characters, so a marked enemy has to be one.
 */
namespace CataclysmQuarryJumpTest
{
	/** Metres, so the positions read the way the row does. */
	constexpr float M = 100.0f;

	/** The one who casts Quarry: an actor holding the skill and the pools its cost is paid from. */
	struct FQuarryJumpCaster
	{
		FQuarryJumpCaster(UWorld* World, const FVector& Where)
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
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FQuarryJumpCaster()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value)
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		/** The shipped row granted into its own designed slot, or null. */
		UCataclysmSkillTemplate* Grant(const FCataclysmWeaponSkill& Row)
		{
			const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbilityInSlot(
				UCataclysmWeaponSkills::TemplateFor(Row.Shape), UCataclysmWeaponSkills::DesignedSlotOf(Row),
				/*Level=*/100, Actor);
			FGameplayAbilitySpec* Spec = Handle.IsValid() ? AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
			UCataclysmSkillTemplate* Skill = Spec ? Cast<UCataclysmSkillTemplate>(Spec->GetPrimaryInstance()) : nullptr;
			if (Skill)
			{
				UCataclysmWeaponSkills::StampOnto(*Skill, Row);
			}
			return Skill;
		}

		bool UseSkill(UCataclysmSkillTemplate* Skill)
		{
			return Skill && AbilitySystem->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(),
															  /*bAllowRemoteActivation=*/false);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** An enemy: a real character on the monsters' side, so a search finds it and a mark can order onto it. */
	struct FQuarryJumpEnemy
	{
		FQuarryJumpEnemy(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Actor->SetHealth(1000.0f);
			Actor->SetAttackDamage(50.0f);
		}

		~FQuarryJumpEnemy()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		bool IsMarked() const
		{
			return UCataclysmSkillEffects::HasTag(Actor, UCataclysmCommand::QuarryTag());
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
	};

	/** The Demonic Staff's Quarry, as the generated table holds it, or null. */
	const FCataclysmWeaponSkill* ShippedQuarry(const TArray<FCataclysmWeaponSkill>& StaffSkills)
	{
		return StaffSkills.FindByPredicate(
			[](const FCataclysmWeaponSkill& Skill) { return Skill.Name == TEXT("Quarry"); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmQuarryMarkJumpsWhenItsQuarryDiesTest,
	"Cataclysm.Quarry.WhenTheMarkedEnemyDiesTheMarkJumpsToTheNearestLivingEnemyWithinEightMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuarryMarkJumpsWhenItsQuarryDiesTest::RunTest(const FString&)
{
	using namespace CataclysmQuarryJumpTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("the weapon skill table loads"), Table))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		return false;
	}
	const TArray<FCataclysmWeaponSkill> Staff = UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Staff"), TEXT("Demonic"));
	const FCataclysmWeaponSkill* Row = ShippedQuarry(Staff);
	if (!TestNotNull(TEXT("set-up: the Demonic Staff has Quarry"), Row))
	{
		return false;
	}

	// SET-UP: THE ROW STATES THE EIGHT METRES this test places its enemies either side of.
	if (!TestEqual(TEXT("set-up: Quarry's jump reaches 8 m"), Row->Params.OnDeathRangeCm, 8.0f * M))
	{
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE CONTROL FIRST: AN ENEMY TEN METRES FROM THE QUARRY, AND NO OTHER. It is the nearest living enemy, so only
	// the 8 m reach keeps the mark off it. Its own caster, so the cast below is not refused by a cooldown.
	{
		FQuarryJumpCaster Caster(World, FVector::ZeroVector);
		FQuarryJumpEnemy Quarry(World, FVector(3 * M, 0, 0));
		FQuarryJumpEnemy Beyond(World, FVector(3 * M, 10 * M, 0));

		UCataclysmSkillTemplate* Skill = Caster.Grant(*Row);
		if (!TestNotNull(TEXT("set-up: control: Quarry is granted"), Skill))
		{
			return false;
		}
		TestTrue(TEXT("set-up: control: Quarry is cast"), Caster.UseSkill(Skill));
		TestTrue(TEXT("set-up: control: the nearer enemy is marked"), Quarry.IsMarked());
		TestFalse(TEXT("set-up: control: the one ten metres beyond it is not"), Beyond.IsMarked());

		TestTrue(TEXT("control: the quarry dies"), UCataclysmSkillEffects::MarkDead(Quarry.Actor));
		TestFalse(TEXT("control: an enemy 10 m from the quarry does not gain the mark"), Beyond.IsMarked());
	}

	// AND THE JUMP: AN ENEMY FIVE METRES FROM THE QUARRY. A hundred metres from the control, outside every reach.
	FQuarryJumpCaster Caster(World, FVector(100 * M, 0, 0));
	FQuarryJumpEnemy Quarry(World, FVector(103 * M, 0, 0));
	FQuarryJumpEnemy Within(World, FVector(103 * M, 5 * M, 0));

	UCataclysmSkillTemplate* Skill = Caster.Grant(*Row);
	if (!TestNotNull(TEXT("set-up: Quarry is granted"), Skill))
	{
		return false;
	}
	TestTrue(TEXT("set-up: Quarry is cast"), Caster.UseSkill(Skill));
	TestTrue(TEXT("set-up: the nearer enemy is marked"), Quarry.IsMarked());
	TestFalse(TEXT("set-up: the one five metres beyond it is not"), Within.IsMarked());
	TestTrue(TEXT("set-up: the caster's quarry is the marked enemy"),
		UCataclysmCommand::QuarryOf(Caster.Actor) == Quarry.Actor);

	TestTrue(TEXT("the quarry dies"), UCataclysmSkillEffects::MarkDead(Quarry.Actor));
	TestTrue(TEXT("the nearest living enemy within 8 m gains the mark"), Within.IsMarked());
	TestTrue(TEXT("and it is now the caster's quarry"), UCataclysmCommand::QuarryOf(Caster.Actor) == Within.Actor);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmQuarryMarkJumpsAgainTest,
	"Cataclysm.Quarry.AMarkThatHasJumpedJumpsAgainWhenItsNewHolderDies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmQuarryMarkJumpsAgainTest::RunTest(const FString&)
{
	// "If the marked enemy dies first, the mark jumps to the nearest living enemy." The mark on its new holder is still
	// the mark, so that holder's death passes it on again. Ruled 2026-10-31.
	using namespace CataclysmQuarryJumpTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("the weapon skill table loads"), Table))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		return false;
	}
	const TArray<FCataclysmWeaponSkill> Staff = UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Staff"), TEXT("Demonic"));
	const FCataclysmWeaponSkill* Row = ShippedQuarry(Staff);
	if (!TestNotNull(TEXT("set-up: the Demonic Staff has Quarry"), Row)
		|| !TestEqual(TEXT("set-up: Quarry's jump reaches 8 m"), Row->Params.OnDeathRangeCm, 8.0f * M))
	{
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE CONTROL FIRST: A MARK PUT ON THE SECOND ENEMY BY HAND, which carries the tag and not the instruction to pass
	// it on. Its death passes nothing, so what passes the mark in the case below is the instruction going with it.
	{
		FQuarryJumpCaster Caster(World, FVector::ZeroVector);
		FQuarryJumpEnemy Second(World, FVector(3 * M, 5 * M, 0));
		FQuarryJumpEnemy Third(World, FVector(3 * M, 10 * M, 0));

		TestTrue(TEXT("set-up: control: the mark lands by hand"),
			UCataclysmSkillEffects::ApplyNamedEffect(Caster.Actor, Second.Actor, UCataclysmCommand::QuarryTag(),
				/*DurationSeconds=*/12.0f, /*Magnitude=*/0.0f, NAME_None));
		TestTrue(TEXT("control: the holder dies"), UCataclysmSkillEffects::MarkDead(Second.Actor));
		TestFalse(TEXT("control: a mark with no instruction to pass on reaches nobody"), Third.IsMarked());
	}

	// THREE ENEMIES IN A LINE, FIVE METRES APART. The third is ten metres from the first, outside the 8 m reach, so only
	// a second jump, from the second enemy, can reach it. A hundred metres from the control.
	FQuarryJumpCaster Caster(World, FVector(100 * M, 0, 0));
	FQuarryJumpEnemy First(World, FVector(103 * M, 0, 0));
	FQuarryJumpEnemy Second(World, FVector(103 * M, 5 * M, 0));
	FQuarryJumpEnemy Third(World, FVector(103 * M, 10 * M, 0));

	UCataclysmSkillTemplate* Skill = Caster.Grant(*Row);
	if (!TestNotNull(TEXT("set-up: Quarry is granted"), Skill))
	{
		return false;
	}
	TestTrue(TEXT("set-up: Quarry is cast"), Caster.UseSkill(Skill));
	TestTrue(TEXT("set-up: the first enemy is marked"), First.IsMarked());

	TestTrue(TEXT("the first holder dies"), UCataclysmSkillEffects::MarkDead(First.Actor));
	TestTrue(TEXT("the mark jumps to the second enemy"), Second.IsMarked());
	TestFalse(TEXT("and not yet to the third, ten metres from the first"), Third.IsMarked());

	TestTrue(TEXT("the second holder dies"), UCataclysmSkillEffects::MarkDead(Second.Actor));
	TestTrue(TEXT("the mark jumps again, to the third enemy"), Third.IsMarked());
	TestTrue(TEXT("and it is now the caster's quarry"), UCataclysmCommand::QuarryOf(Caster.Actor) == Third.Actor);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
