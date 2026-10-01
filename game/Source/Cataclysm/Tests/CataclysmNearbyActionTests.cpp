// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmBeaconCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Issue #1833 group D part 3, ruled 2026-10-01 under the owner's delegation: a
 * blow breaking the energy shield, the player's death, and the two actions on
 * the enemies near the wearer, "When your energy shield is broken, you smite all
 * nearby enemies" and "On death all nearby enemies are healed for 10%-20% of
 * their maximum HP".
 *
 * EACH ACTION IS BUILT BY HAND, as the item would build it from a row, so these
 * tests read the mechanism. THE BREAK IS REACHED THROUGH A REAL BLOW and THE
 * DEATH THROUGH A REAL `HandleDeath`, not raised by hand, so the tests see each
 * event leave the code that raises it in play.
 */
namespace CataclysmNearbyActionTest
{
	constexpr float M = 100.0f;

	/** A bare actor holding every attribute set: a thousand health, a hundred shield. */
	struct FFighter
	{
		explicit FFighter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers on purpose: AddAttributeSetSubobject is a template and
			// a TObjectPtr deduces the wrapper rather than the set.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 100.0f);
			Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 100.0f);
		}

		~FFighter()
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

		float Shield() const
		{
			return AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetEnergyShieldAttribute());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	/** A blow from a fresh attacker carrying this much attack damage. */
	void Strike(UWorld* World, const FFighter& Defender, float AttackDamage)
	{
		const FFighter Attacker(World);
		Attacker.Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), AttackDamage);
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, /*DamagePercent=*/100.0f);
	}

	/** How many times one event is raised on a component while this is alive. */
	struct FEventCount
	{
		FEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
			: System(InSystem), Event(InEvent)
		{
			Handle = System->OnActionEvent.AddLambda([this](FName Raised)
			{
				Count += Raised == Event ? 1 : 0;
			});
		}
		~FEventCount()
		{
			System->OnActionEvent.Remove(Handle);
		}
		UCataclysmAbilitySystemComponent* System = nullptr;
		FName Event;
		int32 Count = 0;
		FDelegateHandle Handle;
	};

	/** A nearby action on an event, as the item would build it from a row. */
	FCataclysmPoolAction Nearby(ECataclysmNearbyAction Kind, const TCHAR* Event, float Percent,
		float CooldownSeconds = 0.0f)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(Kind == ECataclysmNearbyAction::Smite
			? UCataclysmAbilitySystemComponent::SmiteNearbyAction
			: UCataclysmAbilitySystemComponent::HealNearbyEnemiesAction);
		Action.Percent = Percent;
		Action.Nearby = Kind;
		Action.TriggerCooldownSeconds = CooldownSeconds;
		Action.TriggerKey = FName(*FString::Printf(TEXT("A_row:%s:%s"),
			*Action.Pool.ToString(), Event));
		return Action;
	}

	/**
	 * A player pawn with its ability system on a player state, as the game wires
	 * it; the shape `CataclysmDeathTests.cpp` uses.
	 */
	ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (State && Actor)
		{
			Actor->SetPlayerState(State);
			Actor->OnRep_PlayerState();
			return Actor;
		}
		return nullptr;
	}

	/** A creature of the monsters' side with a thousand health. */
	ACataclysmEnemyCharacter* SpawnEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Actor =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Actor)
		{
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			// SetHealth sets the MAXIMUM, and the current value follows it.
			Actor->SetHealth(1000.0f);
			Actor->SetAttackDamage(50.0f);
		}
		return Actor;
	}

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
					  : -1.0f;
	}

	/** Maximum then current, so a share of the maximum is a round number. */
	void SetHealth(const AActor* Actor, float Maximum, float Current)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Maximum);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Current);
		}
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmShieldBreakTest,
	"Cataclysm.NearbyAction.ABlowThatEmptiesTheShieldRaisesItsBreakOnceAndADrainDoesNot")
{
	using namespace CataclysmNearbyActionTest;

	// THE BREAK AS THE SACRIFICIAL WARD DEFINES "WOULD BREAK": the blow's shield
	// share reaches what the shield holds, while it holds something. A blow the
	// shield outlasts is no break, a blow at an empty shield is no break again,
	// and a shield emptied by a write rather than a blow is no break at all.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	{
		const FFighter Wearer(World);
		FEventCount Breaks(Wearer.AbilitySystem, TEXT("energy_shield_broken"));

		Strike(World, Wearer, 30.0f);
		const float Left = Wearer.Shield();
		if (!TestTrue(*FString::Printf(TEXT("set-up: a small blow reached the shield and left some: %.2f"),
				Left), Left > 0.0f && Left < 100.0f))
		{
			return false;
		}
		TestEqual(TEXT("a blow the shield outlasts breaks nothing"), Breaks.Count, 0);

		Strike(World, Wearer, 1000.0f);
		TestEqual(TEXT("the blow that empties it leaves no shield"), Wearer.Shield(), 0.0f, 0.01f);
		TestEqual(TEXT("and raises the break once"), Breaks.Count, 1);

		Strike(World, Wearer, 1000.0f);
		TestEqual(TEXT("a blow at a shield already empty raises no second break"), Breaks.Count, 1);

		// A WRITE, NOT A BLOW: a drain or a reservation that empties the shield.
		Wearer.Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 100.0f);
		Wearer.Set(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 0.0f);
		TestEqual(TEXT("a shield emptied by a write raises no break"), Breaks.Count, 1);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmSmiteNearbyTest,
	"Cataclysm.NearbyAction.ASmiteHitsEveryEnemyWithinFiveMetresAndNoneBeyondOrOnItsSide")
{
	using namespace CataclysmNearbyActionTest;

	// "When your energy shield is broken, you smite all nearby enemies", BY HAND:
	// a hit of 100% of weapon damage on every enemy within five metres. The
	// enemy beyond and the fellow player within are both left alone, the
	// second because the smite searches as the Nova does. Raised twice at once,
	// the quarter second default holds the second back.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Fellow = SpawnPlayer(World, FVector(0.0f, 3.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Near = SpawnEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = SpawnEnemy(World, FVector(8.0f * M, 0.0f, 0.0f));
	UCataclysmAbilitySystemComponent* AbilitySystem = SystemOf(Player);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a fellow player"), Fellow)
		|| !TestNotNull(TEXT("an enemy within five metres"), Near)
		|| !TestNotNull(TEXT("an enemy beyond them"), Far)
		|| !TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}
	AbilitySystem->SetNumericAttributeBase(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 200.0f);
	AbilitySystem->SetPoolActions({Nearby(ECataclysmNearbyAction::Smite,
		TEXT("energy_shield_broken"), 100.0f, /*CooldownSeconds=*/0.25f)});

	const float NearBefore = HealthOf(Near);
	const float FarBefore = HealthOf(Far);
	const float FellowBefore = HealthOf(Fellow);
	AbilitySystem->NoteEnergyShieldBroken();
	const float NearAfter = HealthOf(Near);
	TestTrue(*FString::Printf(TEXT("the enemy three metres away is struck: %.2f to %.2f"),
		NearBefore, NearAfter), NearAfter < NearBefore);
	TestEqual(TEXT("the enemy eight metres away is not"), HealthOf(Far), FarBefore, 0.01f);
	TestEqual(TEXT("nor the fellow player three metres away"), HealthOf(Fellow), FellowBefore, 0.01f);

	AbilitySystem->NoteEnergyShieldBroken();
	TestEqual(TEXT("a second break inside the quarter second smites nothing"),
		HealthOf(Near), NearAfter, 0.01f);
	return true;
}

