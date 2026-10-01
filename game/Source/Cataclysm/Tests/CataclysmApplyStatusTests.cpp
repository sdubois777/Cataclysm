// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * Issue #1833 group E part 1, ruled 2026-10-01 under the owner's delegation: a
 * status applied to the other character of an event, and the event
 * `first_hit_dealt`.
 *
 * `apply_status` rolls its value as a chance at the status's own duration;
 * `apply_status_seconds` always applies, for its value in seconds. The status is
 * an ailment, a stagger, or one debuff from the random pool. An ailment asks the
 * owner's rule of #917, a tenth of the target's maximum health, as every ailment
 * a blow carries does; a stagger does not, as no stagger does.
 *
 * `first_hit_dealt` IS ITS OWN EVENT because the target records a blow before the
 * blow is announced, so by `hit_dealt` the record already holds it. The first
 * test drives real blows through the game's damage path and counts the events on
 * the components' own `OnActionEvent`. The others build each action by hand, as
 * the item would build it from a row; the rows have tests of their own.
 */
namespace CataclysmApplyStatusTest
{
	/** A bare actor holding the vital and combat sets, with a thousand health. */
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
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		}

		~FFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute())
					  : -1.0f;
	}

	bool Carries(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System && Granted.IsValid() && System->HasMatchingGameplayTag(Granted);
	}

	/** How many running effects grant `Granted` on `Actor`. */
	int32 EffectsGranting(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
							FGameplayTagContainer(Granted))).Num()
					  : -1;
	}

	/** The longest time left on anything granting `Granted` on `Actor`, or zero. */
	float SecondsLeftOn(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		float Longest = 0.0f;
		if (System && Granted.IsValid())
		{
			for (const float Seconds : System->GetActiveEffectsTimeRemaining(
					 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted))))
			{
				Longest = FMath::Max(Longest, Seconds);
			}
		}
		return Longest;
	}

	/** What the running effect granting `Granted` on `Actor` has left to deal. */
	float RemainingOn(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		if (!System)
		{
			return -1.0f;
		}
		const TArray<FActiveGameplayEffectHandle> Found = System->GetActiveEffects(
			FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted)));
		return Found.Num() == 1
			? UCataclysmSkillEffects::RemainingDamageOverTime(System, Found[0])
			: -1.0f;
	}

	/** A status action, as the item would build it from a row. */
	FCataclysmPoolAction Status(ECataclysmApplyStatus Kind, const TCHAR* Event,
		const TCHAR* StatusName, float Value, float TriggerCooldownSeconds = 0.0f)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(Kind == ECataclysmApplyStatus::Seconds
			? UCataclysmAbilitySystemComponent::ApplyStatusSecondsAction
			: UCataclysmAbilitySystemComponent::ApplyStatusAction);
		Action.Percent = Value;
		Action.ApplyStatus = Kind;
		Action.StatusName = StatusName;
		Action.TriggerCooldownSeconds = TriggerCooldownSeconds;
		Action.TriggerKey = FName(*FString::Printf(TEXT("A_row:%s:%s:%s"),
			*Action.Pool.ToString(), StatusName, Event));
		return Action;
	}

	/** A player pawn with its ability system on a player state, as the game wires it. */
	ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!State || !Actor)
		{
			return nullptr;
		}
		Actor->SetPlayerState(State);
		Actor->OnRep_PlayerState();
		// AN ATTACK OF 300, as a stat line and an attribute: a blow asks the line
		// before the attribute.
		if (UCataclysmAbilitySystemComponent* System =
				Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent()))
		{
			TMap<FName, FCataclysmStatInputs> Lines;
			Lines.FindOrAdd(FName(TEXT("attack_damage"))).Base = 300.0f;
			System->SetStatInputs(MoveTemp(Lines));
			System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 300.0f);
		}
		return Actor;
	}

	/**
	 * A creature of the monsters' side with a thousand health, so a tenth is 100,
	 * AND NO EVASION, ARMOUR OR BLOCK: a blow that should land must not be dodged,
	 * and its size must not hang on a roll. Asserted by the tests, which read its
	 * health after each blow.
	 */
	ACataclysmEnemyCharacter* SpawnEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Actor =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Actor)
		{
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Actor->SetHealth(1000.0f);
			Actor->SetArmour(0.0f);
			if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
			{
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			}
		}
		return Actor;
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

	/**
	 * Pins `Cataclysm.RandomDebuffPick` for the life of this object, at the
	 * console's own priority, as `CataclysmTestWorld::FScopedCritRoll` does.
	 */
	struct FPinnedDebuffPick
	{
		explicit FPinnedDebuffPick(int32 Pick)
			: Variable(IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.RandomDebuffPick")))
		{
			if (Variable)
			{
				Previous = Variable->GetInt();
				Variable->Set(Pick, ECVF_SetByConsole);
			}
		}
		~FPinnedDebuffPick()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		int32 Previous = -1;
	};
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmFirstHitOrderTest,
	"Cataclysm.ApplyStatus.AFirstHitIsRaisedOncePerAttackerAndTargetThoughTheRecordHoldsTheBlow")
{
	using namespace CataclysmApplyStatusTest;
	// REAL BLOWS through `ApplyHit`, the damage calculation and the attribute set,
	// announced by `NoteBlow` to each player's `OnSomethingWasHit`. A player's
	// second blow on one creature raises `hit_dealt` and not `first_hit_dealt`; a
	// second player's first blow on the same creature raises it for that player;
	// and the first player's first blow on another creature raises it again.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmCombatEvents::In(World);
	ACataclysmPlayerCharacter* First = SpawnPlayer(World, FVector(0.0f, 0.0f, 0.0f));
	ACataclysmPlayerCharacter* Second = SpawnPlayer(World, FVector(0.0f, 300.0f, 0.0f));
	ACataclysmEnemyCharacter* Struck = SpawnEnemy(World, FVector(200.0f, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Other = SpawnEnemy(World, FVector(-200.0f, 0.0f, 0.0f));
	UCataclysmAbilitySystemComponent* FirstSystem = First
		? Cast<UCataclysmAbilitySystemComponent>(First->GetAbilitySystemComponent()) : nullptr;
	UCataclysmAbilitySystemComponent* SecondSystem = Second
		? Cast<UCataclysmAbilitySystemComponent>(Second->GetAbilitySystemComponent()) : nullptr;
	if (!TestNotNull(TEXT("set-up: the first player's ability system"), FirstSystem)
		|| !TestNotNull(TEXT("set-up: the second player's ability system"), SecondSystem)
		|| !TestNotNull(TEXT("set-up: a creature"), Struck)
		|| !TestNotNull(TEXT("set-up: another creature"), Other))
	{
		return false;
	}
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);
	const FEventCount FirstHits(FirstSystem, TEXT("hit_dealt"));
	const FEventCount FirstFirsts(FirstSystem, TEXT("first_hit_dealt"));
	const FEventCount SecondFirsts(SecondSystem, TEXT("first_hit_dealt"));

	// EACH BLOW IS GATED ON THE CREATURE'S HEALTH FALLING, so an evaded or
	// absorbed blow cannot pass for one that raised nothing.
	const auto Lands = [&](AActor* By, AActor* On)
	{
		const float Before = HealthOf(On);
		UCataclysmSkillEffects::ApplyHit(By, On, /*DamagePercent=*/10.0f);
		return HealthOf(On) < Before;
	};

	if (!TestTrue(TEXT("the first player's first blow lands"), Lands(First, Struck)))
	{
		return false;
	}
	TestEqual(TEXT("it raised hit_dealt"), FirstHits.Count, 1);
	TestEqual(TEXT("and first_hit_dealt"), FirstFirsts.Count, 1);

	if (!TestTrue(TEXT("its second blow on the same creature lands"), Lands(First, Struck)))
	{
		return false;
	}
	TestEqual(TEXT("it raised hit_dealt again"), FirstHits.Count, 2);
	TestEqual(TEXT("and not first_hit_dealt"), FirstFirsts.Count, 1);

	if (!TestTrue(TEXT("the second player's first blow on that creature lands"), Lands(Second, Struck)))
	{
		return false;
	}
	TestEqual(TEXT("it raised first_hit_dealt for the second player"), SecondFirsts.Count, 1);
	TestEqual(TEXT("and not for the first"), FirstFirsts.Count, 1);

	if (!TestTrue(TEXT("the first player's first blow on another creature lands"), Lands(First, Other)))
	{
		return false;
	}
	TestEqual(TEXT("it raised first_hit_dealt for that creature"), FirstFirsts.Count, 2);
	return true;
}

CATACLYSM_TEST(FCataclysmHitDealtCarriesItsAmountTest,
	"Cataclysm.ApplyStatus.HitDealtCarriesWhatReachedHealthSoABleedOnItNeedsATenth")
{
	using namespace CataclysmApplyStatusTest;
	// A REAL PLAYER WEARING A BLEED ON `hit_dealt`, always. A blow taking less
	// than a tenth of the creature's thousand applies none; one taking more
	// applies it. Before issue #1833 group E part 1 `hit_dealt` carried nothing,
	// and no blow would have.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmCombatEvents::In(World);
	ACataclysmPlayerCharacter* Player = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Struck = SpawnEnemy(World, FVector(200.0f, 0.0f, 0.0f));
	UCataclysmAbilitySystemComponent* System = Player
		? Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent()) : nullptr;
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	if (!TestNotNull(TEXT("set-up: the player's ability system"), System)
		|| !TestNotNull(TEXT("set-up: a creature"), Struck)
		|| !TestTrue(TEXT("set-up: the bleed tag exists"), Bleed.IsValid()))
	{
		return false;
	}
	System->SetPoolActions({Status(ECataclysmApplyStatus::Chance, TEXT("hit_dealt"), TEXT("Bleed"), 100.0f)});
	const CataclysmTestWorld::FScopedCritRoll NeverCritical(100.0f);

	float Before = HealthOf(Struck);
	UCataclysmSkillEffects::ApplyHit(Player, Struck, /*DamagePercent=*/10.0f);
	const float Small = Before - HealthOf(Struck);
	if (!TestTrue(*FString::Printf(TEXT("set-up: a small blow landed, under a tenth: %.1f"), Small),
			Small > 0.0f && Small < 100.0f))
	{
		return false;
	}
	TestFalse(TEXT("a blow under a tenth applies no bleed"), Carries(Struck, Bleed));

	Before = HealthOf(Struck);
	UCataclysmSkillEffects::ApplyHit(Player, Struck, /*DamagePercent=*/300.0f);
	const float Large = Before - HealthOf(Struck);
	if (!TestTrue(*FString::Printf(TEXT("set-up: a large blow landed, over a tenth: %.1f"), Large),
			Large >= 100.0f))
	{
		return false;
	}
	TestTrue(TEXT("a blow over a tenth applies the bleed"), Carries(Struck, Bleed));
	return true;
}

