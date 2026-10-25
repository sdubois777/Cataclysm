// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/**
 * A DIAGNOSTIC: does a player skill work when a creature casts it?
 *
 * WHY. The Corrupted Stalker "fights with player skills rather than a monster ability list" (the row's sentence,
 * issue #1308 part B). `UCataclysmTriggeredSkill::Trigger` grants and starts a weapon-skill row on any actor's ability
 * system, and creatures own one, but no creature has ever cast a player skill, so whether it works is unmeasured. The
 * coordinating session asked for one small test to measure it on 2026-10-25.
 *
 * IT MEASURES AND DOES NOT JUDGE. What the creature's cast did is printed as information, in the line that begins
 * "ENEMY CAST:", and fails nothing: part B decides what it should do once the answer is known, and this test is then
 * replaced by one that asserts it. What it does assert is its set-up, and a control: the same skill cast by a player
 * at a creature hurts the creature, so a creature's cast that does nothing cannot be the rig's fault.
 *
 * THE RIG, from `CataclysmAoeStrikesAlliesTests.cpp`: a player pawn with its ability system on a player state, a
 * creature of the monsters' side, both with a weapon damage of 100, 2 m apart. The skill is a Strike all round of 4 m,
 * started from the caster's own place, which does not wait for an attack clip.
 */
namespace CataclysmEnemyCastTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	constexpr float M = 100.0f;
	constexpr float Pool = 100000.0f;

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : -1.0f;
	}

	void SetAttribute(const AActor* Actor, const FGameplayAttribute& Attribute, float Value)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}
	}

	/** Maximum then current, so the current value is not held under an old maximum; and a weapon damage of 100. */
	void Ready(const AActor* Actor)
	{
		SetAttribute(Actor, Vital::GetMaxHealthAttribute(), Pool);
		SetAttribute(Actor, Vital::GetHealthAttribute(), Pool);
		SetAttribute(Actor, Combat::GetAttackDamageAttribute(), 100.0f);
		SetAttribute(Actor, Vital::GetMaxManaAttribute(), 1000.0f);
		SetAttribute(Actor, Vital::GetManaAttribute(), 1000.0f);
	}

	FCataclysmWeaponSkill AllRoundStrike()
	{
		FCataclysmWeaponSkill Skill;
		Skill.Slot = ECataclysmAbilitySlot::Heavy;
		Skill.Name = TEXT("A Strike all round of 4 m");
		Skill.Shape = ECataclysmSkillShape::Strike;
		Skill.Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=4; Angle=360"));
		Skill.Tags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.AOE.PointBlank, Type.Melee"));
		return Skill;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmEnemyCastsAPlayerSkillTest,
	"Cataclysm.EnemyCastsAPlayerSkill.WhatATriggeredStrikeDoesWhenACreatureCastsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmEnemyCastsAPlayerSkillTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmEnemyCastTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector(2.0f * M, 0.0f, 0.0f), FRotator::ZeroRotator);
	ACataclysmEnemyCharacter* Creature =
		World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a player state"), State) || !TestNotNull(TEXT("set-up: a player"), Player)
		|| !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	Player->SetPlayerState(State);
	Player->OnRep_PlayerState();
	Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Ready(Player);
	Ready(Creature);
	if (!TestNotNull(TEXT("set-up: the creature has an ability system"), UCataclysmTargeting::AbilitySystemOf(Creature)))
	{
		return false;
	}

	const FCataclysmWeaponSkill Strike = AllRoundStrike();

	// THE CREATURE CASTS, FROM ITS OWN PLACE, AIMED AT THE PLAYER. Reported, not judged.
	const FVector CreatureAt = Creature->GetActorLocation();
	const float PlayerBefore = HealthOf(Player);
	const bool bCreatureStarted = UCataclysmTriggeredSkill::Trigger(
		Creature, Strike, Player->GetActorLocation(), /*DamageShare=*/1.0f, /*bPaysCost=*/false, &CreatureAt);
	const float PlayerLost = PlayerBefore - HealthOf(Player);
	AddInfo(FString::Printf(TEXT("ENEMY CAST: Trigger with a creature as owner returned %s; the player 2 m away lost "
								 "%.2f health of %.0f"),
							bCreatureStarted ? TEXT("true") : TEXT("false"), PlayerLost, PlayerBefore));

	// THE CONTROL: THE PLAYER CASTS THE SAME STRIKE AT THE CREATURE, and that must hurt it, or the rig is wrong.
	const FVector PlayerAt = Player->GetActorLocation();
	const float CreatureBefore = HealthOf(Creature);
	const bool bPlayerStarted = UCataclysmTriggeredSkill::Trigger(
		Player, Strike, CreatureAt, /*DamageShare=*/1.0f, /*bPaysCost=*/false, &PlayerAt);
	TestTrue(TEXT("control: the player's Strike started"), bPlayerStarted);
	TestTrue(FString::Printf(TEXT("control: the player's Strike hurt the creature: %.2f"),
							 CreatureBefore - HealthOf(Creature)),
			 HealthOf(Creature) < CreatureBefore);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
