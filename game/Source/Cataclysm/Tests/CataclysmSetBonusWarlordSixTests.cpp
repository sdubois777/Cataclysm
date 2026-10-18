// Copyright Stephen Dubois. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTaunt.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Warlord's Will's six-piece bonus, the mechanism: a taunt status, the Warhound and its blow. Owner's ruling #6 of
 * 2026-10-17 and the coordinating session's rulings of 2026-10-18 (Q1-Q6).
 *
 * "When you take damage, you have a 10% chance to summon a "Warhound" to fight for you for a short duration. The
 * Warhound taunts enemies and deals damage based on your armor"
 *
 * THE ROWS ARE GIVEN AS STAT LINES, IN THE SHAPE THE ENTRY GIVES THE ENCHANTMENT SESSION: `warhound_summon_chance` at a
 * flat 10, `warhound_seconds` at 10, `warhound_percent_of_armor` at 20 and `warhound_taunt_seconds` at 3, each with no
 * tags. The real rows, worn at six pieces with a control at five, are the enchantment session's test.
 *
 * TWO KINDS OF BODY. A Warlord's Will wearer, and every creature a taunt is tested on, is a real enemy character with
 * a team and a brain (`FWarlordCreature`): a minion takes its summoner's team, and a bare actor's minions would count
 * as hostile to their own summoner and to each other (`UCataclysmTeams::AttitudeBetween`), which in a test whose clock
 * runs lets a Warhound strike the wearer it fights for. THE REPLACEMENT TEST USES BARE BODIES (`FWarhoundBody`), as
 * Tyrant's Chains' does, because a summon skill is granted on one, and its clock never runs.
 *
 * NO BRAIN IS LEFT TO THINK WHERE IT COULD STRIKE: a test that runs the clock keeps every creature hostile to another
 * at least 20 m from it, beyond the 15 m every creature here notices from and the 2 m it reaches. A test that asks a
 * brain moves the world's seconds by hand and fires no timer.
 *
 * THE WARHOUND IS MADE FROM THE IMP'S ROW FOR NOW (the stand-in `docs/DECISIONS.md` records), and nothing here asserts
 * the Imp's numbers as its own: its hit is read against its summoner's armour.
 */
namespace CataclysmSetBonusWarlordSixTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	/** Centimetres in a metre. */
	constexpr float M = 100.0f;

	/** The ruled figures: 10%, 10 s, 20% of armour a hit, and a 3 s taunt. */
	constexpr float WarhoundChance = 10.0f;
	constexpr float WarhoundSeconds = 10.0f;
	constexpr float WarhoundArmourPercent = 20.0f;
	constexpr float TauntSeconds = 3.0f;

	/** The wearer's armour in the blow test, so a fifth of it is a round 200. */
	constexpr float WearerArmour = 1000.0f;

	/** How far a float clock may fall from a figure it was built to equal, in seconds: one step of `RunClock`. */
	constexpr float ClockAllowanceSeconds = 0.06f;

	/** Pins `Cataclysm.WarhoundRoll` for the life of the scope. A roll below the chance summons. */
	struct FScopedWarhoundRoll
	{
		explicit FScopedWarhoundRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.WarhoundRoll"));
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		~FScopedWarhoundRoll()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}

		void Set(float Roll) const
		{
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* Variable = nullptr;
		float Previous = -1.0f;
	};

	/** A stat line of flat figures, each with no tags. WHOLESALE, because `SetStatInputs` replaces what was there. */
	TMap<FName, FCataclysmStatInputs> WarhoundFlats(const TMap<FName, float>& Figures)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const TPair<FName, float>& Each : Figures)
		{
			FCataclysmStatModifier Modifier;
			Modifier.Bucket = ECataclysmStatBucket::Flat;
			Modifier.Source = ECataclysmModifierSource::PassiveKeystone;
			Modifier.Value = Each.Value;

			FCataclysmStatInputs Inputs;
			Inputs.Base = 0.0f;
			Inputs.Modifiers.Add(Modifier);
			Stats.Add(Each.Key, Inputs);
		}
		return Stats;
	}

	/** The four rows of the bonus as stat lines. */
	TMap<FName, float> WarlordSixBonus()
	{
		return {
			{FName(ACataclysmMinion::WarhoundChanceStat), WarhoundChance},
			{FName(ACataclysmMinion::WarhoundSecondsStat), WarhoundSeconds},
			{FName(ACataclysmMinion::WarhoundArmourPercentStat), WarhoundArmourPercent},
			{FName(ACataclysmMinion::WarhoundTauntSecondsStat), TauntSeconds}};
	}

	/**
	 * A real creature with a team and a brain: the wearer, its enemies, a boss. Every defence a blow could meet is
	 * nought, so a hit is read whole. THE SETTERS FIRST AND THE ATTRIBUTES AFTER, because a setter re-runs the
	 * creature's starting attributes.
	 */
	struct FWarlordCreature
	{
		FWarlordCreature(UWorld* World, const FVector& Where, ECataclysmTeam Team, int32 RarityStep = 0)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			if (RarityStep > 0)
			{
				Actor->SetRarityStep(RarityStep);
			}
			Actor->SetHealth(100000.0f);
			Actor->SetArmour(0.0f);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent());
			check(AbilitySystem);
			Set(Combat::GetEvasionAttribute(), 0.0f);
			Set(Combat::GetBlockChanceAttribute(), 0.0f);
			Set(Combat::GetCrowdControlResistanceAttribute(), 0.0f);
			Set(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}

		~FWarlordCreature()
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

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(Vital::GetHealthAttribute());
		}

		/** What this creature's brain would attack now. */
		AActor* Chooses() const
		{
			const ACataclysmEnemyController* Brain = Cast<ACataclysmEnemyController>(Actor->GetController());
			return Brain ? Brain->ChooseTarget() : nullptr;
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A bare actor a sphere search can find, holding health and a class resource pool, as Tyrant's Chains' tests use. */
	struct FWarhoundBody
	{
		FWarhoundBody(UWorld* World, const FVector& Where)
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
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			AbilitySystem->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), 100000.0f);
			AbilitySystem->SetNumericAttributeBase(Vital::GetHealthAttribute(), 100000.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
			AbilitySystem->SetNumericAttributeBase(
				UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
		}

		~FWarhoundBody()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** An imp of the authored type standing where the case says, lasting a minute: a taunter, or the blow's control. */
	ACataclysmMinion* ImpOf(AActor* Summoner, const FVector& Where)
	{
		return ACataclysmMinion::Spawn(Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
	}

	/** Write a minion's health to nothing, which is how anything else dies. */
	void Kill(ACataclysmMinion* Minion)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Minion))
		{
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), 0.0f);
		}
	}

	/** How many Warhounds `Summoner` has standing, counted rather than asked of the one-at-most helper. */
	int32 WarhoundsStanding(const AActor* Summoner)
	{
		int32 Count = 0;
		for (AActor* Follower : UCataclysmCommand::ThingsCommandedBy(Summoner))
		{
			const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
			if (IsValid(Minion) && Minion->bIsWarhound)
			{
				++Count;
			}
		}
		return Count;
	}

	/** One landed blow of 1 on `Struck` from `Attacker`, which raises `hit_taken` on it through the vital set. */
	void StrikeTheWearer(AActor* Attacker, AActor* Struck)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Struck, 1.0f);
	}

	/** Destroy whatever is still standing at the end of a case. */
	void Clear(const TArray<ACataclysmMinion*>& Minions)
	{
		for (ACataclysmMinion* Minion : Minions)
		{
			if (IsValid(Minion))
			{
				Minion->Destroy();
			}
		}
	}
}

// EVERY TEST OPENS THE NAMESPACE INSIDE ITS OWN BODY, because this module is built as a unity blob and a `using
// namespace` at file scope reaches the other files concatenated with this one.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixTauntTargetTest,
	"Cataclysm.SetBonus.Warlord6.ATauntedEnemyTargetsItsTaunterAndAfterTheSecondsThePlayerAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q1: for the taunt's seconds the creature targets only its taunter, and afterwards what it would have.
 *
 * THE CONTROL IS READ FIRST: the creature stands 4 m from the player and 8.9 m from the taunter, an imp of the
 * player's, and chooses the player. Taunted for 3 s it chooses the imp, still at 2.9 s; at 3.1 s it chooses the player
 * again. The world's seconds are moved by hand, so no brain thinks and nothing is struck.
 */
bool FCataclysmWarlordSixTauntTargetTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWarlordCreature Player(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FWarlordCreature Enemy(World, FVector(4 * M, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmMinion* Taunter = ImpOf(Player.Actor, FVector(0, 8 * M, 0));
	if (!TestNotNull(TEXT("set-up: an imp of the player's to taunt with"), Taunter))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({Taunter}); };

	TestTrue(TEXT("control: untaunted, the creature chooses the player, the nearest"), Enemy.Chooses() == Player.Actor);

	TestTrue(TEXT("the taunt lands"), UCataclysmTaunt::ApplyTaunt(Taunter, Enemy.Actor, TauntSeconds));
	TestTrue(TEXT("taunted, it chooses its taunter and not the player"), Enemy.Chooses() == Taunter);

	World->TimeSeconds += 2.9f;
	TestTrue(TEXT("still at 2.9 s"), Enemy.Chooses() == Taunter);

	World->TimeSeconds += 0.2f;
	TestTrue(TEXT("and at 3.1 s it chooses the player again"), Enemy.Chooses() == Player.Actor);
	TestNull(TEXT("with no taunt left on it"), UCataclysmTaunt::TaunterOf(Enemy.Actor));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixTauntResistanceTest,
	"Cataclysm.SetBonus.Warlord6.CrowdControlResistanceShortensATaunt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * #6: a taunt is shortened by crowd-control resistance, as a stun is (`HeldSecondsAfterCrowdControlResistance`).
 *
 * THE CONTROL IS READ FIRST: a creature holding no resistance is taunted for the whole 3 s. One holding 50 is taunted
 * for 1.5 s, and one holding 100 is not taunted at all.
 */
bool FCataclysmWarlordSixTauntResistanceTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWarlordCreature Player(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FWarlordCreature Plain(World, FVector(4 * M, 0, 0), ECataclysmTeam::Monsters);
	FWarlordCreature Half(World, FVector(0, 4 * M, 0), ECataclysmTeam::Monsters);
	FWarlordCreature Whole(World, FVector(-4 * M, 0, 0), ECataclysmTeam::Monsters);
	Half.Set(Combat::GetCrowdControlResistanceAttribute(), 50.0f);
	Whole.Set(Combat::GetCrowdControlResistanceAttribute(), 100.0f);
	ACataclysmMinion* Taunter = ImpOf(Player.Actor, FVector(0, -4 * M, 0));
	if (!TestNotNull(TEXT("set-up: an imp of the player's to taunt with"), Taunter))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({Taunter}); };

	UCataclysmTaunt::ApplyTaunt(Taunter, Plain.Actor, TauntSeconds);
	TestEqual(TEXT("control: a creature holding no resistance is taunted for the whole 3 s"),
			  UCataclysmTaunt::SecondsLeftOn(Plain.Actor), TauntSeconds, 0.001f);

	TestTrue(TEXT("one holding 50 crowd-control resistance is taunted"),
			 UCataclysmTaunt::ApplyTaunt(Taunter, Half.Actor, TauntSeconds));
	TestEqual(TEXT("for half the time, 1.5 s"), UCataclysmTaunt::SecondsLeftOn(Half.Actor), TauntSeconds * 0.5f,
			  0.001f);

	TestFalse(TEXT("and one holding 100 is not taunted at all"),
			  UCataclysmTaunt::ApplyTaunt(Taunter, Whole.Actor, TauntSeconds));
	TestNull(TEXT("so nothing taunts it"), UCataclysmTaunt::TaunterOf(Whole.Actor));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixTauntBossTest,
	"Cataclysm.SetBonus.Warlord6.ABossIsNotTauntedAndTheRungBelowIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * #6: bosses are immune, the Boss and Cataclysm Boss rungs, as for a stun.
 *
 * THE RUNG BELOW IS THE CONTROL, READ FIRST: a Herald is taunted and chooses its taunter. A Boss at the same distance
 * is refused, and goes on choosing the player.
 */
bool FCataclysmWarlordSixTauntBossTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWarlordCreature Player(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FWarlordCreature Herald(World, FVector(4 * M, 0, 0), ECataclysmTeam::Monsters,
							ACataclysmEnemyCharacter::FirstBossRarityStep - 1);
	FWarlordCreature Boss(World, FVector(-4 * M, 0, 0), ECataclysmTeam::Monsters,
						  ACataclysmEnemyCharacter::FirstBossRarityStep);
	ACataclysmMinion* Taunter = ImpOf(Player.Actor, FVector(0, 8 * M, 0));
	if (!TestNotNull(TEXT("set-up: an imp of the player's to taunt with"), Taunter)
		|| !TestFalse(TEXT("set-up: the Herald is the rung below a boss"), Herald.Actor->IsBoss())
		|| !TestTrue(TEXT("set-up: the Boss is a boss"), Boss.Actor->IsBoss()))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({Taunter}); };

	TestTrue(TEXT("control: the Herald is taunted"), UCataclysmTaunt::ApplyTaunt(Taunter, Herald.Actor, TauntSeconds));
	TestTrue(TEXT("and chooses its taunter"), Herald.Chooses() == Taunter);

	TestFalse(TEXT("a Boss is not taunted"), UCataclysmTaunt::ApplyTaunt(Taunter, Boss.Actor, TauntSeconds));
	TestTrue(TEXT("and goes on choosing the player"), Boss.Chooses() == Player.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixTaunterGoesTest,
	"Cataclysm.SetBonus.Warlord6.ATauntEndsAtOnceWhenItsTaunterDiesOrGoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q1: "the taunt ends at once if the taunter dies or goes".
 *
 * TWO CREATURES, EACH TAUNTED BY ITS OWN IMP, AND EACH THE OTHER'S CONTROL. With both taunts running, each chooses its
 * taunter. The first imp is killed: its creature chooses the player at once, inside the 3 s, and the second creature
 * still chooses its own imp. The second imp is then removed from the world: its creature chooses the player too.
 */
bool FCataclysmWarlordSixTaunterGoesTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWarlordCreature Player(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FWarlordCreature First(World, FVector(4 * M, 0, 0), ECataclysmTeam::Monsters);
	FWarlordCreature Second(World, FVector(-4 * M, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmMinion* Dies = ImpOf(Player.Actor, FVector(0, 8 * M, 0));
	ACataclysmMinion* Goes = ImpOf(Player.Actor, FVector(0, -8 * M, 0));
	if (!TestNotNull(TEXT("set-up: the imp that will die"), Dies)
		|| !TestNotNull(TEXT("set-up: the imp that will go"), Goes))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({Dies, Goes}); };

	UCataclysmTaunt::ApplyTaunt(Dies, First.Actor, TauntSeconds);
	UCataclysmTaunt::ApplyTaunt(Goes, Second.Actor, TauntSeconds);
	TestTrue(TEXT("control: the first creature chooses its taunter"), First.Chooses() == Dies);
	TestTrue(TEXT("control: the second creature chooses its own"), Second.Chooses() == Goes);

	Kill(Dies);
	if (!TestTrue(TEXT("set-up: the first imp is dead"), UCataclysmSkillEffects::IsDead(Dies)))
	{
		return false;
	}
	TestTrue(TEXT("the first imp's death ends its taunt at once: its creature chooses the player"),
			 First.Chooses() == Player.Actor);
	TestTrue(TEXT("and the second creature, still taunted, still chooses its own imp"), Second.Chooses() == Goes);

	Goes->Destroy();
	TestTrue(TEXT("the second imp gone from the world, its creature chooses the player too"),
			 Second.Chooses() == Player.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixSecondTauntTest,
	"Cataclysm.SetBonus.Warlord6.TheSameTaunterRestartsItsSecondsAndAnotherIsRefusedUntilItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q2: "the same taunter restarts the 3 s; another taunter is refused until it ends".
 *
 * THE CONTROL IS READ FIRST: two seconds after it was taunted the creature has one second left. Its taunter taunts it
 * again: three seconds left. Another imp's taunt is refused and the first keeps it. Once the restarted three seconds
 * have passed, the other imp's taunt lands and the creature chooses that imp.
 */
bool FCataclysmWarlordSixSecondTauntTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWarlordCreature Player(World, FVector::ZeroVector, ECataclysmTeam::Players);
	FWarlordCreature Enemy(World, FVector(4 * M, 0, 0), ECataclysmTeam::Monsters);
	ACataclysmMinion* First = ImpOf(Player.Actor, FVector(0, 8 * M, 0));
	ACataclysmMinion* Other = ImpOf(Player.Actor, FVector(0, -8 * M, 0));
	if (!TestNotNull(TEXT("set-up: the first taunter"), First) || !TestNotNull(TEXT("set-up: another"), Other))
	{
		return false;
	}
	ON_SCOPE_EXIT { Clear({First, Other}); };

	UCataclysmTaunt::ApplyTaunt(First, Enemy.Actor, TauntSeconds);
	World->TimeSeconds += 2.0f;
	TestEqual(TEXT("control: two seconds on, one second is left"), UCataclysmTaunt::SecondsLeftOn(Enemy.Actor), 1.0f,
			  0.001f);

	TestTrue(TEXT("the same taunter taunts it again"), UCataclysmTaunt::ApplyTaunt(First, Enemy.Actor, TauntSeconds));
	TestEqual(TEXT("and its three seconds restart"), UCataclysmTaunt::SecondsLeftOn(Enemy.Actor), TauntSeconds,
			  0.001f);

	TestFalse(TEXT("another taunter is refused"), UCataclysmTaunt::ApplyTaunt(Other, Enemy.Actor, TauntSeconds));
	TestTrue(TEXT("and the first keeps it"), UCataclysmTaunt::TaunterOf(Enemy.Actor) == First);

	World->TimeSeconds += TauntSeconds + 0.1f;
	TestTrue(TEXT("once the restarted taunt has ended, the other's lands"),
			 UCataclysmTaunt::ApplyTaunt(Other, Enemy.Actor, TauntSeconds));
	TestTrue(TEXT("and the creature chooses the other"), Enemy.Chooses() == Other);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixSummonTest,
	"Cataclysm.SetBonus.Warlord6.ABlowTakenSummonsAWarhoundWhenTheRollIsBelowTheChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q3 and Q4: a landed blow taken rolls the row's 10%, and below it summons one Warhound beside the wearer, the
 * wearer's, outside the summon caps, for the row's 10 s.
 *
 * THE ROLL IS PINNED, AFTER THE WORLD IS MADE. THE CONTROL IS READ FIRST: a roll of 10, equal to the chance, summons
 * none. A roll of 9.9 summons one.
 */
bool FCataclysmWarlordSixSummonTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScopedWarhoundRoll Roll(WarhoundChance);

	FWarlordCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Wearer.AbilitySystem->SetStatInputs(WarhoundFlats(WarlordSixBonus()));
	FWarhoundBody Attacker(World, FVector(30 * M, 0, 0));
	ON_SCOPE_EXIT { Clear({ACataclysmMinion::WarhoundOf(Wearer.Actor)}); };

	const float Before = Wearer.Health();
	StrikeTheWearer(Attacker.Actor, Wearer.Actor);
	if (!TestTrue(FString::Printf(TEXT("set-up: the blow landed on the wearer: %.3f"), Before - Wearer.Health()),
				  Before - Wearer.Health() > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: a roll of 10, equal to the 10% chance, summons no Warhound"),
			  WarhoundsStanding(Wearer.Actor), 0);

	Roll.Set(WarhoundChance - 0.1f);
	StrikeTheWearer(Attacker.Actor, Wearer.Actor);
	ACataclysmMinion* Warhound = ACataclysmMinion::WarhoundOf(Wearer.Actor);
	if (!TestNotNull(TEXT("a roll of 9.9 summons a Warhound"), Warhound))
	{
		return false;
	}
	TestEqual(TEXT("one, and only one"), WarhoundsStanding(Wearer.Actor), 1);
	TestTrue(TEXT("it is the wearer's"), Warhound->Summoner.Get() == Wearer.Actor);
	TestEqual(TEXT("it stands 1.5 m from the wearer, where a re-summoned minion appears"),
			  static_cast<float>(FVector::Dist2D(Warhound->GetActorLocation(), Wearer.Actor->GetActorLocation())),
			  UCataclysmSummonSkill::ResummonDistanceCm, 1.0f);
	TestTrue(TEXT("it holds no place under a summon cap"), Warhound->bOutsideSummonCaps);
	TestEqual(TEXT("so no cap count of its type counts it"),
			  UCataclysmCommand::MinionsOfTypeCommandedBy(Wearer.Actor, Warhound->TypeName).Num(), 0);
	TestEqual(TEXT("it is given the row's ten seconds"), Warhound->GetLifeSpan(), WarhoundSeconds,
			  ClockAllowanceSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixRefreshTest,
	"Cataclysm.SetBonus.Warlord6.ASecondSummonRestartsTheWarhoundsTimeKeepsItsHealthAndMakesNoSecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q3: at most one; a new summon restarts the time of the one standing at the full 10 s and keeps its health; it is
 * gone when its time has run.
 *
 * THE CONTROL IS READ FIRST: six seconds after it was summoned the Warhound has four left. A second blow comes up: the
 * same Warhound stands, no second is made, its health is what it was a moment before, and its time is ten seconds
 * again. Six seconds later it still stands, and four and a half after that it is gone. THE CLOCK RUNS: the attacker
 * stands 30 m off, and the wearer and its Warhound are on one team.
 */
bool FCataclysmWarlordSixRefreshTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScopedWarhoundRoll Roll(0.0f);

	FWarlordCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Wearer.AbilitySystem->SetStatInputs(WarhoundFlats(WarlordSixBonus()));
	FWarhoundBody Attacker(World, FVector(30 * M, 0, 0));
	ON_SCOPE_EXIT { Clear({ACataclysmMinion::WarhoundOf(Wearer.Actor)}); };

	StrikeTheWearer(Attacker.Actor, Wearer.Actor);
	ACataclysmMinion* Warhound = ACataclysmMinion::WarhoundOf(Wearer.Actor);
	if (!TestNotNull(TEXT("set-up: the first blow summons a Warhound"), Warhound))
	{
		return false;
	}
	// HURT, so "keeps its health" is not a full pool compared with a full pool.
	UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Warhound);
	Its->SetNumericAttributeBase(Vital::GetHealthAttribute(),
								 Its->GetNumericAttribute(Vital::GetMaxHealthAttribute()) * 0.5f);

	CataclysmTestWorld::RunClock(World, 6.0f);
	TestEqual(TEXT("control: six seconds on it has four left"), Warhound->GetLifeSpan(), WarhoundSeconds - 6.0f,
			  ClockAllowanceSeconds);
	const float HealthBefore = Its->GetNumericAttribute(Vital::GetHealthAttribute());

	StrikeTheWearer(Attacker.Actor, Wearer.Actor);
	TestTrue(TEXT("the second blow leaves the same Warhound standing"),
			 ACataclysmMinion::WarhoundOf(Wearer.Actor) == Warhound);
	TestEqual(TEXT("and makes no second"), WarhoundsStanding(Wearer.Actor), 1);
	TestEqual(TEXT("its health is kept"), Its->GetNumericAttribute(Vital::GetHealthAttribute()), HealthBefore, 0.01f);
	TestEqual(TEXT("its time restarts at the full ten seconds"), Warhound->GetLifeSpan(), WarhoundSeconds,
			  ClockAllowanceSeconds);

	CataclysmTestWorld::RunClock(World, 6.0f);
	TestTrue(TEXT("six seconds later it still stands, two past where its first time ended"), IsValid(Warhound));

	CataclysmTestWorld::RunClock(World, 4.5f);
	TestFalse(TEXT("and it is gone once the restarted ten seconds have run"), IsValid(Warhound));
	TestEqual(TEXT("leaving the wearer none"), WarhoundsStanding(Wearer.Actor), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixWhatRollsTest,
	"Cataclysm.SetBonus.Warlord6.NeitherATickNorAnEvadedBlowNorABlowWithinAQuarterSecondSummons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q4: "10% per blow taken through hit_taken with its 0.25 s trigger cooldown; damage-over-time ticks do not count".
 *
 * THE ROLL IS PINNED AT 0, SO EVERY BLOW THAT ROLLS SUMMONS. `NoteHitTaken` is called as the vital set calls it: a
 * tick summons none, and an evaded blow none. THE CONTROL is a landed blow, which summons one. A fifth of a second
 * later a landed blow does not refresh it, inside the quarter second; a tenth after that, one does.
 */
bool FCataclysmWarlordSixWhatRollsTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FScopedWarhoundRoll Roll(0.0f);

	FWarlordCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Wearer.AbilitySystem->SetStatInputs(WarhoundFlats(WarlordSixBonus()));
	ON_SCOPE_EXIT { Clear({ACataclysmMinion::WarhoundOf(Wearer.Actor)}); };

	Wearer.AbilitySystem->NoteHitTaken(/*bLanded=*/true, /*Attacker=*/nullptr, /*bDamageOverTime=*/true);
	TestEqual(TEXT("a tick of damage over time summons no Warhound"), WarhoundsStanding(Wearer.Actor), 0);

	Wearer.AbilitySystem->NoteHitTaken(/*bLanded=*/false, /*Attacker=*/nullptr, /*bDamageOverTime=*/false);
	TestEqual(TEXT("an evaded blow summons none"), WarhoundsStanding(Wearer.Actor), 0);

	Wearer.AbilitySystem->NoteHitTaken(/*bLanded=*/true, /*Attacker=*/nullptr, /*bDamageOverTime=*/false);
	ACataclysmMinion* Warhound = ACataclysmMinion::WarhoundOf(Wearer.Actor);
	if (!TestNotNull(TEXT("control: a landed blow summons one"), Warhound))
	{
		return false;
	}

	CataclysmTestWorld::RunClock(World, 0.2f);
	Wearer.AbilitySystem->NoteHitTaken(/*bLanded=*/true, /*Attacker=*/nullptr, /*bDamageOverTime=*/false);
	TestEqual(TEXT("a landed blow 0.2 s later, inside the quarter second, does not restart its time"),
			  Warhound->GetLifeSpan(), WarhoundSeconds - 0.2f, ClockAllowanceSeconds);

	CataclysmTestWorld::RunClock(World, 0.1f);
	Wearer.AbilitySystem->NoteHitTaken(/*bLanded=*/true, /*Attacker=*/nullptr, /*bDamageOverTime=*/false);
	TestEqual(TEXT("and one 0.3 s after the summon does"), Warhound->GetLifeSpan(), WarhoundSeconds,
			  ClockAllowanceSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixBlowTest,
	"Cataclysm.SetBonus.Warlord6.AWarhoundsHitDealsAFifthOfItsSummonersArmourAndTauntsTheEnemyStruck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q5 and #6: each hit is 20% of the summoner's armour and places the 3 s taunt on the enemy struck.
 *
 * THE CONTROL IS READ FIRST: an imp of the same wearer, which is no Warhound, swings at one creature. It deals the
 * imp's own blow and taunts nobody. The Warhound swings at another: it deals a fifth of the wearer's 1000 armour, and
 * that creature, which chose the wearer standing nearer, is taunted by the Warhound for 3 s and chooses it.
 */
bool FCataclysmWarlordSixBlowTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FWarlordCreature Wearer(World, FVector::ZeroVector, ECataclysmTeam::Players);
	Wearer.Set(Combat::GetArmorAttribute(), WearerArmour);
	Wearer.AbilitySystem->SetStatInputs(WarhoundFlats(WarlordSixBonus()));
	FWarlordCreature ImpsMark(World, FVector(0, -4 * M, 0), ECataclysmTeam::Monsters);
	FWarlordCreature WarhoundsMark(World, FVector(0, 4 * M, 0), ECataclysmTeam::Monsters);

	ACataclysmMinion* Warhound = ACataclysmMinion::SummonWarhound(Wearer.Actor);
	ACataclysmMinion* Imp = ImpOf(Wearer.Actor, FVector(-3 * M, 0, 0));
	ON_SCOPE_EXIT { Clear({Warhound, Imp}); };
	const float Armour = Wearer.AbilitySystem->StatForSkill(FName(TEXT("armor")), FGameplayTagContainer(),
		Wearer.AbilitySystem->GetNumericAttribute(Combat::GetArmorAttribute()));
	if (!TestNotNull(TEXT("set-up: a Warhound"), Warhound) || !TestNotNull(TEXT("set-up: an imp"), Imp)
		|| !TestEqual(TEXT("set-up: the wearer's armour is 1000"), Armour, WearerArmour, 0.01f)
		|| !TestTrue(TEXT("set-up: the Warhound's mark chooses the wearer, standing nearer than the Warhound"),
					 WarhoundsMark.Chooses() == Wearer.Actor))
	{
		return false;
	}

	const float ImpsMarkBefore = ImpsMark.Health();
	Imp->AttackTarget(ImpsMark.Actor);
	TestEqual(TEXT("control: an imp that is no Warhound deals its own blow"), ImpsMarkBefore - ImpsMark.Health(),
			  Imp->OwnDamagePerHit, 0.5f);
	TestNull(TEXT("control: and taunts nobody"), UCataclysmTaunt::TaunterOf(ImpsMark.Actor));

	const float WarhoundsMarkBefore = WarhoundsMark.Health();
	Warhound->AttackTarget(WarhoundsMark.Actor);
	TestEqual(TEXT("the Warhound's hit deals a fifth of the wearer's armour"),
			  WarhoundsMarkBefore - WarhoundsMark.Health(), WearerArmour * WarhoundArmourPercent / 100.0f, 0.5f);
	TestTrue(TEXT("and taunts the creature struck"), UCataclysmTaunt::TaunterOf(WarhoundsMark.Actor) == Warhound);
	TestEqual(TEXT("for the row's 3 s"), UCataclysmTaunt::SecondsLeftOn(WarhoundsMark.Actor), TauntSeconds, 0.001f);
	TestTrue(TEXT("so it chooses the Warhound and not the wearer"), WarhoundsMark.Chooses() == Warhound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWarlordSixNotReplacedTest,
	"Cataclysm.SetBonus.Warlord6.AWarhoundsDeathIsNotReplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q3: the Warhound is never replaced or re-summoned, as the Tyrant's Minion is not.
 *
 * THE CONTROL IS READ FIRST, A HUNDRED METRES AWAY: a summoner holding a Summon Imp skill and Press-Ganged
 * (`minion_death_replaced_every_seconds`) loses an imp no skill made, and `ReplaceLost` replaces it with one of the
 * skill's imps. That is what would happen to a Warhound, which no skill makes either. Then a Warhound beside the same
 * skill and Press-Ganged is killed: no Warhound stands and the skill holds no replacement.
 */
bool FCataclysmWarlordSixNotReplacedTest::RunTest(const FString&)
{
	using namespace CataclysmSetBonusWarlordSixTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// SUMMON IMP'S OWN FIGURES, as Tyrant's Chains' test grants them for Press-Ganged.
	const auto GrantSummon = [](FWarhoundBody& Who) -> UCataclysmSummonSkill*
	{
		const FGameplayAbilitySpecHandle Handle = Who.AbilitySystem->GiveAbilityInSlot(
			UCataclysmSummonSkill::StaticClass(), ECataclysmAbilitySlot::Special, /*Level=*/100, Who.Actor);
		FGameplayAbilitySpec* Spec = Who.AbilitySystem->FindAbilitySpecFromHandle(Handle);
		UCataclysmSummonSkill* Skill = Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = TEXT("Summon Imp");
			Skill->Params = UCataclysmSkillShapes::ParseParams(
				TEXT("Count=1; MaxActive=3; Duration=20; Radius=3; Minions=Imp:1"));
		}
		return Skill;
	};

	FWarhoundBody Control(World, FVector(0, 100 * M, 0));
	UCataclysmSummonSkill* ControlSkill = GrantSummon(Control);
	Control.AbilitySystem->SetStatInputs(WarhoundFlats({{FName(UCataclysmSummonSkill::ReplacedOnDeathStat), 10.0f}}));

	FWarhoundBody Summoner(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Skill = GrantSummon(Summoner);
	Summoner.AbilitySystem->SetStatInputs(WarhoundFlats({
		{FName(ACataclysmMinion::WarhoundSecondsStat), WarhoundSeconds},
		{FName(UCataclysmSummonSkill::ReplacedOnDeathStat), 10.0f}}));

	if (!TestNotNull(TEXT("set-up: the control's summon skill"), ControlSkill)
		|| !TestNotNull(TEXT("set-up: a summon skill"), Skill))
	{
		return false;
	}

	ACataclysmMinion* Unmade = ImpOf(Control.Actor, FVector(3 * M, 100 * M, 0));
	ACataclysmMinion* Warhound = ACataclysmMinion::SummonWarhound(Summoner.Actor);
	ON_SCOPE_EXIT
	{
		TArray<ACataclysmMinion*> Left = {Unmade, Warhound};
		for (UCataclysmSummonSkill* Each : {ControlSkill, Skill})
		{
			for (const TObjectPtr<ACataclysmMinion>& Made : Each->Minions)
			{
				Left.Add(Made.Get());
			}
		}
		Clear(Left);
	};
	if (!TestNotNull(TEXT("set-up: the control's imp"), Unmade))
	{
		return false;
	}

	Kill(Unmade);
	TestEqual(TEXT("control: Press-Ganged replaces a dying imp that no skill made with one of the skill's"),
			  ControlSkill->LivingMinionCount(), 1);

	if (!TestNotNull(TEXT("set-up: a Warhound"), Warhound)
		|| !TestEqual(TEXT("set-up: the skill holds no imp"), Skill->LivingMinionCount(), 0))
	{
		return false;
	}
	Kill(Warhound);
	TestEqual(TEXT("the Warhound's death leaves no Warhound standing"), WarhoundsStanding(Summoner.Actor), 0);
	TestEqual(TEXT("and Press-Ganged does not replace it"), Skill->LivingMinionCount(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
