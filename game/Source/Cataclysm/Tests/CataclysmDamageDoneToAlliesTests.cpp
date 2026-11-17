// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllyDamageShare.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * "You take 5%-15% damage done to nearby allies", `Negative_You_take_5_15_damage_done_to_nearby_allies` in
 * `game/Data/EnchantmentsNegative.csv`. Ruled 2026-10-15 by the coordinating session, R1 to R8 in the entry of that
 * date in `docs/DECISIONS.md`. The mechanism is `UCataclysmAllyDamageShare`, called from the damage branch of
 * `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`.
 *
 * THE ROW IS GIVEN BY HAND, as the enchantment would give it: no row is authored. Every wearer holds
 * `damage_done_to_nearby_allies_taken_percent` at a flat 10 from an enchantment, with no tags.
 *
 * THE CHARACTERS ARE REAL: player pawns with their ability system on a player state, as the game wires them, so the
 * share is sent to the pawn and taken by the state's attribute sets; imps summoned by a player with
 * `ACataclysmMinion::Spawn`; creatures of the monsters' side; thralls taken with `UCataclysmCommand::Subjugate`. The
 * players are never refreshed, so they hold no class line, no armour and no resistance unless a test gives them one.
 * Every blow is sent with `UCataclysmSkillEffects::ApplyDirectDamage`, which needs no reach; the creature that strikes
 * stands well away from everybody so that it is never one of the characters searched.
 *
 * NO ROLL IS LEFT TO CHANCE. `CataclysmTestWorld::MakeWorldThatHasBegunPlay` pins the critical strike roll to 100,
 * which never strikes; nobody here has an evasion or a block chance, and the share cannot be evaded.
 *
 * FIGURES ARE READ AS WHAT EACH CHARACTER LOST, against what its own health was when the test set it, and a share as
 * a tenth of what its ally lost in the same blow.
 */
namespace CataclysmDamageDoneToAlliesTest
{
	using Vital = UCataclysmVitalAttributeSet;

	constexpr float M = 100.0f;
	constexpr float Pool = 1000.0f;
	constexpr float Percent = 10.0f;

	UCataclysmAbilitySystemComponent* SystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : -1.0f;
	}

	/** Maximum then current, so the current value is not held under an old maximum. */
	void SetHealth(const AActor* Actor, float Maximum, float Current)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), Maximum);
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), Current);
		}
	}

	void SetAttribute(const AActor* Actor, const FGameplayAttribute& Attribute, float Value)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}
	}

	/** A player pawn with its ability system on a player state, at a thousand health. */
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
		SetHealth(Actor, Pool, Pool);
		return Actor;
	}

	/** A creature of the monsters' side, too healthy for any test to kill. */
	ACataclysmEnemyCharacter* SpawnCreature(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Actor =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Actor)
		{
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			SetHealth(Actor, 100000.0f, 100000.0f);
		}
		return Actor;
	}

	/** An imp of `Summoner`'s, at a thousand health of a thousand. */
	ACataclysmMinion* SpawnImp(AActor* Summoner, const FVector& Where)
	{
		ACataclysmMinion* Imp =
			ACataclysmMinion::Spawn(Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false);
		if (Imp)
		{
			SetHealth(Imp, Pool, Pool);
		}
		return Imp;
	}

	/** Give this character flat figures from an enchantment, as worn rows would, replacing any it held. */
	void Hold(const AActor* Actor, const TMap<FName, float>& Stats)
	{
		UCataclysmAbilitySystemComponent* System = SystemOf(Actor);
		if (!System)
		{
			return;
		}
		TMap<FName, FCataclysmStatInputs> Inputs;
		for (const TPair<FName, float>& Stat : Stats)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::Enchantment;
			Flat.Value = Stat.Value;
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(Stat.Key);
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		}
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** Wear the row at `Percent`. */
	void Wear(const AActor* Actor)
	{
		Hold(Actor, {{FName(UCataclysmAllyDamageShare::PercentStat), Percent}});
	}

	void Strike(AActor* From, AActor* To, float Damage)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(From, To, Damage);
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
}

