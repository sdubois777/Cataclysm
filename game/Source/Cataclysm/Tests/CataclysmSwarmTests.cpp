// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Swarm, the Ritualist keystone since the demonic tree rework of 2026-11-24: "When you or one of your minions kills
 * an enemy, a new minion is summoned where it fell, if your Fervour has room for it." Its row is
 * `kill_summons_minion_where_it_fell`, a flat 1, read by `UCataclysmSummonSkill::SummonAtAKillFor` from
 * `ACataclysmPlayerCharacter::OnSomethingDied`.
 *
 * A REAL PLAYER ON A REAL PLAYER STATE, so every kill is heard by the listener the game binds, and every kill is a blow
 * that takes a creature's last health. The player holds NO SUMMON SKILL, which is a Wand Ritualist: the owner answered
 * on 2026-11-24 that The Swarm works for one, so what it summons is Summon Imp's kind, read from its row.
 */
namespace CataclysmSwarmTest
{
	constexpr float M = 100.0f;

	/** The pool, room for many imps of Summon Imp's 10. */
	constexpr float SwarmPool = 150.0f;

	struct FSwarmScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** The damage lines a blow needs, and The Swarm's flag when `bKeystone`. Replaces the whole stat line. */
	void WearSwarm(const FSwarmScene& Scene, bool bKeystone)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.Add(FName(TEXT("attack_damage"))).Base = 100.0f;
		Lines.Add(FName(TEXT("spell_damage"))).Base = 100.0f;
		if (bKeystone)
		{
			FCataclysmStatModifier Flag;
			Flag.Bucket = ECataclysmStatBucket::Flat;
			Flag.Source = ECataclysmModifierSource::PassiveKeystone;
			Flag.Value = 1.0f;
			FCataclysmStatInputs& Line = Lines.Add(FName(UCataclysmSummonSkill::KillSummonsMinionStat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flag};
		}
		Scene.System->SetStatInputs(MoveTemp(Lines));
	}