CATACLYSM_TEST(FCataclysmDeathHealTest,
	"Cataclysm.NearbyAction.AHealOnDeathReachesNearbyEnemiesBeforeTheRespawnAndOnlyOnce")
{
	using namespace CataclysmNearbyActionTest;

	// "On death all nearby enemies are healed for 10%-20% of their maximum HP",
	// BY HAND at 10%, THROUGH A REAL DEATH: a blow empties the player's health,
	// `HandleDeath` runs, and the enemies within five metres, a floor source
	// among them, are healed by a tenth of their own maximum. Read BEFORE the
	// respawn, so the heal is shown to come before `ClearWhatDeathEnds`, which
	// `Revive` runs; and read again after it, so the respawn is shown to raise
	// no second death and heal nothing more.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Killer = SpawnEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = SpawnEnemy(World, FVector(8.0f * M, 0.0f, 0.0f));
	ACataclysmBeaconCharacter* Beacon = World->SpawnActor<ACataclysmBeaconCharacter>(
		FVector(0.0f, 4.0f * M, 0.0f), FRotator::ZeroRotator);
	UCataclysmAbilitySystemComponent* AbilitySystem = SystemOf(Player);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a killer within five metres"), Killer)
		|| !TestNotNull(TEXT("an enemy beyond them"), Far)
		|| !TestNotNull(TEXT("a floor source within them"), Beacon)
		|| !TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}
	Beacon->SetHealth(1000.0f);
	SetHealth(Killer, 1000.0f, 500.0f);
	SetHealth(Far, 1000.0f, 500.0f);
	SetHealth(Beacon, 1000.0f, 500.0f);
	AbilitySystem->SetPoolActions({Nearby(ECataclysmNearbyAction::HealEnemies,
		TEXT("player_death"), 10.0f)});
	FEventCount Deaths(AbilitySystem, TEXT("player_death"));

	UCataclysmSkillEffects::ApplyDirectDamage(Killer, Player, 100000.0f);
	if (!TestTrue(TEXT("the player died"), UCataclysmSkillEffects::IsDead(Player)))
	{
		return false;
	}
	TestEqual(TEXT("the death raised player_death once"), Deaths.Count, 1);
	TestEqual(TEXT("before the respawn, the enemy three metres away is healed by a tenth"),
		HealthOf(Killer), 600.0f, 0.01f);
	TestEqual(TEXT("and the floor source four metres away"), HealthOf(Beacon), 600.0f, 0.01f);
	TestEqual(TEXT("and not the enemy eight metres away"), HealthOf(Far), 500.0f, 0.01f);

	// DRIVEN DIRECTLY RATHER THAN WAITED FOR, as `CataclysmDeathTests.cpp` does:
	// a world built by UWorld::CreateWorld is never ticked, so its timers never
	// fire.
	Player->Revive();
	TestFalse(TEXT("the player stood back up"), UCataclysmSkillEffects::IsDead(Player));
	TestEqual(TEXT("the respawn raised no second player_death"), Deaths.Count, 1);
	TestEqual(TEXT("and healed nothing more"), HealthOf(Killer), 600.0f, 0.01f);
	return true;
}