#define CATACLYSM_ALLIES_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesMinionTest,
	"Cataclysm.DamageDoneToAllies.AMinionStruckWithinFiveMetresGivesItsWearerThePercentOfWhatReachedItsHealth")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// A creature strikes an imp 2 m from its summoner, who wears the row; a player without the row stands 3 m from the
	// imp. A second imp, of the plain player's, stands 100 m away and is struck the same: the control for what the
	// blow takes from an imp nobody shares with.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Plain = SpawnPlayer(World, FVector(2.0f * M, 3.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a plain player"), Plain)
		|| !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Imp = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* FarImp = SpawnImp(Plain, FVector(100.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the wearer's imp"), Imp) || !TestNotNull(TEXT("set-up: a far imp"), FarImp))
	{
		return false;
	}
	Wear(Wearer);

	Strike(Creature, FarImp, 300.0f);
	const float FarImpLost = Pool - HealthOf(FarImp);
	if (!TestTrue(*FString::Printf(TEXT("control: the blow takes health from an imp nobody shares with: %.2f"),
			FarImpLost), FarImpLost > 0.0f))
	{
		return false;
	}

	Strike(Creature, Imp, 300.0f);
	const float ImpLost = Pool - HealthOf(Imp);
	TestEqual(TEXT("the imp beside the wearer takes the whole of the same blow: the share is in addition"),
		ImpLost, FarImpLost, 0.01f);
	TestEqual(TEXT("the player without the row, 3 m from the imp, loses nothing"), HealthOf(Plain), Pool, 0.01f);
	TestEqual(TEXT("the wearer, 2 m from it, loses a tenth of what reached the imp's health"),
		Pool - HealthOf(Wearer), ImpLost * Percent / 100.0f, 0.01f);
	return true;
}

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesReachTest,
	"Cataclysm.DamageDoneToAllies.AnAllyBeyondFiveMetresGivesNone")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// Two wearers, one 3 m from the imp and one 8 m from it, on the same side. The near one is the control.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Near = SpawnPlayer(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmPlayerCharacter* Far = SpawnPlayer(World, FVector(-8.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a near wearer"), Near) || !TestNotNull(TEXT("set-up: a far wearer"), Far)
		|| !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Imp = SpawnImp(Near, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: an imp"), Imp))
	{
		return false;
	}
	Wear(Near);
	Wear(Far);
	if (!TestTrue(TEXT("set-up: the far wearer is on the imp's side"), UCataclysmTargeting::IsFriendlyTo(Far, Imp)))
	{
		return false;
	}

	Strike(Creature, Imp, 300.0f);
	const float ImpLost = Pool - HealthOf(Imp);
	if (!TestTrue(TEXT("set-up: the blow reached the imp's health"), ImpLost > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: the wearer 3 m away takes a tenth"), Pool - HealthOf(Near),
		ImpLost * Percent / 100.0f, 0.01f);
	TestEqual(TEXT("the wearer 8 m away takes nothing"), HealthOf(Far), Pool, 0.01f);
	return true;
}

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesEnemyTest,
	"Cataclysm.DamageDoneToAllies.AnEnemyStruckBesideTheWearerGivesNone")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// A creature 2 m from the wearer is struck by the wearer and then by another creature; neither blow gives the
	// wearer anything. The control first: the wearer's imp, 2 m away on the other side, gives a share.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Beside = SpawnCreature(World, FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature beside it"), Beside)
		|| !TestNotNull(TEXT("set-up: a creature far off"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Imp = SpawnImp(Wearer, FVector(-2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an imp"), Imp))
	{
		return false;
	}
	Wear(Wearer);

	Strike(Creature, Imp, 300.0f);
	const float ImpLost = Pool - HealthOf(Imp);
	const float AfterTheImp = HealthOf(Wearer);
	if (!TestEqual(TEXT("control: the wearer takes a tenth of its imp's blow"), Pool - AfterTheImp,
			ImpLost * Percent / 100.0f, 0.01f)
		|| !TestTrue(TEXT("set-up: and the share was more than nothing"), AfterTheImp < Pool))
	{
		return false;
	}

	const float BesideBefore = HealthOf(Beside);
	Strike(Wearer, Beside, 300.0f);
	const float BesideAfterTheWearer = HealthOf(Beside);
	Strike(Creature, Beside, 300.0f);
	if (!TestTrue(TEXT("set-up: both blows took health from the creature beside the wearer"),
			BesideAfterTheWearer < BesideBefore && HealthOf(Beside) < BesideAfterTheWearer))
	{
		return false;
	}
	TestEqual(TEXT("and neither gives the wearer anything: a creature is no ally"), HealthOf(Wearer), AfterTheImp,
		0.01f);
	return true;
}

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesDefencesTest,
	"Cataclysm.DamageDoneToAllies.TheWearersArmourAndResistanceToTheBlowsTypeReduceTheShare")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// Three wearers 3 m from one imp: a plain one, the control; one with 800 armour; one with 50 Demonic resistance.
	// The creature's blow is Demonic, which the share carries.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Plain = SpawnPlayer(World, FVector(3.0f * M, 0.0f, 0.0f));
	ACataclysmPlayerCharacter* Armoured = SpawnPlayer(World, FVector(-3.0f * M, 0.0f, 0.0f));
	ACataclysmPlayerCharacter* Resistant = SpawnPlayer(World, FVector(0.0f, 3.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a plain wearer"), Plain) || !TestNotNull(TEXT("set-up: an armoured one"), Armoured)
		|| !TestNotNull(TEXT("set-up: a resistant one"), Resistant) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Imp = SpawnImp(Plain, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: an imp"), Imp)
		|| !TestTrue(TEXT("set-up: the creature's blows are Demonic"), Creature->DamageType == FName(TEXT("Demonic"))))
	{
		return false;
	}
	Wear(Plain);
	Wear(Armoured);
	Wear(Resistant);
	constexpr float Armour = 800.0f;
	SetAttribute(Armoured, UCataclysmCombatAttributeSet::GetArmorAttribute(), Armour);
	SetAttribute(Resistant, UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(), 50.0f);

	Strike(Creature, Imp, 500.0f);
	const float ImpLost = Pool - HealthOf(Imp);
	const float PlainLost = Pool - HealthOf(Plain);
	const float ArmouredLost = Pool - HealthOf(Armoured);
	const float ResistantLost = Pool - HealthOf(Resistant);
	if (!TestEqual(TEXT("control: the plain wearer takes a tenth of what reached the imp"), PlainLost,
			ImpLost * Percent / 100.0f, 0.01f)
		|| !TestTrue(TEXT("set-up: and that is more than nothing"), PlainLost > 0.0f))
	{
		return false;
	}
	const float ArmourKeeps =
		1.0f - UCataclysmDamageCalculation::ArmorReduction(Armour, ACataclysmGameMode::DifficultyTierIn(World)) / 100.0f;
	TestTrue(*FString::Printf(TEXT("the armoured wearer takes less: %.2f against %.2f"), ArmouredLost, PlainLost),
		ArmouredLost > 0.0f && ArmouredLost < PlainLost);
	TestEqual(TEXT("by what its armour removes from a blow"), ArmouredLost, PlainLost * ArmourKeeps, 0.01f);
	TestEqual(TEXT("and the wearer resisting half of Demonic damage takes half"), ResistantLost, PlainLost * 0.5f,
		0.01f);
	return true;
}

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesKillsTest,
	"Cataclysm.DamageDoneToAllies.TheShareCanKillTheWearerAndItsDeathNamesWhoStruckTheAlly")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// A wearer left 20 health stands 2 m from its imp; a blow of 500 on the imp sends a share of 50.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: combat events"), Events) || !TestNotNull(TEXT("set-up: a wearer"), Wearer)
		|| !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Imp = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an imp"), Imp))
	{
		return false;
	}
	Wear(Wearer);
	SetHealth(Wearer, Pool, 20.0f);

	bool bHeard = false;
	const AActor* Killer = nullptr;
	const FDelegateHandle Handle = Events->OnDeath.AddLambda(
		[&bHeard, &Killer, Wearer](const FCataclysmDeathNotice& Notice)
		{
			if (Notice.Victim == Wearer)
			{
				bHeard = true;
				Killer = Notice.Killer;
			}
		});
	ON_SCOPE_EXIT { Events->OnDeath.Remove(Handle); };

	if (!TestFalse(TEXT("control: the wearer is alive before the blow"), UCataclysmSkillEffects::IsDead(Wearer)))
	{
		return false;
	}
	Strike(Creature, Imp, 500.0f);
	if (!TestTrue(TEXT("set-up: the blow took more than 200 from the imp, so the share is above 20"),
			Pool - HealthOf(Imp) > 200.0f))
	{
		return false;
	}
	TestTrue(TEXT("the share kills the wearer"), UCataclysmSkillEffects::IsDead(Wearer));
	TestTrue(TEXT("its death is announced"), bHeard);
	TestTrue(TEXT("and names the creature that struck the imp as the killer"), Killer == Creature);
	return true;
}

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesRaisesNothingTest,
	"Cataclysm.DamageDoneToAllies.TheShareRaisesNoHitTakenNoFervourAndNoRetaliation")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// A wearer that fills Fervour from health lost to damage, at one point per 1% of its maximum, and retaliates for
	// 50. The control is a blow the creature strikes at the wearer itself: it raises hit_taken once, fills Fervour and
	// is answered. Then the creature's blow on the imp sends a share that lands and does none of the three.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	UCataclysmAbilitySystemComponent* WearerSystem = SystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature"), Creature)
		|| !TestNotNull(TEXT("set-up: the wearer's ability system"), WearerSystem))
	{
		return false;
	}
	ACataclysmMinion* Imp = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an imp"), Imp))
	{
		return false;
	}
	Wear(Wearer);
	SetAttribute(Wearer, UCataclysmClassResourceAttributeSet::GetFervourFromDamageAttribute(), 1.0f);
	SetAttribute(Wearer, UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 1000.0f);
	SetAttribute(Wearer, UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
	SetAttribute(Wearer, UCataclysmCombatAttributeSet::GetRetaliationAttribute(), 50.0f);
	const auto FervourOf = [WearerSystem]()
	{
		return WearerSystem->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
	};

	FEventCount HitsTaken(WearerSystem, TEXT("hit_taken"));
	const float CreatureBefore = HealthOf(Creature);
	Strike(Creature, Wearer, 100.0f);
	const float FervourAfterTheBlow = FervourOf();
	const float CreatureAfterTheBlow = HealthOf(Creature);
	const float WearerAfterTheBlow = HealthOf(Wearer);
	if (!TestEqual(TEXT("control: a blow struck at the wearer raises hit_taken once"), HitsTaken.Count, 1)
		|| !TestTrue(*FString::Printf(TEXT("control: and fills Fervour: %.2f"), FervourAfterTheBlow),
			FervourAfterTheBlow > 0.0f)
		|| !TestTrue(TEXT("control: and is answered by the wearer's retaliation"), CreatureAfterTheBlow < CreatureBefore))
	{
		return false;
	}

	Strike(Creature, Imp, 500.0f);
	const float ImpLost = Pool - HealthOf(Imp);
	if (!TestEqual(TEXT("set-up: the share lands: the wearer loses a tenth of what reached the imp"),
			WearerAfterTheBlow - HealthOf(Wearer), ImpLost * Percent / 100.0f, 0.01f)
		|| !TestTrue(TEXT("set-up: and that is more than nothing"), HealthOf(Wearer) < WearerAfterTheBlow))
	{
		return false;
	}
	TestEqual(TEXT("the share raises no hit_taken"), HitsTaken.Count, 1);
	TestEqual(TEXT("fills no Fervour"), FervourOf(), FervourAfterTheBlow, 0.01f);
	TestEqual(TEXT("and is not answered by retaliation"), HealthOf(Creature), CreatureAfterTheBlow, 0.01f);
	return true;
}

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesNoLoopTest,
	"Cataclysm.DamageDoneToAllies.TwoWearersTakeOneShareEachOfAMinionsBlowAndNoneOfEachOthers")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// Two wearers 3 m apart, each within 5 m of one imp. A blow on the imp gives each one share and no more: had a
	// share been passed on, each would have taken a tenth of the other's on top. Then a blow struck at the first
	// wearer gives the second nothing, because a wearer struck shares nothing.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* First = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Second = SpawnPlayer(World, FVector(0.0f, 3.0f * M, 0.0f));
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a first wearer"), First) || !TestNotNull(TEXT("set-up: a second"), Second)
		|| !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Imp = SpawnImp(First, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an imp"), Imp)
		|| !TestTrue(TEXT("set-up: the two wearers are on one side"), UCataclysmTargeting::IsFriendlyTo(Second, First)))
	{
		return false;
	}
	Wear(First);
	Wear(Second);

	Strike(Creature, Imp, 300.0f);
	const float Share = (Pool - HealthOf(Imp)) * Percent / 100.0f;
	if (!TestTrue(TEXT("set-up: the blow reached the imp's health"), Share > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the first wearer takes one share of the imp's blow"), Pool - HealthOf(First), Share, 0.01f);
	TestEqual(TEXT("and so does the second, and neither takes any of the other's"), Pool - HealthOf(Second), Share,
		0.01f);

	const float FirstBefore = HealthOf(First);
	const float SecondBefore = HealthOf(Second);
	Strike(Creature, First, 100.0f);
	if (!TestTrue(TEXT("set-up: the blow struck at the first wearer took health from it"),
			HealthOf(First) < FirstBefore))
	{
		return false;
	}
	TestEqual(TEXT("and gives the second wearer 3 m away nothing"), HealthOf(Second), SecondBefore, 0.01f);
	return true;
}

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesOverkillTest,
	"Cataclysm.DamageDoneToAllies.OverkillIsNotShared")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// An imp left 40 health is struck for 500. The wearer takes a tenth of the 40 the imp had, not of the 500.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Imp = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: an imp"), Imp))
	{
		return false;
	}
	Wear(Wearer);
	constexpr float Left = 40.0f;
	SetHealth(Imp, Pool, Left);
	if (!TestEqual(TEXT("control: the imp holds 40"), HealthOf(Imp), Left, 0.01f))
	{
		return false;
	}

	Strike(Creature, Imp, 500.0f);
	TestTrue(TEXT("the blow empties the imp"), HealthOf(Imp) <= 0.0f);
	TestEqual(TEXT("and the wearer takes a tenth of the 40 the imp held, not of the 500"), Pool - HealthOf(Wearer),
		Left * Percent / 100.0f, 0.01f);
	return true;
}