CATACLYSM_TEST(FCataclysmStaggerStatusTest,
	"Cataclysm.ApplyStatus.AStaggerLastsItsSecondOrTheStatedSecondsAndAsksNoTenth")
{
	using namespace CataclysmApplyStatusTest;
	// A STAGGER IS NOT AN AILMENT. A chance row at 100 staggers for the normal
	// second, a seconds row for its seconds, and both on an event carrying
	// nothing at all.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Staggered = UCataclysmSkillEffects::StaggeredTag();
	{
		const FFighter Wearer(World);
		const FFighter ByChance(World);
		const FFighter BySeconds(World);
		Wearer.AbilitySystem->SetPoolActions({
			Status(ECataclysmApplyStatus::Chance, TEXT("retaliation_dealt"), TEXT("Stagger"), 100.0f),
			Status(ECataclysmApplyStatus::Seconds, TEXT("deployable_hit"), TEXT("Stagger"), 2.5f)});

		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("retaliation_dealt")), nullptr, 0.0f, true, ByChance.Actor);
		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("deployable_hit")), nullptr, 0.0f, true, BySeconds.Actor);
		TestTrue(TEXT("the chance row staggered, with nothing reaching health"), Carries(ByChance.Actor, Staggered));
		TestEqual(TEXT("for the normal second"), SecondsLeftOn(ByChance.Actor, Staggered),
			UCataclysmSkillEffects::StaggerSeconds, 0.01f);
		TestTrue(TEXT("the seconds row staggered"), Carries(BySeconds.Actor, Staggered));
		TestEqual(TEXT("for its 2.5 seconds"), SecondsLeftOn(BySeconds.Actor, Staggered), 2.5f, 0.01f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmAilmentStatusGateTest,
	"Cataclysm.ApplyStatus.AnAilmentStatusAsksForATenthOfTheTargetsHealth")
{
	using namespace CataclysmApplyStatusTest;
	// THE OWNER'S RULE OF #917, AS `RollOnLandedBlow` ASKS IT OF ALL ELEVEN: a
	// bleed and a Cripple on an event carrying 99 of a thousand apply nothing,
	// and on one carrying 100 they apply.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Cripple = UCataclysmSkillShapes::StatusTagFor(TEXT("Cripple"));
	if (!TestTrue(TEXT("set-up: the two tags exist"), Bleed.IsValid() && Cripple.IsValid()))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FFighter Under(World);
		const FFighter AtATenth(World);
		Wearer.AbilitySystem->SetPoolActions({
			Status(ECataclysmApplyStatus::Chance, TEXT("critical_strike"), TEXT("Bleed"), 100.0f),
			Status(ECataclysmApplyStatus::Seconds, TEXT("critical_strike"), TEXT("Cripple"), 3.0f)});

		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("critical_strike")), nullptr, 99.0f, true, Under.Actor);
		TestFalse(TEXT("99 of a thousand applies no bleed"), Carries(Under.Actor, Bleed));
		TestFalse(TEXT("and no Cripple"), Carries(Under.Actor, Cripple));

		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("critical_strike")), nullptr, 100.0f, true, AtATenth.Actor);
		TestTrue(TEXT("100 of a thousand applies the bleed"), Carries(AtATenth.Actor, Bleed));
		TestTrue(TEXT("and the Cripple"), Carries(AtATenth.Actor, Cripple));
	}
	return true;
}

