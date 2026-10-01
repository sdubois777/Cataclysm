// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
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
#include "Misc/ScopeExit.h"

/**
 * Issue #1833 group D part 4, ruled 2026-10-01 under the owner's delegation: the
 * remaining damage of the wearer's own damage over time effects, dealt at once.
 * "When you die, all active DoTs on nearby enemies instantly deal their remaining
 * damage" ends each one; "Necrosis effects deal 20%-40% of their remaining damage
 * instantly when you land a critical strike" ends nothing.
 *
 * THE TICKS ARE REAL: each effect is applied through the game's own functions and
 * the clock is run, so what remains is what the engine's period timer has left,
 * not a number written by hand. THE DEATH IS REAL: the player dies through a blow,
 * and `HandleDeath` raises `player_death`. Each action is built by hand, as the
 * item would build it from a row; the rows have tests of their own.
 */
namespace CataclysmRemainingDamageTest
{
	constexpr float M = 100.0f;

	/** A bare actor holding every attribute set, with a thousand health. */
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

	/** The running effect granting `Granted` on `Actor`, or an invalid handle. */
	FActiveGameplayEffectHandle Running(const AActor* Actor, const FGameplayTag& Granted)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		if (!System)
		{
			return FActiveGameplayEffectHandle();
		}
		const TArray<FActiveGameplayEffectHandle> Found = System->GetActiveEffects(
			FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Granted)));
		return Found.IsEmpty() ? FActiveGameplayEffectHandle() : Found[0];
	}

	float RemainingOn(const AActor* Actor, const FGameplayTag& Granted)
	{
		return UCataclysmSkillEffects::RemainingDamageOverTime(
			UCataclysmTargeting::AbilitySystemOf(Actor), Running(Actor, Granted));
	}

	/** A damage over time of ten a tick for ten seconds, in `Instigator`'s name, unscaled. */
	bool TenATick(AActor* Instigator, AActor* Target, const FGameplayTag& Granted,
		float Seconds = 10.0f)
	{
		return UCataclysmSkillEffects::ApplyDamageOverTime(Instigator, Target,
			/*DamagePerTick=*/10.0f, Seconds, Granted, /*bScalesWithInstigator=*/false);
	}

	/** A remaining damage action, as the item would build it from a row. */
	FCataclysmPoolAction Remaining(ECataclysmRemainingDamage Kind, const TCHAR* Event, float Percent,
		const FGameplayTag& Ailment = FGameplayTag())
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(Event);
		Action.Pool = FName(Kind == ECataclysmRemainingDamage::Nearby
			? UCataclysmAbilitySystemComponent::RemainingDamageNearbyAction
			: UCataclysmAbilitySystemComponent::RemainingDamageTargetAction);
		Action.Percent = Percent;
		Action.RemainingDamage = Kind;
		Action.Ailment = Ailment;
		Action.TriggerKey = FName(*FString::Printf(TEXT("A_row:%s:%s"), *Action.Pool.ToString(), Event));
		return Action;
	}

	/** A player pawn with its ability system on a player state, as the game wires it. */
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
			Actor->SetHealth(1000.0f);
			Actor->SetAttackDamage(50.0f);
		}
		return Actor;
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_TEST(FCataclysmRemainingTicksTest,
	"Cataclysm.RemainingDamage.TheTicksLeftAndAShareOfHealthOverThemAreWhatTheEngineWouldDeal")
{
	using Effects = UCataclysmSkillEffects;

	// THE ENGINE'S RULE: the tick due now and every period after it, up to and
	// including the end.
	TestEqual(TEXT("due in 1, every 1, 10 left: 10"), Effects::DamageOverTimeTicksLeft(1.0f, 1.0f, 10.0f), 10);
	TestEqual(TEXT("due in 0.5, every 1, 7.5 left: 8"), Effects::DamageOverTimeTicksLeft(0.5f, 1.0f, 7.5f), 8);
	TestEqual(TEXT("due exactly at the end: 1"), Effects::DamageOverTimeTicksLeft(1.0f, 1.0f, 1.0f), 1);
	TestEqual(TEXT("due after the end: 0"), Effects::DamageOverTimeTicksLeft(1.0f, 1.0f, 0.5f), 0);
	TestEqual(TEXT("no period timer: 0"), Effects::DamageOverTimeTicksLeft(-1.0f, 1.0f, 10.0f), 0);
	TestEqual(TEXT("no period: 0"), Effects::DamageOverTimeTicksLeft(1.0f, 0.0f, 10.0f), 0);
	TestEqual(TEXT("never ends: 0"), Effects::DamageOverTimeTicksLeft(1.0f, 1.0f, -1.0f), 0);

	// A SHARE OF CURRENT HEALTH, RULED 2026-10-01: Health x (1 - (1 - s)^n).
	// 1000 x (1 - 0.99^4) = 39.403990.
	TestEqual(TEXT("1% of 1000 over four ticks: 39.404"),
		Effects::ShareOfHealthOverTicks(0.01f, 1000.0f, 1000.0f, false, 4), 39.40399f, 0.001f);
	// A BOSS IS HELD AT HALF ITS MAXIMUM: 1000 x (1 - 0.5^4) = 937.5, held to 500.
	TestEqual(TEXT("a boss at full health keeps half: 500"),
		Effects::ShareOfHealthOverTicks(0.5f, 1000.0f, 1000.0f, true, 4), 500.0f, 0.001f);
	TestEqual(TEXT("no tick left: nothing"),
		Effects::ShareOfHealthOverTicks(0.01f, 1000.0f, 1000.0f, false, 0), 0.0f, 0.001f);
	return true;
}

