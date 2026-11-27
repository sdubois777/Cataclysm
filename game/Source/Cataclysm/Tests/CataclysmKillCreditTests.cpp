// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Data/CataclysmDataRows.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Deaths the Soul Collector did not make (the Death trees' slice C4b2, 2026-11-25): Death's Toll's follower kill, which
 * the starting node grants since the keystone rework (issue #2951), when Reclaimed and Claimed were retired. Its
 * mechanism is read with stats given by hand, so each figure is exact. `tools/tests/test_kill_credit_reaches_the_soul_collector.py` checks the order the
 * player's kill handler calls them in. ONE CASE GOES THROUGH THE WHOLE PLAYER, as the coordinating session asked:
 * `AMinionsKillPaysTheRealPlayerUnderDeathsToll`, on the scene `Cataclysm.Swarm.` builds, a real player on a real player
 * state whose real imp kills a creature, heard by `ACataclysmPlayerCharacter::OnSomethingDied` as the game hears it.
 */

namespace CataclysmKillCreditTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	/** An actor holding 1,000 health and an empty Fervour pool of 100, able to be given stat lines and action rows. */
	struct FKillCreditHolder
	{
		explicit FKillCreditHolder(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers, because AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												   100.0f);
			SetFervour(0.0f);
		}

		~FKillCreditHolder()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void SetFervour(float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
												   Value);
		}

		float Fervour() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** A line of one flat row on a stat, with no base. */
	void CreditLine(TMap<FName, FCataclysmStatInputs>& Lines, const TCHAR* Stat, float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Lines.FindOrAdd(FName(Stat)).Modifiers = {Row};
	}

	/** An action row restoring this share of maximum health on `kill`, under the trigger key given. */
	FCataclysmPoolAction HealOnKill(const TCHAR* TriggerKey, float Percent)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("kill"));
		Action.Pool = FName(TEXT("health"));
		Action.Percent = Percent;
		Action.TriggerKey = FName(TriggerKey);
		return Action;
	}

	/** A death notice: this killer, this victim. */
	FCataclysmDeathNotice KilledBy(AActor* Killer, AActor* Victim)
	{
		FCataclysmDeathNotice Notice;
		Notice.Killer = Killer;
		Notice.Victim = Victim;
		return Notice;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillCreditDeathsTollTest,
	"Cataclysm.KillCredit.DeathsTollMakesAFollowersKillYours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Death's Toll, on a character paying 5 Fervour a kill with a grace of 4 seconds. A kill by an actor it owns, which
 * `UCataclysmCommand::CommanderOf` reads as its follower, is credited: it pays 5, stamps the kill clock and begins the
 * hold. THE CONTROLS: without Death's Toll the same kill is not credited and pays nothing; with it, a kill by an actor
 * nobody owns is not credited.
 */
bool FCataclysmKillCreditDeathsTollTest::RunTest(const FString&)
{
	using namespace CataclysmKillCreditTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FKillCreditHolder Commander(World);
	UCataclysmAbilitySystemComponent* System = Commander.AbilitySystem;
	AActor* Follower = World->SpawnActor<AActor>();
	AActor* Stranger = World->SpawnActor<AActor>();
	AActor* Slain = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: a follower"), Follower) || !TestNotNull(TEXT("set-up: a stranger"), Stranger)
		|| !TestNotNull(TEXT("set-up: a slain creature"), Slain))
	{
		return false;
	}
	Follower->SetOwner(Commander.Actor);

	const auto Lines = [](bool bWithDeathsToll)
	{
		TMap<FName, FCataclysmStatInputs> Made;
		CreditLine(Made, UCataclysmFervour::PerKillStat, 5.0f);
		CreditLine(Made, UCataclysmFervour::KillDecayGraceSecondsStat, 4.0f);
		if (bWithDeathsToll)
		{
			CreditLine(Made, UCataclysmFervour::FollowerKillsCountAsYoursStat, 1.0f);
		}
		return Made;
	};

	System->SetStatInputs(Lines(false));
	TestFalse(TEXT("control: without Death's Toll a follower's kill is not credited"),
			  UCataclysmFervour::CreditAFollowersKill(System, Commander.Actor, KilledBy(Follower, Slain)));
	TestEqual(TEXT("and pays nothing"), Commander.Fervour(), 0.0f, 0.001f);

	System->SetStatInputs(Lines(true));
	TestFalse(TEXT("control: with it, a kill by an actor nobody owns is not credited"),
			  UCataclysmFervour::CreditAFollowersKill(System, Commander.Actor, KilledBy(Stranger, Slain)));
	TestTrue(TEXT("with it, a follower's kill is credited"),
			 UCataclysmFervour::CreditAFollowersKill(System, Commander.Actor, KilledBy(Follower, Slain)));
	TestEqual(TEXT("it pays the kill's 5"), Commander.Fervour(), 5.0f, 0.001f);
	TestTrue(TEXT("it stamps the kill clock"), System->SecondsSinceOwnKill() >= 0.0f);
	TestTrue(TEXT("and begins the hold"), System->KillDecayIsHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillCreditSoulCollectorRowsTest,
	"Cataclysm.KillCredit.DeathsTollRaisesTheKillToTheSoulCollectorsRowsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Death's Toll on a character at 500 of 1,000 health holding two action rows on `kill`: a Soul Collector row (its
 * trigger key beginning `Passive_SoulCollector_`) restoring 10% of maximum health, and a Ravager row restoring 20%. A
 * follower's kill raises `kill` to the Soul Collector row only: 600. THE CONTROL: the character's own kill raises it to
 * both: 800.
 */
bool FCataclysmKillCreditSoulCollectorRowsTest::RunTest(const FString&)
{
	using namespace CataclysmKillCreditTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FKillCreditHolder Commander(World);
	UCataclysmAbilitySystemComponent* System = Commander.AbilitySystem;
	AActor* Follower = World->SpawnActor<AActor>();
	AActor* Slain = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: a follower"), Follower) || !TestNotNull(TEXT("set-up: a slain creature"), Slain))
	{
		return false;
	}
	Follower->SetOwner(Commander.Actor);

	TMap<FName, FCataclysmStatInputs> Lines;
	CreditLine(Lines, UCataclysmFervour::FollowerKillsCountAsYoursStat, 1.0f);
	System->SetStatInputs(MoveTemp(Lines));
	const FString SoulCollectorKey = UCataclysmFervour::SoulCollectorRowKeyPrefix() + TEXT("basic_b_a0:health:kill");
	TestEqual(TEXT("set-up: the Soul Collector's rows begin Passive_SoulCollector_"),
			  UCataclysmFervour::SoulCollectorRowKeyPrefix(), FString(TEXT("Passive_SoulCollector_")));
	System->SetPoolActions({HealOnKill(*SoulCollectorKey, 10.0f),
							HealOnKill(TEXT("Passive_Ravager_basic_d_c1:health:kill"), 20.0f)});
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();

	System->SetNumericAttributeBase(Health, 500.0f);
	TestTrue(TEXT("set-up: the follower's kill is credited"),
			 UCataclysmFervour::CreditAFollowersKill(System, Commander.Actor, KilledBy(Follower, Slain)));
	TestEqual(TEXT("a follower's kill raises kill to the Soul Collector row only: 600"), Commander.Health(), 600.0f,
			  0.01f);

	System->SetNumericAttributeBase(Health, 500.0f);
	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, Slain);
	TestEqual(TEXT("control: the character's own kill raises it to both: 800"), Commander.Health(), 800.0f, 0.01f);
	return true;
}