CATACLYSM_TEST(FCataclysmCrippleForSecondsTest,
	"Cataclysm.ApplyStatus.ACrippleForStatedSecondsKeepsItsOwnSlowForThoseSeconds")
{
	using namespace CataclysmApplyStatusTest;
	// "Retaliation damage applies a 2-4 second slow to the attacker", BY HAND at 2
	// seconds: the Cripple row's own 30% slow, for 2 seconds rather than its 4.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Cripple = UCataclysmSkillShapes::StatusTagFor(TEXT("Cripple"));
	if (!TestTrue(TEXT("set-up: the Cripple tag exists"), Cripple.IsValid()))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FFighter Attacker(World);
		Wearer.AbilitySystem->SetPoolActions({
			Status(ECataclysmApplyStatus::Seconds, TEXT("retaliation_dealt"), TEXT("Cripple"), 2.0f)});
		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("retaliation_dealt")), nullptr, 100.0f, true, Attacker.Actor);
		TestTrue(TEXT("the attacker is crippled"), Carries(Attacker.Actor, Cripple));
		TestEqual(TEXT("for the stated 2 seconds"), SecondsLeftOn(Attacker.Actor, Cripple), 2.0f, 0.01f);
		TestEqual(TEXT("at the Cripple row's own slow of 30"),
			UCataclysmSkillEffects::StatedStrengthOn(Attacker.Actor, Cripple), 30.0f, 0.01f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmRandomDebuffPoolTest,
	"Cataclysm.ApplyStatus.TheRandomDebuffPoolHoldsFiveDebuffsAndAppliesEach")
{
	using namespace CataclysmApplyStatusTest;
	// THE POOL RULED 2026-10-01: Madness, Cripple, Weaken, Shred and Stun, in that
	// order. Each pinned pick puts its own tag on a fresh creature from an event
	// carrying a fifth of its health.
	const TArray<const FCataclysmAilmentKind*> Pool = UCataclysmAilments::RandomDebuffPool();
	const TCHAR* const Expected[] = {
		TEXT("Madness"), TEXT("Cripple"), TEXT("Weaken"), TEXT("Shred"), TEXT("Stun")};
	if (!TestEqual(TEXT("five debuffs to choose among"), Pool.Num(), 5))
	{
		return false;
	}
	for (int32 Index = 0; Index < 5; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("the pool's place %d"), Index),
			FString(Pool[Index]->Ailment), FString(Expected[Index]));
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FFighter Wearer(World);
	Wearer.AbilitySystem->SetPoolActions({
		Status(ECataclysmApplyStatus::Chance, TEXT("hit_dealt"), TEXT("Random Debuff"), 100.0f)});
	for (int32 Index = 0; Index < Pool.Num(); ++Index)
	{
		ACataclysmEnemyCharacter* Creature = SpawnEnemy(World, FVector(200.0f * (Index + 1), 0.0f, 0.0f));
		const FGameplayTag Granted = Tag(Pool[Index]->TagName);
		if (!TestNotNull(TEXT("set-up: a creature"), Creature)
			|| !TestTrue(TEXT("set-up: the debuff's tag exists"), Granted.IsValid()))
		{
			return false;
		}
		const FPinnedDebuffPick Pinned(Index);
		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("hit_dealt")), nullptr, 200.0f, true, Creature);
		TestTrue(*FString::Printf(TEXT("pick %d puts %s on the creature"), Index, Pool[Index]->Ailment),
			Carries(Creature, Granted));
	}
	return true;
}