	bool BuildSwarmScene(FAutomationTestBase& Test, UWorld* World, const FVector& Where, FSwarmScene& Out)
	{
		Out.State = World->SpawnActor<ACataclysmPlayerState>();
		Out.Player = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("set-up: a player state"), Out.State)
			|| !Test.TestNotNull(TEXT("set-up: a player"), Out.Player))
		{
			return false;
		}
		Out.Player->SetPlayerState(Out.State);
		Out.Player->OnRep_PlayerState();
		Out.System = Out.State->GetCataclysmAbilitySystemComponent();
		if (!Test.TestNotNull(TEXT("set-up: the player's ability system"), Out.System))
		{
			return false;
		}
		Out.System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		Out.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		Out.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
											SwarmPool);
		WearSwarm(Out, /*bKeystone=*/false);
		return true;
	}

	/** A creature of the monsters' side with one health left, so any blow kills it. */
	ACataclysmEnemyCharacter* FrailCreature(UWorld* World, const FVector& At)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(1000.0f);
			if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Made))
			{
				Theirs->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1.0f);
			}
		}
		return Made;
	}

	/** One blow of the player's at 100%, never a critical strike, in the name of a test strike. */
	void SwarmBlow(const FSwarmScene& Scene, AActor* Target)
	{
		UCataclysmStrikeSkill* Swing = NewObject<UCataclysmStrikeSkill>(Scene.Player);
		Swing->SkillName = TEXT("Test Swing");
		Swing->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Strike"))));
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 0.0f;
		Delivery.Skill = Swing;
		UCataclysmSkillEffects::ApplyHit(Scene.Player, Target, /*DamagePercent=*/100.0f, Swing->SkillTags, Delivery);
	}

	bool IsGone(const AActor* Who)
	{
		return !IsValid(Who) || UCataclysmSkillEffects::IsDead(Who);
	}

	TArray<ACataclysmMinion*> ImpsOf(const FSwarmScene& Scene)
	{
		return UCataclysmCommand::MinionsOfTypeCommandedBy(Scene.Player, TEXT("Imp"));
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a
// `using namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSwarmOwnKillTest,
	"Cataclysm.Swarm.AKillOfYoursSummonsSummonImpsKindWhereTheEnemyFellWithNoSummonSkillHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE CONTROL FIRST: without the keystone the player's kill summons nothing. With it, the next kill summons one imp of
 * Summon Imp's kind where the creature died, though the player holds no summon skill, and it holds Summon Imp's
 * reserve.
 */
bool FCataclysmSwarmOwnKillTest::RunTest(const FString&)
{
	using namespace CataclysmSwarmTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FSwarmScene Scene;
	if (!BuildSwarmScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}

	ACataclysmEnemyCharacter* Before = FrailCreature(World, FVector(4 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a creature"), Before))
	{
		return false;
	}
	SwarmBlow(Scene, Before);
	if (!TestTrue(TEXT("set-up: the blow killed it"), IsGone(Before))
		|| !TestEqual(TEXT("control: without The Swarm a kill summons nothing"), ImpsOf(Scene).Num(), 0))
	{
		return false;
	}

	WearSwarm(Scene, /*bKeystone=*/true);
	ACataclysmEnemyCharacter* After = FrailCreature(World, FVector(8 * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a second creature"), After))
	{
		return false;
	}
	const FVector Died = After->GetActorLocation();
	SwarmBlow(Scene, After);
	if (!TestTrue(TEXT("set-up: the blow killed the second"), IsGone(After)))
	{
		return false;
	}
	const TArray<ACataclysmMinion*> Summoned = ImpsOf(Scene);
	if (!TestEqual(TEXT("with The Swarm the kill summons one imp"), Summoned.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("it stands where the creature fell"),
			  static_cast<float>(FVector::Dist2D(Summoned[0]->GetActorLocation(), Died)), 0.0f, 1.0f);
	TestFalse(TEXT("it is no summon skill's: none is held"), Summoned[0]->SummonedBy.IsValid());
	TestEqual(TEXT("and it holds Summon Imp's reserve of 10"), UCataclysmCommand::ReservedFervourOf(Scene.Player),
			  10.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSwarmMinionKillTest,
	"Cataclysm.Swarm.AKillByOneOfYourMinionsSummonsAnotherAndAFullPoolNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "When you OR ONE OF YOUR MINIONS kills an enemy". An imp of the player's strikes a creature with one health left; the
 * death's killer is the imp, not the player (no Conduit), and the keystone still summons a second imp. Then, with the
 * pool set to what the two hold, the next kill by the imp summons nothing and destroys nothing.
 */
bool FCataclysmSwarmMinionKillTest::RunTest(const FString&)
{
	using namespace CataclysmSwarmTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FSwarmScene Scene;
	if (!BuildSwarmScene(*this, World, FVector::ZeroVector, Scene))
	{
		return false;
	}
	WearSwarm(Scene, /*bKeystone=*/true);

	ACataclysmMinion* Striker = ACataclysmMinion::Spawn(Scene.Player, FVector(2 * M, 2 * M, 0), /*Lifetime=*/60.0f,
														/*bBurns=*/false, TEXT("Imp"));
	ACataclysmEnemyCharacter* Victim = FrailCreature(World, FVector(6 * M, 2 * M, 0));
	if (!TestNotNull(TEXT("set-up: the player's imp"), Striker) || !TestNotNull(TEXT("set-up: a creature"), Victim)
		|| !TestEqual(TEXT("set-up: the player commands one imp"), ImpsOf(Scene).Num(), 1))
	{
		return false;
	}

	Striker->AttackTarget(Victim);
	if (!TestTrue(TEXT("set-up: the imp's blow killed the creature"), IsGone(Victim)))
	{
		return false;
	}
	TestEqual(TEXT("a kill by the player's imp summons a second imp"), ImpsOf(Scene).Num(), 2);

	// THE POOL HOLDS NO MORE: what the summoned imp reserves, and the striker's nought, so another 10 has no room.
	Scene.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
										  UCataclysmCommand::ReservedFervourOf(Scene.Player));
	if (!TestFalse(TEXT("set-up: the pool cannot hold another imp's 10"),
				   UCataclysmCommand::HasRoomToReserve(Scene.Player, 10.0f)))
	{
		return false;
	}
	ACataclysmEnemyCharacter* NoRoom = FrailCreature(World, FVector(6 * M, -2 * M, 0));
	if (!TestNotNull(TEXT("set-up: a third creature"), NoRoom))
	{
		return false;
	}
	Striker->AttackTarget(NoRoom);
	TestTrue(TEXT("set-up: the imp's blow killed it"), IsGone(NoRoom));
	TestEqual(TEXT("with no room in the pool a kill summons none, and none of the two is gone"),
			  ImpsOf(Scene).Num(), 2);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