CATACLYSM_TEST(FCataclysmRemainingAfterRealTicksTest,
	"Cataclysm.RemainingDamage.AfterRealTicksTheBurstDealsWhatIsLeftAndEndsTheEffect")
{
	using namespace CataclysmRemainingDamageTest;

	// TEN A TICK FOR TEN SECONDS, two and a half seconds in: two ticks have
	// landed and eight are left, so the burst is 80 and the effect is gone. A
	// SECOND ONE, REFRESHED to ten seconds at the same moment, has ten left.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Poison = Tag(TEXT("Keyword.DoT.Poison"));
	if (!TestTrue(TEXT("set-up: the poison tag exists"), Poison.IsValid()))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FFighter Plain(World);
		const FFighter Refreshed(World);
		if (!TestTrue(TEXT("set-up: a poison lands"), TenATick(Wearer.Actor, Plain.Actor, Poison))
			|| !TestTrue(TEXT("set-up: and a second"), TenATick(Wearer.Actor, Refreshed.Actor, Poison)))
		{
			return false;
		}
		CataclysmTestWorld::RunClock(World, 2.5f);
		if (!TestEqual(TEXT("set-up: two ticks of ten have landed"), HealthOf(Plain.Actor), 980.0f, 0.01f))
		{
			return false;
		}
		TestTrue(TEXT("set-up: the second is refreshed to ten seconds"),
			TenATick(Wearer.Actor, Refreshed.Actor, Poison));

		TestEqual(TEXT("eight ticks of ten are left"), RemainingOn(Plain.Actor, Poison), 80.0f, 0.01f);
		TestEqual(TEXT("the refreshed one has ten left, the last at twelve seconds"),
			RemainingOn(Refreshed.Actor, Poison), 100.0f, 0.01f);

		TestEqual(TEXT("the burst reaches one effect"),
			UCataclysmSkillEffects::DealRemainingDamageOverTime(
				Wearer.Actor, Plain.Actor, 100.0f, FGameplayTag(), /*bEndEach=*/true), 1);
		TestEqual(TEXT("and deals its eighty at once"), HealthOf(Plain.Actor), 900.0f, 0.01f);
		TestFalse(TEXT("and the poison is gone"), Running(Plain.Actor, Poison).IsValid());

		CataclysmTestWorld::RunClock(World, 3.0f);
		TestEqual(TEXT("so nothing ticks after it"), HealthOf(Plain.Actor), 900.0f, 0.01f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmRemainingOnlyOwnTest,
	"Cataclysm.RemainingDamage.OnlyTheWearersOwnEffectsAreDealtNotAnotherOrAMinions")
{
	using namespace CataclysmRemainingDamageTest;

	// THREE EFFECTS ON ONE TARGET: the wearer's poison, another character's
	// bleed, and a burn the wearer's minion set, which is the minion's by the
	// ruling of 2026-09-17. Only the poison is dealt and ended.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Poison = Tag(TEXT("Keyword.DoT.Poison"));
	const FGameplayTag Bleed = Tag(TEXT("Keyword.DoT.Bleed"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestTrue(TEXT("set-up: the three tags exist"), Poison.IsValid() && Bleed.IsValid() && Burn.IsValid()))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FFighter Other(World);
		const FFighter Target(World);
		ACataclysmMinion* Minion = ACataclysmMinion::Spawn(Wearer.Actor, FVector(0.0f, 3.0f * M, 0.0f),
			/*Lifetime=*/60.0f, /*bBurns=*/false);
		ON_SCOPE_EXIT { if (IsValid(Minion)) { Minion->Destroy(); } };
		if (!TestNotNull(TEXT("set-up: the wearer's minion"), Minion)
			|| !TestTrue(TEXT("set-up: the wearer's poison"), TenATick(Wearer.Actor, Target.Actor, Poison))
			|| !TestTrue(TEXT("set-up: another's bleed"), TenATick(Other.Actor, Target.Actor, Bleed))
			|| !TestTrue(TEXT("set-up: the minion's burn"), TenATick(Minion, Target.Actor, Burn)))
		{
			return false;
		}
		CataclysmTestWorld::RunClock(World, 2.5f);

		TestEqual(TEXT("one effect is the wearer's"),
			UCataclysmSkillEffects::DealRemainingDamageOverTime(
				Wearer.Actor, Target.Actor, 100.0f, FGameplayTag(), /*bEndEach=*/true), 1);
		TestFalse(TEXT("the wearer's poison is dealt and gone"), Running(Target.Actor, Poison).IsValid());
		TestEqual(TEXT("another's bleed keeps its eight ticks"), RemainingOn(Target.Actor, Bleed), 80.0f, 0.01f);
		TestEqual(TEXT("the minion's burn keeps its eight ticks"), RemainingOn(Target.Actor, Burn), 80.0f, 0.01f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmRemainingShareOfHealthTest,
	"Cataclysm.RemainingDamage.AShareOfCurrentHealthBurstsForWhatItsTicksWouldTake")
{
	using namespace CataclysmRemainingDamageTest;

	// A VOID SPLINTER OF 1% A TICK FOR FOUR SECONDS, a second and a half in: one
	// tick took 10 of 1000, and the three left would take 990 x (1 - 0.99^3) =
	// 29.404, which the burst deals at once.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Splinter = Tag(TEXT("Keyword.DoT.VoidSplinter"));
	if (!TestTrue(TEXT("set-up: the Void Splinter tag exists"), Splinter.IsValid()))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FFighter Target(World);
		if (!TestTrue(TEXT("set-up: a Void Splinter lands"),
				UCataclysmSkillEffects::ApplyShareOfHealthOverTime(
					Wearer.Actor, Target.Actor, 0.01f, 4.0f, Splinter)))
		{
			return false;
		}
		CataclysmTestWorld::RunClock(World, 1.5f);
		if (!TestEqual(TEXT("set-up: one tick took 1% of 1000"), HealthOf(Target.Actor), 990.0f, 0.01f))
		{
			return false;
		}
		TestEqual(TEXT("three ticks left would take 29.404"), RemainingOn(Target.Actor, Splinter), 29.40399f, 0.01f);
		UCataclysmSkillEffects::DealRemainingDamageOverTime(
			Wearer.Actor, Target.Actor, 100.0f, FGameplayTag(), /*bEndEach=*/true);
		TestEqual(TEXT("and the burst takes them at once"), HealthOf(Target.Actor), 990.0f - 29.40399f, 0.01f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmRemainingOnDeathTest,
	"Cataclysm.RemainingDamage.OnDeathEveryOwnEffectWithinFiveMetresBurstsAndEnds")
{
	using namespace CataclysmRemainingDamageTest;

	// "When you die, all active DoTs on nearby enemies instantly deal their
	// remaining damage", BY HAND, THROUGH A REAL DEATH. The enemy three metres
	// away takes its poison's eight remaining ticks at once and the poison ends;
	// the enemy eight metres away keeps its poison running. Each tick's worth is
	// read from the ticks that landed, so the creature's own defences are counted
	// once in each.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Poison = Tag(TEXT("Keyword.DoT.Poison"));
	ACataclysmPlayerCharacter* Player = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Near = SpawnEnemy(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Far = SpawnEnemy(World, FVector(8.0f * M, 0.0f, 0.0f));
	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Player));
	if (!TestNotNull(TEXT("a player"), Player) || !TestNotNull(TEXT("an enemy within five metres"), Near)
		|| !TestNotNull(TEXT("an enemy beyond them"), Far)
		|| !TestNotNull(TEXT("the player's ability system"), AbilitySystem)
		|| !TestTrue(TEXT("set-up: the poison tag exists"), Poison.IsValid())
		|| !TestTrue(TEXT("set-up: the player poisons the near enemy"), TenATick(Player, Near, Poison))
		|| !TestTrue(TEXT("set-up: and the far one"), TenATick(Player, Far, Poison)))
	{
		return false;
	}
	AbilitySystem->SetPoolActions({Remaining(ECataclysmRemainingDamage::Nearby, TEXT("player_death"), 100.0f)});

	CataclysmTestWorld::RunClock(World, 2.5f);
	const float NearBefore = HealthOf(Near);
	const float EachTick = (1000.0f - NearBefore) / 2.0f;
	if (!TestTrue(*FString::Printf(TEXT("set-up: two ticks landed on the near enemy, %.2f each"), EachTick),
			EachTick > 0.0f))
	{
		return false;
	}
	const float FarBefore = HealthOf(Far);

	UCataclysmSkillEffects::ApplyDirectDamage(Near, Player, 100000.0f);
	if (!TestTrue(TEXT("the player died"), UCataclysmSkillEffects::IsDead(Player)))
	{
		return false;
	}
	TestEqual(TEXT("the near enemy takes its eight remaining ticks at once"),
		HealthOf(Near), NearBefore - 8.0f * EachTick, 0.05f);
	TestFalse(TEXT("and its poison ends"), Running(Near, Poison).IsValid());
	TestEqual(TEXT("the far enemy takes nothing at once"), HealthOf(Far), FarBefore, 0.01f);
	TestTrue(TEXT("and its poison runs on"), Running(Far, Poison).IsValid());
	return true;
}

CATACLYSM_TEST(FCataclysmRemainingNecrosisShareTest,
	"Cataclysm.RemainingDamage.ACriticalStrikeDealsAShareOfItsTargetsNecrosisAndEndsNothing")
{
	using namespace CataclysmRemainingDamageTest;

	// "Necrosis effects deal 20%-40% of their remaining damage instantly when you
	// land a critical strike", BY HAND at 30%. The target carries the wearer's
	// Necrosis and the wearer's burn, each with eight ticks of ten left. A
	// critical strike on it deals 30% of the Necrosis's 80, which is 24, and
	// neither effect is ended or reduced: the Necrosis still has 80 left.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Necrosis = Tag(TEXT("Keyword.DoT.Necrosis"));
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!TestTrue(TEXT("set-up: the two tags exist"), Necrosis.IsValid() && Burn.IsValid()))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FFighter Target(World);
		Wearer.AbilitySystem->SetPoolActions({Remaining(ECataclysmRemainingDamage::Target,
			TEXT("critical_strike"), 30.0f, Necrosis)});
		if (!TestTrue(TEXT("set-up: the wearer's Necrosis"), TenATick(Wearer.Actor, Target.Actor, Necrosis))
			|| !TestTrue(TEXT("set-up: and its burn"), TenATick(Wearer.Actor, Target.Actor, Burn)))
		{
			return false;
		}
		CataclysmTestWorld::RunClock(World, 2.5f);
		if (!TestEqual(TEXT("set-up: two ticks of each have landed"), HealthOf(Target.Actor), 960.0f, 0.01f))
		{
			return false;
		}

		Wearer.AbilitySystem->ActOnEvent(FName(TEXT("critical_strike")), nullptr, 0.0f, true, Target.Actor);
		TestEqual(TEXT("the critical strike deals 30% of the Necrosis's 80"),
			HealthOf(Target.Actor), 936.0f, 0.01f);
		TestEqual(TEXT("and the Necrosis still has its 80 left"), RemainingOn(Target.Actor, Necrosis), 80.0f, 0.01f);
		TestEqual(TEXT("and the burn is untouched"), RemainingOn(Target.Actor, Burn), 80.0f, 0.01f);
	}
	return true;
}

CATACLYSM_TEST(FCataclysmWholeDurationTest,
	"Cataclysm.RemainingDamage.ATenSecondEffectOfTenATickDealsAHundredOverItsWholeDuration")
{
	using namespace CataclysmRemainingDamageTest;

	// THE FACT THE REMAINING DAMAGE RESTS ON: an effect lasting an exact multiple
	// of its period delivers its last tick at expiry, so ten seconds of ten a
	// second is a hundred. The engine runs that last tick only when its period
	// timer is due as the duration ends, a float comparison on the timer manager's
	// clock (GameplayEffect.cpp, `CheckDuration`). Measured 2026-10-01 at
	// `RunClock` steps of 0.025, 0.05 and 0.1: ten ticks at each. The first test to
	// measure a whole duration; `CataclysmDebuffTests.cpp` asks for "at least 30" of
	// four ticks so that the edge cannot decide it.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FGameplayTag Poison = Tag(TEXT("Keyword.DoT.Poison"));
	if (!TestTrue(TEXT("set-up: the poison tag exists"), Poison.IsValid()))
	{
		return false;
	}
	{
		const FFighter Wearer(World);
		const FFighter Target(World);
		if (!TestTrue(TEXT("set-up: a poison lands"), TenATick(Wearer.Actor, Target.Actor, Poison)))
		{
			return false;
		}
		CataclysmTestWorld::RunClock(World, 11.0f);
		TestEqual(TEXT("ten ticks of ten, the last at expiry"), HealthOf(Target.Actor), 900.0f, 0.01f);
		TestFalse(TEXT("and the poison has ended"), Running(Target.Actor, Poison).IsValid());
	}
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