namespace CataclysmKillCreditEndToEndTest
{
	constexpr float CreditMetre = 100.0f;

	/** A real player on a real player state, as `Cataclysm.Swarm.` builds one. */
	struct FKillCreditScene
	{
		ACataclysmPlayerCharacter* Player = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** The damage lines a blow needs, 5 Fervour a kill, and Death's Toll when `bDeathsToll`. Replaces the stat lines. */
	void WearDeathsToll(const FKillCreditScene& Scene, bool bDeathsToll)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.Add(FName(TEXT("attack_damage"))).Base = 100.0f;
		Lines.Add(FName(TEXT("spell_damage"))).Base = 100.0f;
		FCataclysmStatModifier Five;
		Five.Bucket = ECataclysmStatBucket::Flat;
		Five.Source = ECataclysmModifierSource::PassiveKeystone;
		Five.Value = 5.0f;
		Lines.Add(FName(UCataclysmFervour::PerKillStat)).Modifiers = {Five};
		if (bDeathsToll)
		{
			FCataclysmStatModifier Flag = Five;
			Flag.Value = 1.0f;
			Lines.Add(FName(UCataclysmFervour::FollowerKillsCountAsYoursStat)).Modifiers = {Flag};
		}
		Scene.System->SetStatInputs(MoveTemp(Lines));
	}

