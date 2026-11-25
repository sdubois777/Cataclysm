// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Tests/CataclysmTestWorld.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * The Shadow's kill strikes, Lethal Dose, and the Necromancer's Interpose and Guard the Master (the Death trees' slice
 * C6e, 2026-11-25, as ruled): Blade Echo and Thousand Cuts strike the wearer's mark within 10 metres, else the
 * strongest enemy that near, and Thousand Cuts moves beside it first (Q6); a critical strike's Poison or Bleed takes
 * half the critical strike multiplier's increases (Q8); a hit is taken by the nearest minion within 3 metres on a roll
 * (Q9); minions turn on the last enemy that hit their commander and deal more to it.
 */

namespace CataclysmShadowStrikesTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float M = 100.0f;

	/** A character on the player's side at `Where`, with 100 attack damage and no critical strikes. */
	ACataclysmEnemyCharacter* WearerAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
			Made->SetAttackDamage(100.0f);
			Made->SetHealth(10'000.0f);
			if (UAbilitySystemComponent* System = Made->GetAbilitySystemComponent())
			{
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
			}
		}
		return Made;
	}

	/** A creature on the monsters' side at `Where` with `Health`. */
	ACataclysmEnemyCharacter* CreatureAt(UWorld* World, const FVector& Where, float Health = 1000.0f,
										 float AttackDamage = 0.0f)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(Health);
			Made->SetAttackDamage(AttackDamage);
			if (UAbilitySystemComponent* System = Made->GetAbilitySystemComponent())
			{
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
			}
		}
		return Made;
	}

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	/** A kill action of 100% weapon damage: Blade Echo's, or Thousand Cuts' with `bDash`. */
	FCataclysmPoolAction KillStrike(bool bDash)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("kill"));
		Action.Pool = FName(bDash ? UCataclysmAbilitySystemComponent::DashToStrongestAndStrikeAction
								  : UCataclysmAbilitySystemComponent::StrikeStrongestNearAction);
		Action.Percent = 100.0f;
		Action.bStrikeStrongestNear = !bDash;
		Action.bDashToStrongestAndStrike = bDash;
		Action.TriggerKey = FName(TEXT("Passive_kill_strike:kill"));
		return Action;
	}

	/** One line per stat, each holding its rows. */
	void Lines(UCataclysmAbilitySystemComponent* System,
			   const TArray<TPair<const TCHAR*, FCataclysmStatModifier>>& Rows, float Base = 0.0f)
	{
		TMap<FName, FCataclysmStatInputs> Made;
		for (const TPair<const TCHAR*, FCataclysmStatModifier>& Row : Rows)
		{
			FCataclysmStatInputs& Line = Made.FindOrAdd(FName(Row.Key));
			Line.Base = Base;
			Line.Modifiers.Add(Row.Value);
		}
		System->SetStatInputs(MoveTemp(Made));
	}

	FCataclysmStatModifier Row(ECataclysmStatBucket Bucket, float Value,
							   ECataclysmStatCondition Condition = ECataclysmStatCondition::Always)
	{
		FCataclysmStatModifier Made;
		Made.Bucket = Bucket;
		Made.Source = ECataclysmModifierSource::PassiveKeystone;
		Made.Value = Value;
		Made.Condition = Condition;
		return Made;
	}

	/** Pins a console variable for the test and restores it after. */
	struct FPinned
	{
		FPinned(const TCHAR* Name, float Value)
			: Variable(IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (Variable)
			{
				Was = Variable->GetFloat();
				Variable->Set(Value, ECVF_SetByCode);
			}
		}
		~FPinned()
		{
			if (Variable)
			{
				Variable->Set(Was, ECVF_SetByCode);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Was = -1.0f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowStrikesBladeEchoTest,
	"Cataclysm.ShadowStrikes.BladeEchoStrikesTheStrongestWithinTenMetresAndTheMarkFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Blade Echo's action by hand on a kill. The wearer's own mark first: a weak creature 3 metres away is marked while it
 * is the only one in reach, then a stronger one arrives 8 metres away; the mark is sticky, and a kill strikes the
 * marked creature. With the mark lifted, the next kill strikes the strongest within 10 metres, and a stronger one at
 * 20 metres is beyond the reach either way.
 */
bool FCataclysmShadowStrikesBladeEchoTest::RunTest(const FString&)
{
	using namespace CataclysmShadowStrikesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Wearer = WearerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Weak = CreatureAt(World, FVector(3.0f * M, 0, 0), 1000.0f);
	ACataclysmEnemyCharacter* Far = CreatureAt(World, FVector(-20.0f * M, 0, 0), 9000.0f);
	UCataclysmAbilitySystemComponent* System = SystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: a wearer and two creatures"), Weak && Far ? System : nullptr))
	{
		return false;
	}
	System->SetPoolActions({KillStrike(/*bDash=*/false)});
	Lines(System, {{UCataclysmAbilitySystemComponent::ShadowMarksStat, Row(ECataclysmStatBucket::Flat, 1.0f)}});
	System->MaintainShadowMarks();
	const bool bWeakMarked = System->HoldsShadowMarkOn(Weak);
	ACataclysmEnemyCharacter* Strong = CreatureAt(World, FVector(0, 8.0f * M, 0), 5000.0f);
	if (!TestNotNull(TEXT("set-up: a stronger creature arrives"), Strong))
	{
		return false;
	}
	System->MaintainShadowMarks();

	float WeakWas = HealthOf(Weak), StrongWas = HealthOf(Strong), FarWas = HealthOf(Far);
	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, nullptr);
	const float MarkedWeakLost = WeakWas - HealthOf(Weak);
	const float StrongLostWhileMarked = StrongWas - HealthOf(Strong);

	Lines(System, {});
	System->MaintainShadowMarks();
	WeakWas = HealthOf(Weak);
	StrongWas = HealthOf(Strong);
	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, nullptr);
	const float StrongLost = StrongWas - HealthOf(Strong);
	const float WeakLostUnmarked = WeakWas - HealthOf(Weak);

	AddInfo(FString::Printf(TEXT("BLADE ECHO: with the mark on the weak, it lost %.1f and the strong %.1f; unmarked, the "
								 "strong lost %.1f and the weak %.1f; the far lost %.1f in all"),
							MarkedWeakLost, StrongLostWhileMarked, StrongLost, WeakLostUnmarked, FarWas - HealthOf(Far)));
	TestTrue(TEXT("set-up: the weak creature was marked while it was the only one in reach"), bWeakMarked);
	TestTrue(TEXT("the wearer's own mark is struck first"), MarkedWeakLost > 0.0f);
	TestEqual(TEXT("CONTROL: and the stronger one is not"), StrongLostWhileMarked, 0.0f, 0.001f);
	TestTrue(TEXT("with no mark, the strongest within 10 metres is struck"), StrongLost > 0.0f);
	TestEqual(TEXT("CONTROL: and the weaker one is not"), WeakLostUnmarked, 0.0f, 0.001f);
	TestEqual(TEXT("CONTROL: a stronger one at 20 metres is beyond the reach"), FarWas - HealthOf(Far), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowStrikesThousandCutsTest,
	"Cataclysm.ShadowStrikes.ThousandCutsMovesBesideTheStrongestAndStrikesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Thousand Cuts' action by hand on a kill: the wearer is moved from the origin to stand beside a creature 7 metres
 * away, its capsule touching and not inside the creature's, and the creature is struck.
 */
bool FCataclysmShadowStrikesThousandCutsTest::RunTest(const FString&)
{
	using namespace CataclysmShadowStrikesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Wearer = WearerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Target = CreatureAt(World, FVector(7.0f * M, 0, 0), 5000.0f);
	UCataclysmAbilitySystemComponent* System = SystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: a wearer and a creature"), Target ? System : nullptr))
	{
		return false;
	}
	System->SetPoolActions({KillStrike(/*bDash=*/true)});
	const float Was = HealthOf(Target);
	System->ActOnEvent(FName(TEXT("kill")), nullptr, 0.0f, /*bLanded=*/true, nullptr);
	const float Apart = FVector::Dist2D(Wearer->GetActorLocation(), Target->GetActorLocation());
	const float Touching = Wearer->GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ Target->GetCapsuleComponent()->GetScaledCapsuleRadius();

	AddInfo(FString::Printf(TEXT("THOUSAND CUTS: the wearer at %.0f cm, %.1f cm from the creature's centre (radii %.1f); "
								 "the creature lost %.1f"),
							Wearer->GetActorLocation().X, Apart, Touching, Was - HealthOf(Target)));
	TestTrue(TEXT("the wearer is moved toward the creature"), Wearer->GetActorLocation().X > 5.0f * M);
	TestTrue(TEXT("and stands outside it"), Apart >= Touching - 0.5f);
	TestTrue(TEXT("beside it, within one 10 cm step"), Apart <= Touching + 10.5f);
	TestTrue(TEXT("and strikes it"), Was - HealthOf(Target) > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowStrikesLethalDoseTest,
	"Cataclysm.ShadowStrikes.LethalDoseAddsItsShareToTheDamageOverTimeIncreases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Lethal Dose's addition, as ruled (Q8), into the increased bucket: with `dot_damage` at 100% increased, a Poison
 * priced while 50 points are lent is 2.5 / 2 of the same Poison priced with none, 1.25 and not 1.5, because the share
 * sums with the increases rather than multiplying them.
 */
bool FCataclysmShadowStrikesLethalDoseTest::RunTest(const FString&)
{
	using namespace CataclysmShadowStrikesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Wearer = WearerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* System = SystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: a wearer"), System))
	{
		return false;
	}
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetDotFrequencyAttribute(), 100.0f);
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetDotDamageAttribute(), 100.0f);
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetDotDurationAttribute(), 100.0f);
	Lines(System, {{TEXT("dot_damage"), Row(ECataclysmStatBucket::Increased, 100.0f)}}, /*Base=*/100.0f);
	const FGameplayTagContainer Tags;
	const float Plain = UCataclysmSkillEffects::DamageOverTimeNumbers(System, 10.0f, 4.0f, Tags).DamagePerTick;
	float Lent = 0.0f;
	{
		TGuardValue<float> Lends(System->AilmentIncreasePercentLentByCritical, 50.0f);
		Lent = UCataclysmSkillEffects::DamageOverTimeNumbers(System, 10.0f, 4.0f, Tags).DamagePerTick;
	}

	AddInfo(FString::Printf(TEXT("LETHAL DOSE: %.4f a tick plain, %.4f with 50 points lent"), Plain, Lent));
	if (!TestTrue(TEXT("set-up: the Poison is priced"), Plain > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the share sums with the increases: (1 + 1 + 0.5) / (1 + 1)"), Lent / Plain, 1.25f, 0.001f);
	TestEqual(TEXT("and nothing is lent afterwards"), System->AilmentIncreasePercentLentByCritical, 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowStrikesInterposeTest,
	"Cataclysm.ShadowStrikes.InterposeHandsAHitToTheNearestMinionWithinThreeMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Interpose's row by hand at 100%, its roll pinned. A creature's blow on the defender is taken by its Imp 2 metres
 * away: the defender loses nothing and the Imp loses health. CONTROL: with no row, the defender loses health.
 */
bool FCataclysmShadowStrikesInterposeTest::RunTest(const FString&)
{
	using namespace CataclysmShadowStrikesTest;

	FPinned Roll(TEXT("Cataclysm.InterposeRoll"), 0.0f);
	float DefenderLost[2] = {0.0f, 0.0f};
	float ImpLost[2] = {0.0f, 0.0f};
	for (int32 WithRow = 0; WithRow < 2; ++WithRow)
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!TestNotNull(TEXT("world"), World))
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		ACataclysmEnemyCharacter* Defender = WearerAt(World, FVector::ZeroVector);
		ACataclysmEnemyCharacter* Attacker = CreatureAt(World, FVector(3.0f * M, 0, 0), 1000.0f, /*AttackDamage=*/100.0f);
		ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Defender, FVector(0, 2.0f * M, 0), /*Lifetime=*/60.0f,
													   /*bBurns=*/false, TEXT("Imp"));
		UCataclysmAbilitySystemComponent* System = SystemOf(Defender);
		UAbilitySystemComponent* ImpSystem = UCataclysmTargeting::AbilitySystemOf(Imp);
		if (!TestNotNull(TEXT("set-up: a defender, an attacker and an Imp"), Attacker && ImpSystem ? System : nullptr))
		{
			return false;
		}
		ImpSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100'000.0f);
		ImpSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100'000.0f);
		if (WithRow)
		{
			Lines(System, {{UCataclysmAbilitySystemComponent::InterposeChanceStat, Row(ECataclysmStatBucket::Flat, 100.0f)}});
		}
		const float DefenderWas = HealthOf(Defender), ImpWas = HealthOf(Imp);
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotBeEvaded = true;
		UCataclysmSkillEffects::ApplyHit(Attacker, Defender, 100.0f, FGameplayTagContainer(), Delivery);
		DefenderLost[WithRow] = DefenderWas - HealthOf(Defender);
		ImpLost[WithRow] = ImpWas - HealthOf(Imp);
	}

	AddInfo(FString::Printf(TEXT("INTERPOSE: without the row the defender lost %.1f and the Imp %.1f; with it, %.1f and "
								 "%.1f"),
							DefenderLost[0], ImpLost[0], DefenderLost[1], ImpLost[1]));
	TestTrue(TEXT("CONTROL: with no row the defender takes the hit"), DefenderLost[0] > 0.0f);
	TestEqual(TEXT("CONTROL: and the Imp takes nothing"), ImpLost[0], 0.0f, 0.001f);
	TestEqual(TEXT("with the row the defender takes nothing"), DefenderLost[1], 0.0f, 0.001f);
	TestTrue(TEXT("and the Imp takes the hit"), ImpLost[1] > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmShadowStrikesGuardTheMasterTest,
	"Cataclysm.ShadowStrikes.GuardTheMastersMinionsTurnOnTheLastAttackerAndDealMoreToIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Guard the Master's two rows by hand. With the commander's last blow from a creature 4 metres away, its Imp's choice
 * is that creature and its damage against it is 3% more; against another creature it is not. CONTROL: with no flag the
 * Imp is given no such target.
 */
bool FCataclysmShadowStrikesGuardTheMasterTest::RunTest(const FString&)
{
	using namespace CataclysmShadowStrikesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmEnemyCharacter* Master = WearerAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Attacker = CreatureAt(World, FVector(4.0f * M, 0, 0));
	ACataclysmEnemyCharacter* Other = CreatureAt(World, FVector(-4.0f * M, 0, 0));
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Master, FVector(0, 2.0f * M, 0), /*Lifetime=*/60.0f,
												   /*bBurns=*/false, TEXT("Imp"));
	UCataclysmAbilitySystemComponent* System = SystemOf(Master);
	if (!TestNotNull(TEXT("set-up: a master, two creatures and an Imp"), Attacker && Other && Imp ? System : nullptr))
	{
		return false;
	}
	FCataclysmLastBlow Blow;
	Blow.Attacker = Attacker;
	Blow.WorldSeconds = World->GetTimeSeconds();
	System->RecordLastBlow(MoveTemp(Blow));
	const AActor* WithNoFlag = UCataclysmCommand::CommandersLastAttackerFor(Imp);

	Lines(System, {{UCataclysmCommand::TurnOnLastAttackerStat, Row(ECataclysmStatBucket::Flat, 1.0f)},
				   {TEXT("minion_damage"),
					Row(ECataclysmStatBucket::Increased, 3.0f, ECataclysmStatCondition::TargetIsYourLastAttacker)}});
	const AActor* WithTheFlag = UCataclysmCommand::CommandersLastAttackerFor(Imp);
	const float AgainstAttacker =
		UCataclysmCommand::SummonerMultiplierAgainst(Master, TEXT("minion_damage"), Attacker, Imp->TypeTags);
	const float AgainstOther =
		UCataclysmCommand::SummonerMultiplierAgainst(Master, TEXT("minion_damage"), Other, Imp->TypeTags);

	AddInfo(FString::Printf(TEXT("GUARD THE MASTER: target %s with no flag, %s with it; minion damage %.3f against the "
								 "attacker, %.3f against the other"),
							*GetNameSafe(WithNoFlag), *GetNameSafe(WithTheFlag), AgainstAttacker, AgainstOther));
	TestNull(TEXT("CONTROL: with no flag the Imp is given no such target"), WithNoFlag);
	TestTrue(TEXT("with it, the Imp turns on the last enemy that hit its master"), WithTheFlag == Attacker);
	TestEqual(TEXT("and deals 3% more to it"), AgainstAttacker, 1.03f, 0.0001f);
	TestEqual(TEXT("CONTROL: and not to another"), AgainstOther, 1.0f, 0.0001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