CATACLYSM_TEST(FCataclysmMaddenedDeathHealTest,
	"Cataclysm.NearbyAction.AMaddenedPlayersDeathHealsNoThrallImpOrFellowPlayer")
{
	using namespace CataclysmNearbyActionTest;

	// A MADDENED PLAYER DIES. Madness makes the player's own thrall, imp and
	// fellow player answer the search for enemies, which is measured first so
	// the test is known to reach the case. Ruled 2026-10-01: none of them is
	// healed, because madness changes whom a creature attacks, not whose side
	// it is on; the creature of the monsters' side beside them is.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Fellow = SpawnPlayer(World, FVector(0.0f, 3.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Thrall = SpawnEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Creature = SpawnEnemy(World, FVector(-3.0f * M, 0.0f, 0.0f));
	UCataclysmAbilitySystemComponent* AbilitySystem = SystemOf(Player);
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("a fellow player"), Fellow)
		|| !TestNotNull(TEXT("a creature to take"), Thrall)
		|| !TestNotNull(TEXT("a creature of the monsters' side"), Creature)
		|| !TestNotNull(TEXT("the player's ability system"), AbilitySystem))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the player takes a thrall"),
			UCataclysmCommand::Subjugate(Player, Thrall)))
	{
		return false;
	}
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Player, FVector(0.0f, -3.0f * M, 0.0f),
		/*Lifetime=*/60.0f, /*bBurns=*/false);
	ON_SCOPE_EXIT { if (IsValid(Imp)) { Imp->Destroy(); } };
	if (!TestNotNull(TEXT("set-up: the player summons a minion"), Imp))
	{
		return false;
	}
	SetHealth(Thrall, 1000.0f, 500.0f);
	SetHealth(Imp, 1000.0f, 500.0f);
	SetHealth(Fellow, 1000.0f, 500.0f);
	SetHealth(Creature, 1000.0f, 500.0f);

	UCataclysmSkillEffects::ApplyTagForDuration(Player, Player, UCataclysmTeams::MadnessTag(), 10.0f);
	if (!TestTrue(TEXT("set-up: the player is maddened"), UCataclysmTeams::IsMaddened(Player)))
	{
		return false;
	}
	const TArray<AActor*> Searched = UCataclysmTargeting::FindEnemiesInSphere(
		World, Player, Player->GetActorLocation(), UCataclysmAbilitySystemComponent::NearbyActionRadiusCm);
	if (!TestTrue(TEXT("set-up: the maddened player's search for enemies finds the thrall"),
			Searched.Contains(Thrall))
		|| !TestTrue(TEXT("set-up: and the minion"), Searched.Contains(Imp))
		|| !TestTrue(TEXT("set-up: and the fellow player"), Searched.Contains(Fellow)))
	{
		return false;
	}

	AbilitySystem->SetPoolActions({Nearby(ECataclysmNearbyAction::HealEnemies,
		TEXT("player_death"), 10.0f)});
	UCataclysmSkillEffects::ApplyDirectDamage(Creature, Player, 100000.0f);
	if (!TestTrue(TEXT("the player died"), UCataclysmSkillEffects::IsDead(Player)))
	{
		return false;
	}
	TestEqual(TEXT("the creature of the monsters' side is healed by a tenth"),
		HealthOf(Creature), 600.0f, 0.01f);
	TestEqual(TEXT("the thrall, the player's by ownership, is not"), HealthOf(Thrall), 500.0f, 0.01f);
	TestEqual(TEXT("the minion, the player's by team, is not"), HealthOf(Imp), 500.0f, 0.01f);
	TestEqual(TEXT("the fellow player, of the player's team, is not"), HealthOf(Fellow), 500.0f, 0.01f);
	return true;
}