CATACLYSM_ALLIES_TEST(FCataclysmDamageDoneToAlliesDirectLossTest,
	"Cataclysm.DamageDoneToAllies.ASacrificialBondShareAndAnExecuteGiveNoShare")
{
	using namespace CataclysmDamageDoneToAlliesTest;

	// THE BOND: two thralls of the wearer's. The bonded one stands 9 m from the wearer, beyond its 5 m; the other 4 m
	// from the wearer and 5 m from the bonded one, inside the bond's 6 m. A blow on the bonded thrall is halved, and
	// the other thrall pays its half as a direct loss: the wearer takes nothing of either. The control first: the same
	// blow struck at the other thrall itself gives the wearer a share.
	//
	// THE EXECUTE: the wearer's imp, struck once, is then executed by the creature that struck it, through
	// `ExecuteIfLeftBelow` as a striking row's request is acted on. The wearer takes its share of the blow and nothing
	// of the execute.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Bonded = SpawnCreature(World, FVector(9.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Other = SpawnCreature(World, FVector(4.0f * M, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World, FVector(0.0f, -50.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature to bond"), Bonded)
		|| !TestNotNull(TEXT("set-up: a second creature to take"), Other)
		|| !TestNotNull(TEXT("set-up: a creature that strikes"), Creature))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the wearer takes the first thrall"), UCataclysmCommand::Subjugate(Wearer, Bonded))
		|| !TestTrue(TEXT("set-up: and the second"), UCataclysmCommand::Subjugate(Wearer, Other)))
	{
		return false;
	}
	SetHealth(Bonded, Pool, Pool);
	SetHealth(Other, Pool, Pool);
	Wear(Wearer);

	Strike(Creature, Other, 100.0f);
	const float OtherLost = Pool - HealthOf(Other);
	const float AfterTheControl = HealthOf(Wearer);
	if (!TestTrue(TEXT("set-up: the blow reached the thrall's health"), OtherLost > 0.0f)
		|| !TestEqual(TEXT("control: a thrall struck 4 m from the wearer gives it a tenth"), Pool - AfterTheControl,
			OtherLost * Percent / 100.0f, 0.01f))
	{
		return false;
	}

	Bonded->ModifierRows.Add(FName(UCataclysmEnemyModifiers::SacrificialBondRow));
	const float OtherBefore = HealthOf(Other);
	Strike(Creature, Bonded, 200.0f);
	if (!TestTrue(TEXT("set-up: the bonded thrall kept part of the blow"), HealthOf(Bonded) < Pool)
		|| !TestTrue(TEXT("set-up: and the other thrall paid its share as a direct loss"),
			HealthOf(Other) < OtherBefore))
	{
		return false;
	}
	TestEqual(TEXT("the wearer takes nothing of the bond's share, nor of the bonded thrall beyond its 5 m"),
		HealthOf(Wearer), AfterTheControl, 0.01f);

	ACataclysmMinion* Imp = SpawnImp(Wearer, FVector(0.0f, 3.0f * M, 0.0f));
	UCataclysmAbilitySystemComponent* ImpSystem = SystemOf(Imp);
	if (!TestNotNull(TEXT("set-up: an imp"), Imp) || !TestNotNull(TEXT("set-up: its ability system"), ImpSystem))
	{
		return false;
	}
	Strike(Creature, Imp, 100.0f);
	const float ImpLost = Pool - HealthOf(Imp);
	const float AfterTheImpsBlow = HealthOf(Wearer);
	if (!TestEqual(TEXT("set-up: the imp's blow gives the wearer a tenth"), AfterTheControl - AfterTheImpsBlow,
			ImpLost * Percent / 100.0f, 0.01f))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent::FExecuteAsked Asked;
	Asked.By = Creature;
	Asked.BelowPercent = 100.0f;
	if (!TestTrue(TEXT("set-up: the creature executes the imp"), ImpSystem->ExecuteIfLeftBelow(Asked)))
	{
		return false;
	}
	TestEqual(TEXT("and the wearer takes nothing of the execute"), HealthOf(Wearer), AfterTheImpsBlow, 0.01f);
	return true;
}

#undef CATACLYSM_ALLIES_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