	/** The scene `Cataclysm.Swarm.`'s `BuildSwarmScene` builds, with a Fervour pool of 100. */
	bool BuildCreditScene(FAutomationTestBase& Test, UWorld* World, FKillCreditScene& Out)
	{
		Out.State = World->SpawnActor<ACataclysmPlayerState>();
		Out.Player = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
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
		Out.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		WearDeathsToll(Out, /*bDeathsToll=*/false);
		return true;
	}

	/** A creature of the monsters' side with one health left, so any blow kills it. */
	ACataclysmEnemyCharacter* OneHealthCreature(UWorld* World, const FVector& At)
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

	float PlayersFervour(const FKillCreditScene& Scene)
	{
		return Scene.System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKillCreditEndToEndTest,
	"Cataclysm.KillCredit.AMinionsKillPaysTheRealPlayerUnderDeathsToll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path, as the coordinating session asked: a real player on a real player state paying 5 Fervour a kill, and
 * a real imp of its, which kills a creature with one health left. The death's killer is the imp (no Conduit), and
 * `ACataclysmPlayerCharacter::OnSomethingDied` hears it. With Death's Toll the kill pays the player 5 and stamps its kill
 * clock. THE CONTROL: the same kill without Death's Toll pays the player nothing and stamps nothing.
 */
bool FCataclysmKillCreditEndToEndTest::RunTest(const FString&)
{
	using namespace CataclysmKillCreditEndToEndTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	FKillCreditScene Scene;
	if (!BuildCreditScene(*this, World, Scene))
	{
		return false;
	}
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Scene.Player, FVector(2 * CreditMetre, 2 * CreditMetre, 0),
													/*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
	ACataclysmEnemyCharacter* First = OneHealthCreature(World, FVector(6 * CreditMetre, 2 * CreditMetre, 0));
	ACataclysmEnemyCharacter* Second = OneHealthCreature(World, FVector(6 * CreditMetre, -2 * CreditMetre, 0));
	if (!TestNotNull(TEXT("set-up: the player's imp"), Imp) || !TestNotNull(TEXT("set-up: a creature"), First)
		|| !TestNotNull(TEXT("set-up: a second creature"), Second))
	{
		return false;
	}

	const float BeforeTheControl = PlayersFervour(Scene);
	Imp->AttackTarget(First);
	if (!TestTrue(TEXT("set-up: the imp's blow killed the first creature"),
				  !IsValid(First) || UCataclysmSkillEffects::IsDead(First)))
	{
		return false;
	}
	TestEqual(TEXT("control: without Death's Toll the imp's kill pays the player nothing"),
			  PlayersFervour(Scene) - BeforeTheControl, 0.0f, 0.001f);
	TestTrue(TEXT("control: and stamps no kill clock"), Scene.System->SecondsSinceOwnKill() < 0.0f);

	WearDeathsToll(Scene, /*bDeathsToll=*/true);
	const float BeforeTheToll = PlayersFervour(Scene);
	Imp->AttackTarget(Second);
	if (!TestTrue(TEXT("set-up: the imp's blow killed the second creature"),
				  !IsValid(Second) || UCataclysmSkillEffects::IsDead(Second)))
	{
		return false;
	}
	TestEqual(TEXT("with Death's Toll the imp's kill pays the player 5"), PlayersFervour(Scene) - BeforeTheToll, 5.0f,
			  0.001f);
	TestTrue(TEXT("and stamps the player's kill clock"), Scene.System->SecondsSinceOwnKill() >= 0.0f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