CATACLYSM_TEST(FCataclysmSecondBleedTest,
	"Cataclysm.ApplyStatus.ASecondBleedRefreshesAnEqualOrWeakerOneAndAStrongerOneReplacesIt")
{
	using namespace CataclysmApplyStatusTest;
	// THE ONE-STACK RULE (#913's context, the rulings of 2026-09-09): a target
	// carries one bleed. A second of equal strength refreshes how long it runs; a
	// stronger one replaces it; a weaker one refreshes the stronger one and
	// leaves its damage alone. The wearer's dot_damage sets the strength.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	if (!TestTrue(TEXT("set-up: the bleed tag exists"), Bleed.IsValid()))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FFighter Target(World);
		Wearer.AbilitySystem->SetPoolActions({
			Status(ECataclysmApplyStatus::Chance, TEXT("critical_strike"), TEXT("Bleed"), 100.0f)});
		const auto Strike = [&](float DotDamage)
		{
			Wearer.AbilitySystem->SetNumericAttributeBase(
				UCataclysmCombatAttributeSet::GetDotDamageAttribute(), DotDamage);
			Wearer.AbilitySystem->ActOnEvent(FName(TEXT("critical_strike")), nullptr, 100.0f, true, Target.Actor);
		};

		Strike(100.0f);
		const float Whole = SecondsLeftOn(Target.Actor, Bleed);
		const float Normal = RemainingOn(Target.Actor, Bleed);
		if (!TestEqual(TEXT("set-up: one bleed"), EffectsGranting(Target.Actor, Bleed), 1)
			|| !TestTrue(*FString::Printf(TEXT("set-up: it runs %.2f s and has %.1f to deal"), Whole, Normal),
				Whole > 2.0f && Normal > 0.0f))
		{
			return false;
		}

		CataclysmTestWorld::RunClock(World, 1.5f);
		const float Later = SecondsLeftOn(Target.Actor, Bleed);
		if (!TestTrue(*FString::Printf(TEXT("set-up: 1.5 s later it has %.2f s left"), Later), Later < Whole - 1.0f))
		{
			return false;
		}
		Strike(100.0f);
		TestEqual(TEXT("an equal second bleed leaves one"), EffectsGranting(Target.Actor, Bleed), 1);
		TestEqual(TEXT("and refreshes how long it runs"), SecondsLeftOn(Target.Actor, Bleed), Whole, 0.05f);

		Strike(200.0f);
		const float Stronger = RemainingOn(Target.Actor, Bleed);
		TestEqual(TEXT("a stronger second bleed leaves one"), EffectsGranting(Target.Actor, Bleed), 1);
		TestTrue(*FString::Printf(TEXT("and replaces it, dealing more: %.1f against %.1f"), Stronger, Normal),
			Stronger > Normal * 1.5f);

		CataclysmTestWorld::RunClock(World, 1.5f);
		Strike(50.0f);
		TestEqual(TEXT("a weaker second bleed leaves one"), EffectsGranting(Target.Actor, Bleed), 1);
		TestEqual(TEXT("and refreshes how long the stronger one runs"),
			SecondsLeftOn(Target.Actor, Bleed), Whole, 0.05f);
		TestTrue(*FString::Printf(TEXT("without weakening it: %.1f to deal"), RemainingOn(Target.Actor, Bleed)),
			RemainingOn(Target.Actor, Bleed) > Normal * 1.5f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmStatusTriggerCooldownTest,
	"Cataclysm.ApplyStatus.AStatusWaitsOutItsTriggerCooldownOnlyOnceItWasApplied")
{
	using namespace CataclysmApplyStatusTest;
	// "Gadgets apply a 1-2 second stagger to enemies they hit, once every 5
	// seconds", BY HAND at a second: staggered at once, not again two seconds
	// later though the first has ended, and again past five. And a bleed an event
	// was too small to carry starts no cooldown, so the next event applies it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Staggered = UCataclysmSkillEffects::StaggeredTag();
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	{
		const FFighter Wearer(World);
		const FFighter Target(World);
		const FFighter Bled(World);
		Wearer.AbilitySystem->SetPoolActions({
			Status(ECataclysmApplyStatus::Seconds, TEXT("deployable_hit"), TEXT("Stagger"), 1.0f, 5.0f),
			Status(ECataclysmApplyStatus::Chance, TEXT("retaliation_dealt"), TEXT("Bleed"), 100.0f, 5.0f)});
		const auto GadgetHits = [&]()
		{
			Wearer.AbilitySystem->ActOnEvent(FName(TEXT("deployable_hit")), nullptr, 0.0f, true, Target.Actor);
			return Carries(Target.Actor, Staggered);
		};
		TestTrue(TEXT("the first gadget hit staggers"), GadgetHits());
		CataclysmTestWorld::RunClock(World, 2.0f);
		if (!TestFalse(TEXT("set-up: two seconds later the stagger has ended"), Carries(Target.Actor, Staggered)))
		{
			return false;
		}
		TestFalse(TEXT("a gadget hit then staggers nothing: the five seconds run"), GadgetHits());
		CataclysmTestWorld::RunClock(World, 3.5f);
		TestTrue(TEXT("past five seconds a gadget hit staggers again"), GadgetHits());

		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("retaliation_dealt")), nullptr, 50.0f, true, Bled.Actor);
		TestFalse(TEXT("retaliation too small to carry a bleed applies none"), Carries(Bled.Actor, Bleed));
		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("retaliation_dealt")), nullptr, 150.0f, true, Bled.Actor);
		TestTrue(TEXT("and started no cooldown: the next applies it"), Carries(Bled.Actor, Bleed));
	}
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