CATACLYSM_TEST(FCataclysmShareSideIgnoringMadnessTest,
	"Cataclysm.NearbyAction.ShareSideIgnoringMadnessAsksOwnershipAndTeamEachOnTheirOwn")
{
	using namespace CataclysmNearbyActionTest;

	// THE TWO HALVES APART, which the death test cannot do: a thrall is both
	// owned and given its commander's team, so it would hold through either.
	// Here an owned thing carries no team, and two of one team own nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Owner = World->SpawnActor<AActor>();
	AActor* Owned = World->SpawnActor<AActor>();
	AActor* Stranger = World->SpawnActor<AActor>();
	ACataclysmEnemyCharacter* One = SpawnEnemy(World, FVector(0.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Two = SpawnEnemy(World, FVector(5.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Other = SpawnEnemy(World, FVector(10.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("an owner"), Owner) || !TestNotNull(TEXT("a thing it owns"), Owned)
		|| !TestNotNull(TEXT("a stranger"), Stranger) || !TestNotNull(TEXT("a creature"), One)
		|| !TestNotNull(TEXT("a second of its team"), Two)
		|| !TestNotNull(TEXT("a creature of another team"), Other))
	{
		return false;
	}
	Owned->SetOwner(Owner);
	Other->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
	if (!TestTrue(TEXT("set-up: the bare actors carry no team"),
			UCataclysmTeams::TeamOf(Owner) == FGenericTeamId::NoTeam
			&& UCataclysmTeams::TeamOf(Owned) == FGenericTeamId::NoTeam))
	{
		return false;
	}

	TestTrue(TEXT("owned, with no team: one side"), UCataclysmTeams::ShareSideIgnoringMadness(Owner, Owned));
	TestTrue(TEXT("and asked the other way round"), UCataclysmTeams::ShareSideIgnoringMadness(Owned, Owner));
	TestFalse(TEXT("no owner and no team: no side"), UCataclysmTeams::ShareSideIgnoringMadness(Owner, Stranger));
	TestTrue(TEXT("one team, owning nothing: one side"), UCataclysmTeams::ShareSideIgnoringMadness(One, Two));
	TestFalse(TEXT("two teams: two sides"), UCataclysmTeams::ShareSideIgnoringMadness(One, Other));

	UCataclysmSkillEffects::ApplyTagForDuration(One, One, UCataclysmTeams::MadnessTag(), 10.0f);
	if (!TestTrue(TEXT("set-up: one is maddened"), UCataclysmTeams::IsMaddened(One)))
	{
		return false;
	}
	TestEqual(TEXT("set-up: so the attitude between them is hostile"),
		static_cast<int32>(UCataclysmTeams::AttitudeBetween(One, Two)),
		static_cast<int32>(ETeamAttitude::Hostile));
	TestTrue(TEXT("and they are still one side"), UCataclysmTeams::ShareSideIgnoringMadness(One, Two));

	Owner->Destroy();
	Owned->Destroy();
	Stranger->Destroy();
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
