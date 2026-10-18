// Copyright Stephen Dubois. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTaunt.h"
#include "AbilitySystem/CataclysmTauntAura.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Brute's Heart's six-piece bonus, the taunt aura. The owner's ruling #6 of 2026-10-17 (Warlord's Will 6's taunt status
 * "also completes Brute's Heart 6's taunt") and the coordinating session's rulings of 2026-10-18 (Q1-Q4).
 *
 * "When your health falls below 50%, you gain a powerful aura that taunts all nearby enemies and increases your armor
 * and resistances by 50%"
 *
 * THE ROWS ARE GIVEN AS STAT LINES, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION: the four aura stats at a flat
 * 50, 1, 5 and 3, each with no tags. The armour row is already built and is not this file's. The real rows, worn at six
 * pieces with a control at five, are the enchantment session's test.
 *
 * EVERY BODY IS A REAL CREATURE WITH A TEAM AND A BRAIN (`FBruteSixCreature`): the wearer on the players' side, every
 * enemy on the monsters' side, and creatures of one side are not each other's enemies. A minion takes its summoner's
 * team, so the Warhound in the one test that needs it is the wearer's ally. Every defence an enemy could meet a taunt
 * with is nought unless the case says. 100000 health each; the wearer is put below half by writing its health.
 *
 * NO CLOCK RUNS. The aura is driven by calling `UCataclysmTauntAura::Step` with the world's seconds moved by hand, as
 * the regeneration step would call it, so no brain thinks, nothing is struck and no timer fires. Every hostile pair stands
 * at least 3 m apart; the near enemies 3 m from the wearer, inside the 5 m, and the far one 8 m, beyond it.
 */
namespace CataclysmSetBonusBruteSixTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	/** Centimetres in a metre. */
	constexpr float BruteSixMetre = 100.0f;

	/** Every creature's maximum health, and so half of it. */
	constexpr float BruteSixHealth = 100000.0f;

	/** The ruled figures: below 50%, a beat every 1 s, 5 m, a 3 s taunt. */
	constexpr float BruteSixBelowPercent = 50.0f;
	constexpr float BruteSixEverySeconds = 1.0f;
	constexpr float BruteSixRadiusMetres = 5.0f;
	constexpr float BruteSixTauntSeconds = 3.0f;

	/** How far a float clock may fall from a figure it was built to equal, in seconds. */
	constexpr float BruteSixClockAllowance = 0.001f;

	/**
	 * A real creature with a team and a brain: the wearer, its enemies, a boss. THE SETTERS FIRST AND THE ATTRIBUTES
	 * AFTER, because a setter re-runs the creature's starting attributes.
	 */
	struct FBruteSixCreature
	{
		FBruteSixCreature(UWorld* World, const FVector& Where, ECataclysmTeam Team, int32 RarityStep = 0)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			if (RarityStep > 0)
			{
				Actor->SetRarityStep(RarityStep);
			}
			Actor->SetHealth(BruteSixHealth);
			Actor->SetArmour(0.0f);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent());
			check(AbilitySystem);
			Set(Combat::GetEvasionAttribute(), 0.0f);
			Set(Combat::GetBlockChanceAttribute(), 0.0f);
			Set(Combat::GetCrowdControlResistanceAttribute(), 0.0f);
			Set(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}

		~FBruteSixCreature()
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

		/** Write this creature's health as a share of its maximum, in per cent. */
		void HealthAt(float Percent)
		{
			Set(Vital::GetHealthAttribute(), BruteSixHealth * Percent / 100.0f);
		}

		/** The four rows of the aura as flat stat lines with no tags, all at once, since `SetStatInputs` replaces. */
		void WearTheAura()
		{
			const TMap<FName, float> Flats = {
				{FName(UCataclysmTauntAura::BelowHealthPercentStat), BruteSixBelowPercent},
				{FName(UCataclysmTauntAura::EverySecondsStat), BruteSixEverySeconds},
				{FName(UCataclysmTauntAura::RadiusMetresStat), BruteSixRadiusMetres},
				{FName(UCataclysmTauntAura::TauntSecondsStat), BruteSixTauntSeconds}};
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const TPair<FName, float>& Each : Flats)
			{
				FCataclysmStatModifier Modifier;
				Modifier.Bucket = ECataclysmStatBucket::Flat;
				Modifier.Source = ECataclysmModifierSource::Enchantment;
				Modifier.Value = Each.Value;
				FCataclysmStatInputs Inputs;
				Inputs.Base = 0.0f;
				Inputs.Modifiers.Add(Modifier);
				Stats.Add(Each.Key, Inputs);
			}
			AbilitySystem->SetStatInputs(MoveTemp(Stats));
		}

		/** One regeneration step's worth of the aura: how many taunts it landed. */
		int32 Step() const
		{
			return UCataclysmTauntAura::Step(Actor);
		}

		/** Who taunts this creature now, or null. */
		AActor* TauntedBy() const
		{
			return UCataclysmTaunt::TaunterOf(Actor);
		}

		/** The seconds left on the taunt on this creature, or nought. */
		float TauntLeft() const
		{
			return UCataclysmTaunt::SecondsLeftOn(Actor);
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/**
	 * Counts the `crowd_control` events raised on one creature's ability system for the life of the scope, heard from
	 * `OnActionEvent`, which `ActOnEvent` announces before anything else. A taunt that lands raises one on its applier,
	 * ruled 2026-10-18 (B of Warlord's Will 6, Q3 here).
	 */
	struct FBruteSixCrowdControlCount
	{
		explicit FBruteSixCrowdControlCount(const AActor* Who)
			: System(Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Who)))
		{
			if (System)
			{
				Handle = System->OnActionEvent.AddLambda([this](FName Raised)
				{
					Count += Raised == FName(TEXT("crowd_control")) ? 1 : 0;
				});
			}
		}

		~FBruteSixCrowdControlCount()
		{
			if (System)
			{
				System->OnActionEvent.Remove(Handle);
			}
		}

		UCataclysmAbilitySystemComponent* System = nullptr;
		FDelegateHandle Handle;
		int32 Count = 0;
	};

	/** Destroy a minion still standing at the end of a case. */
	void BruteSixClear(ACataclysmMinion* Minion)
	{
		if (IsValid(Minion))
		{
			Minion->Destroy();
		}
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a `using
// namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteSixReachTest,
	"Cataclysm.SetBonus.Brute6.AboveHalfHealthNobodyIsTauntedAndBelowItAnEnemyInReachIsAndOneBeyondIsNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q1 and Q2: below 50%, as the armour row reads it, every enemy within 5 m is taunted by the wearer for 3 s.
 *
 * THE CONTROL IS READ FIRST: at full health a step taunts nobody. At exactly half it still taunts nobody, since the
 * armour row's `health_below` is strictly below. At 49% it taunts the enemy 3 m off, by the wearer, for 3 s, and not
 * the one 8 m off.
 */
bool FCataclysmBruteSixReachTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteSixCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FBruteSixCreature Near(World, FVector(3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters);
	FBruteSixCreature Far(World, FVector(0, 8 * BruteSixMetre, 0), ECataclysmTeam::Monsters);
	Wearer.WearTheAura();

	TestEqual(TEXT("control: at full health a step taunts nobody"), Wearer.Step(), 0);
	TestNull(TEXT("and the enemy 3 m off is not taunted"), Near.TauntedBy());

	Wearer.HealthAt(50.0f);
	TestEqual(TEXT("at exactly half health a step still taunts nobody: strictly below, as the armour row reads it"),
			  Wearer.Step(), 0);

	Wearer.HealthAt(49.0f);
	TestEqual(TEXT("at 49% a step lands one taunt"), Wearer.Step(), 1);
	TestTrue(TEXT("on the enemy 3 m off, taunted by the wearer"), Near.TauntedBy() == Wearer.Actor);
	TestEqual(TEXT("for 3 s"), Near.TauntLeft(), BruteSixTauntSeconds, BruteSixClockAllowance);
	TestNull(TEXT("and the enemy 8 m off, beyond the 5 m, is not taunted"), Far.TauntedBy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteSixBossTest,
	"Cataclysm.SetBonus.Brute6.ABossInReachIsNotTauntedAndTheRungBelowIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q4, as for Warlord's Will 6: bosses are immune, the Boss and Cataclysm Boss rungs.
 *
 * THE RUNG BELOW IS THE CONTROL, READ FIRST: a Herald 3 m off is taunted by the wearer. A Boss 3 m off on the other
 * side is not, so the beat lands one taunt, and raises one `crowd_control` on the wearer.
 */
bool FCataclysmBruteSixBossTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteSixCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FBruteSixCreature Herald(World, FVector(3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters,
							 ACataclysmEnemyCharacter::FirstBossRarityStep - 1);
	FBruteSixCreature Boss(World, FVector(-3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters,
						   ACataclysmEnemyCharacter::FirstBossRarityStep);
	if (!TestFalse(TEXT("set-up: the Herald is the rung below a boss"), Herald.Actor->IsBoss())
		|| !TestTrue(TEXT("set-up: the Boss is a boss"), Boss.Actor->IsBoss()))
	{
		return false;
	}
	Wearer.WearTheAura();
	Wearer.HealthAt(40.0f);
	FBruteSixCrowdControlCount Noted(Wearer.Actor);

	const int32 Landed = Wearer.Step();
	TestTrue(TEXT("control: the Herald is taunted, by the wearer"), Herald.TauntedBy() == Wearer.Actor);
	TestNull(TEXT("and the Boss is not taunted"), Boss.TauntedBy());
	TestEqual(TEXT("so the beat landed one taunt"), Landed, 1);
	TestEqual(TEXT("so one crowd_control is raised on the wearer, and none for the refused Boss"), Noted.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteSixRenewTest,
	"Cataclysm.SetBonus.Brute6.TheTauntIsRenewedEachSecondBelowHalfAndRunsOutOnceAbove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q2: every 1 s while below half the taunt is placed again, and once above half it runs out its 3 s.
 *
 * THE CONTROL IS READ FIRST: the first beat leaves 3 s. Half a second on a step lands nothing and 2.5 s are left; a
 * second after the beat it lands again and 3 s are left. Healed to full, a step a second later lands nothing and the
 * taunt has 2 s left; 3.1 s after the last beat it is gone.
 */
bool FCataclysmBruteSixRenewTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteSixCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FBruteSixCreature Enemy(World, FVector(3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters);
	Wearer.WearTheAura();
	Wearer.HealthAt(40.0f);

	TestEqual(TEXT("control: the first beat lands one taunt"), Wearer.Step(), 1);
	TestEqual(TEXT("which has 3 s"), Enemy.TauntLeft(), BruteSixTauntSeconds, BruteSixClockAllowance);

	World->TimeSeconds += 0.5f;
	TestEqual(TEXT("half a second on, a step lands nothing: the beat is not due"), Wearer.Step(), 0);
	TestEqual(TEXT("and 2.5 s are left"), Enemy.TauntLeft(), 2.5f, BruteSixClockAllowance);

	World->TimeSeconds += 0.5f;
	TestEqual(TEXT("a second after the first beat, a step lands the taunt again"), Wearer.Step(), 1);
	TestEqual(TEXT("and its 3 s are whole again"), Enemy.TauntLeft(), BruteSixTauntSeconds, BruteSixClockAllowance);

	Wearer.HealthAt(100.0f);
	World->TimeSeconds += 1.0f;
	TestEqual(TEXT("healed to full, a step a second later lands nothing"), Wearer.Step(), 0);
	TestEqual(TEXT("and the taunt runs out: 2 s are left"), Enemy.TauntLeft(), 2.0f, BruteSixClockAllowance);
	TestTrue(TEXT("still the wearer's"), Enemy.TauntedBy() == Wearer.Actor);

	World->TimeSeconds += 2.1f;
	TestEqual(TEXT("3.1 s after the last beat a step lands nothing"), Wearer.Step(), 0);
	TestNull(TEXT("and the enemy is no longer taunted"), Enemy.TauntedBy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteSixCrowdControlTest,
	"Cataclysm.SetBonus.Brute6.EachTauntThatLandsRaisesCrowdControlOnTheWearer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q3: every landed taunt, placed or restarted, notes crowd control on the wearer, so `AttackerOf(Wearer)` is the wearer
 * and its `crowd_control` rows answer each one.
 *
 * THE CONTROL IS READ FIRST: above half a step raises nothing and the wearer has applied no crowd control. Below it,
 * with two enemies in reach, a beat raises two on the wearer and starts the clock `seconds_after_crowd_control` reads;
 * the next beat, a second later, two more; a step between beats none. Neither enemy raises one of its own.
 */
bool FCataclysmBruteSixCrowdControlTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteSixCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FBruteSixCreature First(World, FVector(3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters);
	FBruteSixCreature Second(World, FVector(-3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters);
	Wearer.WearTheAura();
	FBruteSixCrowdControlCount OnWearer(Wearer.Actor);
	FBruteSixCrowdControlCount OnFirst(First.Actor);
	FBruteSixCrowdControlCount OnSecond(Second.Actor);
	if (!TestNotNull(TEXT("set-up: the wearer has an ability system to count on"), OnWearer.System))
	{
		return false;
	}

	Wearer.Step();
	TestEqual(TEXT("control: above half a step raises no crowd_control on the wearer"), OnWearer.Count, 0);
	TestTrue(TEXT("control: and the wearer has applied no crowd control"),
			 Wearer.AbilitySystem->SecondsSinceCrowdControlApplied() < 0.0f);

	Wearer.HealthAt(40.0f);
	TestEqual(TEXT("below half a beat lands two taunts"), Wearer.Step(), 2);
	TestEqual(TEXT("and raises two crowd_control on the wearer, one for each taunt placed"), OnWearer.Count, 2);
	TestEqual(TEXT("and starts the clock seconds_after_crowd_control reads"),
			  Wearer.AbilitySystem->SecondsSinceCrowdControlApplied(), 0.0f, BruteSixClockAllowance);

	World->TimeSeconds += 0.5f;
	Wearer.Step();
	TestEqual(TEXT("a step between beats raises none"), OnWearer.Count, 2);

	World->TimeSeconds += 0.5f;
	TestEqual(TEXT("the next beat, a second after the first, restarts both"), Wearer.Step(), 2);
	TestEqual(TEXT("and raises two more, one for each taunt restarted"), OnWearer.Count, 4);
	TestEqual(TEXT("and neither enemy raised crowd_control of its own"), OnFirst.Count + OnSecond.Count, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteSixWarhoundTest,
	"Cataclysm.SetBonus.Brute6.AnEnemyAWarhoundTauntsIsRefusedByTheAuraUntilThatTauntEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q4, as Q2 of Warlord's Will 6: another taunter is refused until the running taunt ends.
 *
 * THE CONTROL IS READ FIRST: a Warhound of the wearer's taunts the enemy 3 m off, and the taunt is the Warhound's own
 * crowd control and not the wearer's. A beat of the wearer's aura then lands nothing: the enemy stays the Warhound's
 * and the wearer raises no `crowd_control`. 3.1 s later the Warhound's taunt has ended and the next beat lands on it.
 */
bool FCataclysmBruteSixWarhoundTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteSixCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FBruteSixCreature Enemy(World, FVector(3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmMinion* Warhound = ACataclysmMinion::Spawn(Wearer.Actor, FVector(0, -3 * BruteSixMetre, 0),
														 /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: a Warhound of the wearer's"), Warhound))
	{
		return false;
	}
	ON_SCOPE_EXIT { BruteSixClear(Warhound); };
	Warhound->bIsWarhound = true;
	Wearer.WearTheAura();
	Wearer.HealthAt(40.0f);
	FBruteSixCrowdControlCount OnWearer(Wearer.Actor);

	TestTrue(TEXT("control: the Warhound's taunt lands"),
			 UCataclysmTaunt::ApplyTaunt(Warhound, Enemy.Actor, BruteSixTauntSeconds));
	TestEqual(TEXT("control: and it is the Warhound's crowd control, not the wearer's"), OnWearer.Count, 0);

	TestEqual(TEXT("the aura's beat lands nothing on an enemy the Warhound taunts"), Wearer.Step(), 0);
	TestTrue(TEXT("which stays the Warhound's"), Enemy.TauntedBy() == Warhound);
	TestEqual(TEXT("and the refused taunt raised no crowd_control on the wearer"), OnWearer.Count, 0);

	World->TimeSeconds += BruteSixTauntSeconds + 0.1f;
	TestEqual(TEXT("once the Warhound's taunt has ended, the next beat lands"), Wearer.Step(), 1);
	TestTrue(TEXT("and the enemy is the wearer's"), Enemy.TauntedBy() == Wearer.Actor);
	TestEqual(TEXT("with one crowd_control on the wearer"), OnWearer.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBruteSixResistanceTest,
	"Cataclysm.SetBonus.Brute6.CrowdControlResistanceShortensTheAurasTaunt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q4, as for Warlord's Will 6: crowd-control resistance shortens the taunt, and at 100 nothing is left.
 *
 * THE CONTROL IS READ FIRST: an enemy holding no resistance is taunted for the whole 3 s by the same beat that taunts
 * one holding 50 for 1.5 s and leaves one holding 100 untaunted. So the beat lands two.
 */
bool FCataclysmBruteSixResistanceTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusBruteSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FBruteSixCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FBruteSixCreature Plain(World, FVector(3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters);
	FBruteSixCreature Half(World, FVector(-3 * BruteSixMetre, 0, 0), ECataclysmTeam::Monsters);
	FBruteSixCreature Whole(World, FVector(0, 3 * BruteSixMetre, 0), ECataclysmTeam::Monsters);
	Half.Set(Combat::GetCrowdControlResistanceAttribute(), 50.0f);
	Whole.Set(Combat::GetCrowdControlResistanceAttribute(), 100.0f);
	Wearer.WearTheAura();
	Wearer.HealthAt(40.0f);

	const int32 Landed = Wearer.Step();
	TestEqual(TEXT("control: an enemy holding no resistance is taunted for the whole 3 s"), Plain.TauntLeft(),
			  BruteSixTauntSeconds, BruteSixClockAllowance);
	TestEqual(TEXT("one holding 50 crowd-control resistance for half of it, 1.5 s"), Half.TauntLeft(),
			  BruteSixTauntSeconds * 0.5f, BruteSixClockAllowance);
	TestNull(TEXT("and one holding 100 is not taunted at all"), Whole.TauntedBy());
	TestEqual(TEXT("so the beat landed two taunts"), Landed, 2);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
