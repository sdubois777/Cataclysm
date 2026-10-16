// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAreaShareOnAllies.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Player/CataclysmGameMode.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * "Your AOE skills deal 50%-75% of their damage to nearby allies as well as enemies",
 * `Negative_Your_AOE_skills_deal_50_75_of_their_damage_to` in `game/Data/EnchantmentsNegative.csv`. Ruled 2026-10-15 by
 * the coordinating session, R1 to R10 in the entry of that date in `docs/DECISIONS.md`. The mechanism is
 * `UCataclysmAreaShareOnAllies`, called from the four places a point-blank skill deals its own blow.
 *
 * THE ROW IS GIVEN BY HAND, as the enchantment would give it: no row is authored. Every wearer holds
 * `aoe_skill_damage_to_nearby_allies_percent` at a flat 50 from an enchantment, requiring `Type.AOE.PointBlank`.
 *
 * THE CHARACTERS ARE REAL: player pawns with their ability system on a player state, as the game wires them, so the
 * player's own listeners raise `hit_dealt`, `critical_strike` and `kill`; imps summoned with `ACataclysmMinion::Spawn`.
 * AN ENEMY OF THE SAME DEFENCES AS AN ALLY is an imp summoned by a creature of the monsters' side: the same class and
 * the same defaults as the wearer's own imp, on the other side. The players are never refreshed, so they hold no class
 * line, no armour and no resistance unless a test gives them one. Each wearer holds a weapon damage of 100.
 *
 * NO ROLL IS LEFT TO CHANCE. `CataclysmTestWorld::MakeWorldThatHasBegunPlay` pins the critical strike roll to 100,
 * which never strikes, except in the one test that pins it to 0 after making its world. Nobody here has an evasion or
 * a block chance, and a point-blank blow is area damage, which cannot be evaded.
 *
 * NO WAIT ON A TICK: every swing is made with `SwingOnce` or an activation, which deal their blows at once, and the one
 * zone is swept by hand. Every character stands at least 1 m from every other.
 *
 * FIGURES ARE READ AS WHAT EACH CHARACTER LOST against what its health was when the test set it, and a share as the
 * percent of what an enemy of the same defences lost to the same swing.
 */
namespace CataclysmAoeStrikesAlliesTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	constexpr float M = 100.0f;
	constexpr float Pool = 1000.0f;
	constexpr float Percent = 50.0f;

	/** A Strike all round of 6 m, which nothing else here reaches. */
	const TCHAR* const AllRound = TEXT("Radius=6; Angle=360");

	/** The skill's tags: point blank and melee, as the designed point-blank Strikes are. */
	const TCHAR* const PointBlankCell = TEXT("Type.AOE.PointBlank, Type.Melee");

	FGameplayTag PointBlankTag()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("Type.AOE.PointBlank")), /*ErrorIfNotFound=*/false);
	}

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

	/** A player pawn with its ability system on a player state, at a thousand health, with a weapon damage of 100. */
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
		SetAttribute(Actor, Combat::GetAttackDamageAttribute(), 100.0f);
		SetAttribute(Actor, Vital::GetMaxManaAttribute(), 1000.0f);
		SetAttribute(Actor, Vital::GetManaAttribute(), 1000.0f);
		return Actor;
	}

	/** A creature of the monsters' side, too healthy for any test to kill, far from everybody. */
	ACataclysmEnemyCharacter* SpawnCreature(UWorld* World)
	{
		ACataclysmEnemyCharacter* Actor =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector(0.0f, -80.0f * M, 0.0f), FRotator::ZeroRotator);
		if (Actor)
		{
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			SetHealth(Actor, 100000.0f, 100000.0f);
		}
		return Actor;
	}

	/** An imp of `Summoner`'s, at a thousand health of a thousand: an ally of a player's, an enemy of a creature's. */
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

	/** One flat figure an enchantment gives, requiring `Type.AOE.PointBlank`, another named tag, or none. */
	struct FHeld
	{
		const TCHAR* Stat = nullptr;
		float Value = 0.0f;
		bool bPointBlankOnly = false;
		const TCHAR* OtherRequiredTag = nullptr;
	};

	/** Give this character flat figures from an enchantment, as worn rows would, replacing any it held. */
	void Hold(const AActor* Actor, const TArray<FHeld>& Stats)
	{
		UCataclysmAbilitySystemComponent* System = SystemOf(Actor);
		if (!System)
		{
			return;
		}
		TMap<FName, FCataclysmStatInputs> Inputs;
		for (const FHeld& Stat : Stats)
		{
			FCataclysmStatModifier Flat;
			Flat.Bucket = ECataclysmStatBucket::Flat;
			Flat.Source = ECataclysmModifierSource::Enchantment;
			Flat.Value = Stat.Value;
			if (Stat.bPointBlankOnly)
			{
				Flat.RequiredTags.AddTag(PointBlankTag());
			}
			if (Stat.OtherRequiredTag)
			{
				Flat.RequiredTags.AddTag(
					FGameplayTag::RequestGameplayTag(FName(Stat.OtherRequiredTag), /*ErrorIfNotFound=*/false));
			}
			FCataclysmStatInputs& Line = Inputs.FindOrAdd(FName(Stat.Stat));
			Line.Base = 0.0f;
			Line.Modifiers = {Flat};
		}
		System->SetStatInputs(MoveTemp(Inputs));
	}

	/** Wear the row as it is ruled: the percent, flat, requiring `Type.AOE.PointBlank`. */
	void Wear(const AActor* Actor)
	{
		Hold(Actor, {{UCataclysmAreaShareOnAllies::PercentStat, Percent, /*bPointBlankOnly=*/true}});
	}

	/** A skill template granted into a slot of a player's and given a row's parameters, name and tags. */
	template <typename TSkill>
	TSkill* GrantSkill(ACataclysmPlayerCharacter* Player, ECataclysmAbilitySlot Slot, const FString& ParamText,
					   const FString& Name, const FString& TagCell)
	{
		UCataclysmAbilitySystemComponent* System = SystemOf(Player);
		if (!System)
		{
			return nullptr;
		}
		const FGameplayAbilitySpecHandle Handle =
			System->GiveAbilityInSlot(TSkill::StaticClass(), Slot, /*Level=*/100, Player);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? System->FindAbilitySpecFromHandle(Handle) : nullptr;
		TSkill* Skill = Spec ? Cast<TSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = Name;
			Skill->Params = UCataclysmSkillShapes::ParseParams(ParamText);
			Skill->SkillTags = UCataclysmSkillShapes::TagsFromCell(TagCell);
		}
		return Skill;
	}

	/** The point-blank Strike all round most tests swing. */
	UCataclysmStrikeSkill* GrantNova(ACataclysmPlayerCharacter* Player, const FString& ParamText = AllRound)
	{
		return GrantSkill<UCataclysmStrikeSkill>(Player, ECataclysmAbilitySlot::Heavy, ParamText,
												 TEXT("A point-blank nova"), PointBlankCell);
	}

	/** Use a granted skill, as its key would. */
	bool Activate(ACataclysmPlayerCharacter* Player, UGameplayAbility* Skill)
	{
		UCataclysmAbilitySystemComponent* System = SystemOf(Player);
		return System && Skill
			&& System->TryActivateAbility(Skill->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
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

#define CATACLYSM_AOE_ALLIES_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesShareTest,
	"Cataclysm.AoeStrikesAllies.AMinionInsideThePointBlankSwingTakesThePercentOfWhatAnEnemyThereTakes")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// Two players 100 m apart, each with an imp of its own 2 m along X and a creature's imp 2 m along Y, each swinging
	// a point-blank nova all round of 6 m. Only the second wears the row. The first is the control.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Plain = SpawnPlayer(World, FVector(0.0f, 100.0f * M, 0.0f));
	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World);
	if (!TestNotNull(TEXT("set-up: a plain player"), Plain) || !TestNotNull(TEXT("set-up: a wearer"), Wearer)
		|| !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* PlainsImp = SpawnImp(Plain, Plain->GetActorLocation() + FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* PlainsEnemy = SpawnImp(Creature, Plain->GetActorLocation() + FVector(0.0f, 2.0f * M, 0.0f));
	ACataclysmMinion* Ally = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Enemy = SpawnImp(Creature, FVector(0.0f, 2.0f * M, 0.0f));
	UCataclysmStrikeSkill* PlainNova = GrantNova(Plain);
	UCataclysmStrikeSkill* WearersNova = GrantNova(Wearer);
	if (!TestNotNull(TEXT("set-up: the plain player's imp"), PlainsImp)
		|| !TestNotNull(TEXT("set-up: an enemy imp beside it"), PlainsEnemy)
		|| !TestNotNull(TEXT("set-up: the wearer's imp"), Ally) || !TestNotNull(TEXT("set-up: an enemy imp"), Enemy)
		|| !TestNotNull(TEXT("set-up: a plain nova"), PlainNova)
		|| !TestNotNull(TEXT("set-up: the wearer's nova"), WearersNova))
	{
		return false;
	}
	if (!TestTrue(TEXT("set-up: the wearer's imp is its ally"), UCataclysmTargeting::IsFriendlyTo(Ally, Wearer))
		|| !TestTrue(TEXT("set-up: the creature's imp is its enemy"), UCataclysmTargeting::IsHostileTo(Enemy, Wearer)))
	{
		return false;
	}
	Wear(Wearer);

	PlainNova->SwingOnce();
	const float PlainsEnemyLost = Pool - HealthOf(PlainsEnemy);
	if (!TestTrue(*FString::Printf(TEXT("control: the plain swing takes health from the enemy: %.2f"),
			PlainsEnemyLost), PlainsEnemyLost > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: and nothing from the imp of the player without the row"), HealthOf(PlainsImp), Pool,
		0.01f);

	WearersNova->SwingOnce();
	const float EnemyLost = Pool - HealthOf(Enemy);
	TestEqual(TEXT("the wearer's enemy takes what the plain swing's did: the row changes nothing for it"),
		EnemyLost, PlainsEnemyLost, 0.01f);
	TestEqual(TEXT("the wearer's imp inside the swing takes half of what the enemy of the same defences took"),
		Pool - HealthOf(Ally), EnemyLost * Percent / 100.0f, 0.01f);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesOutsideTest,
	"Cataclysm.AoeStrikesAllies.AnAllyOutsideTheSwingsAreaTakesNone")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// A cone 90 degrees wide and 6 m deep, facing along X, which is the facing of a pawn spawned unturned and with no
	// cursor. Three imps of the wearer's: 2 m in front, the control; 8 m in front, beyond the reach; 2 m behind,
	// outside the angle.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer))
	{
		return false;
	}
	ACataclysmMinion* Inside = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Beyond = SpawnImp(Wearer, FVector(8.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Behind = SpawnImp(Wearer, FVector(-2.0f * M, 0.0f, 0.0f));
	UCataclysmStrikeSkill* Cone = GrantNova(Wearer, TEXT("Radius=6; Angle=90"));
	if (!TestNotNull(TEXT("set-up: an imp in front"), Inside) || !TestNotNull(TEXT("set-up: one beyond"), Beyond)
		|| !TestNotNull(TEXT("set-up: one behind"), Behind) || !TestNotNull(TEXT("set-up: a cone"), Cone))
	{
		return false;
	}
	Wear(Wearer);

	Cone->SwingOnce();
	const float InsideLost = Pool - HealthOf(Inside);
	if (!TestTrue(*FString::Printf(TEXT("control: the imp inside the cone takes a share: %.2f"), InsideLost),
			InsideLost > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the imp 8 m in front, beyond the cone's 6 m, takes none"), HealthOf(Beyond), Pool, 0.01f);
	TestEqual(TEXT("and the imp 2 m behind, outside its 90 degrees, takes none"), HealthOf(Behind), Pool, 0.01f);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesTagTest,
	"Cataclysm.AoeStrikesAllies.ASkillWithoutThePointBlankTagGivesNone")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// One wearer and one imp 2 m away; two Strikes all round of the same reach, one point blank and one only melee.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer))
	{
		return false;
	}
	ACataclysmMinion* Ally = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	UCataclysmStrikeSkill* Nova = GrantNova(Wearer);
	UCataclysmStrikeSkill* Swing = GrantSkill<UCataclysmStrikeSkill>(
		Wearer, ECataclysmAbilitySlot::Special, AllRound, TEXT("A melee swing"), TEXT("Type.Melee"));
	if (!TestNotNull(TEXT("set-up: an imp"), Ally) || !TestNotNull(TEXT("set-up: a nova"), Nova)
		|| !TestNotNull(TEXT("set-up: a swing"), Swing)
		|| !TestTrue(TEXT("set-up: Type.AOE.PointBlank is in the vocabulary"), PointBlankTag().IsValid()))
	{
		return false;
	}
	Wear(Wearer);

	Nova->SwingOnce();
	const float NovaShare = Pool - HealthOf(Ally);
	if (!TestTrue(*FString::Printf(TEXT("control: the point-blank nova gives the imp a share: %.2f"), NovaShare),
			NovaShare > 0.0f))
	{
		return false;
	}
	SetHealth(Ally, Pool, Pool);
	Swing->SwingOnce();
	TestEqual(TEXT("the swing without Type.AOE.PointBlank gives it none"), HealthOf(Ally), Pool, 0.01f);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesRidersTest,
	"Cataclysm.AoeStrikesAllies.TheAllyTakesNoBurnKnockbackOrStunTheSwingGivesTheEnemy")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// A nova all round that burns, knocks back 3 m and stuns for a second. The wearer's imp 2 m along X and a
	// creature's imp 2 m along Y. The enemy is the control for each rider.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Ally = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Enemy = SpawnImp(Creature, FVector(0.0f, 2.0f * M, 0.0f));
	UCataclysmStrikeSkill* Nova =
		GrantNova(Wearer, TEXT("Radius=6; Angle=360; Burn=1; Knockback=3; Effect=Stun; StunSeconds=1"));
	if (!TestNotNull(TEXT("set-up: an imp"), Ally) || !TestNotNull(TEXT("set-up: an enemy imp"), Enemy)
		|| !TestNotNull(TEXT("set-up: a nova"), Nova))
	{
		return false;
	}
	Wear(Wearer);
	const FVector AllyWas = Ally->GetActorLocation();
	const FVector EnemyWas = Enemy->GetActorLocation();

	Nova->SwingOnce();
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	TestTrue(TEXT("control: the enemy is set alight"), UCataclysmSkillEffects::HasTag(Enemy, Burn));
	const double EnemyMoved = FVector::Dist(Enemy->GetActorLocation(), EnemyWas);
	TestTrue(*FString::Printf(TEXT("control: the enemy is knocked back: %.0f cm"), EnemyMoved), EnemyMoved > 1.0f * M);
	TestTrue(TEXT("control: the enemy is stunned"), UCataclysmSkillEffects::IsStunned(Enemy));
	if (!TestTrue(*FString::Printf(TEXT("set-up: the imp took its share: %.2f"), Pool - HealthOf(Ally)),
			HealthOf(Ally) < Pool))
	{
		return false;
	}
	TestFalse(TEXT("the imp is not set alight"), UCataclysmSkillEffects::HasTag(Ally, Burn));
	TestEqual(TEXT("the imp is not moved"),
		static_cast<float>(FVector::Dist(Ally->GetActorLocation(), AllyWas)), 0.0f, 1.0f);
	TestFalse(TEXT("and the imp is not stunned"), UCataclysmSkillEffects::IsStunned(Ally));
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesEventsTest,
	"Cataclysm.AoeStrikesAllies.TheAllysBlowRaisesNoHitDealtCriticalStrikeOrKillOnTheWearer")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// Every roll critically strikes, and the wearer has a chance of 50. Two creature's imps, one 2 m along Y at a
	// hundred thousand health and one 2 m along -Y at 1. The wearer's imp is summoned only after the first swing, 2 m
	// along X.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll AlwaysCrits(0.0f);

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Enemy = SpawnImp(Creature, FVector(0.0f, 2.0f * M, 0.0f));
	ACataclysmMinion* Doomed = SpawnImp(Creature, FVector(0.0f, -2.0f * M, 0.0f));
	UCataclysmStrikeSkill* Nova = GrantNova(Wearer);
	UCataclysmAbilitySystemComponent* System = SystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: an enemy imp"), Enemy) || !TestNotNull(TEXT("set-up: a doomed one"), Doomed)
		|| !TestNotNull(TEXT("set-up: a nova"), Nova) || !TestNotNull(TEXT("set-up: an ability system"), System))
	{
		return false;
	}
	Wear(Wearer);
	SetAttribute(Wearer, Combat::GetCritChanceAttribute(), 50.0f);
	SetAttribute(Wearer, Combat::GetCritMultiplierAttribute(), 150.0f);
	SetHealth(Doomed, Pool, 1.0f);
	// THE OTHER ENEMY IS STRUCK THREE TIMES, CRITICALLY, and must not die, or its death would be a kill of its own.
	SetHealth(Enemy, 100000.0f, 100000.0f);

	FEventCount HitsDealt(System, TEXT("hit_dealt"));
	FEventCount FirstHits(System, TEXT("first_hit_dealt"));
	FEventCount Crits(System, TEXT("critical_strike"));
	FEventCount Kills(System, TEXT("kill"));

	// THE CONTROL: two enemies struck, both critically, one killed.
	Nova->SwingOnce();
	if (!TestTrue(TEXT("set-up: the doomed enemy dies"), UCataclysmSkillEffects::IsDead(Doomed))
		|| !TestEqual(TEXT("control: two hits dealt"), HitsDealt.Count, 2)
		|| !TestEqual(TEXT("control: two first hits"), FirstHits.Count, 2)
		|| !TestEqual(TEXT("control: two critical strikes"), Crits.Count, 2)
		|| !TestEqual(TEXT("control: one kill"), Kills.Count, 1))
	{
		return false;
	}

	// THE SAME SWING WITH AN ALLY INSIDE: one more of each for the enemy, none for the ally.
	ACataclysmMinion* Ally = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the wearer's imp"), Ally))
	{
		return false;
	}
	Nova->SwingOnce();
	if (!TestTrue(*FString::Printf(TEXT("set-up: the imp took its share: %.2f"), Pool - HealthOf(Ally)),
			HealthOf(Ally) < Pool))
	{
		return false;
	}
	TestEqual(TEXT("one hit dealt more, the enemy's: the ally's blow raises no hit_dealt"), HitsDealt.Count, 3);
	TestEqual(TEXT("no first hit more: neither the enemy's second nor the ally's raises first_hit_dealt"),
		FirstHits.Count, 2);
	TestEqual(TEXT("one critical strike more, the enemy's: the ally's blow raises no critical_strike"), Crits.Count, 3);

	// AND A SHARE THAT KILLS THE ALLY.
	SetHealth(Ally, Pool, 1.0f);
	Nova->SwingOnce();
	if (!TestTrue(TEXT("set-up: the share kills the imp"), UCataclysmSkillEffects::IsDead(Ally)))
	{
		return false;
	}
	TestEqual(TEXT("and raises no kill"), Kills.Count, 1);
	TestEqual(TEXT("while the enemy's blow of the same swing is still a hit dealt"), HitsDealt.Count, 4);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesArmourTest,
	"Cataclysm.AoeStrikesAllies.TheAllysArmourReducesItsShare")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// Two other players inside the wearer's nova, allies by team: a plain one 2 m along X, the control, and one with
	// 800 armour 2 m along Y.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Plain = SpawnPlayer(World, FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmPlayerCharacter* Armoured = SpawnPlayer(World, FVector(0.0f, 2.0f * M, 0.0f));
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a plain ally"), Plain)
		|| !TestNotNull(TEXT("set-up: an armoured one"), Armoured))
	{
		return false;
	}
	UCataclysmStrikeSkill* Nova = GrantNova(Wearer);
	if (!TestNotNull(TEXT("set-up: a nova"), Nova)
		|| !TestTrue(TEXT("set-up: the players are on one side"), UCataclysmTargeting::IsFriendlyTo(Armoured, Wearer)))
	{
		return false;
	}
	Wear(Wearer);
	constexpr float Armour = 800.0f;
	SetAttribute(Armoured, Combat::GetArmorAttribute(), Armour);

	Nova->SwingOnce();
	const float PlainLost = Pool - HealthOf(Plain);
	const float ArmouredLost = Pool - HealthOf(Armoured);
	if (!TestTrue(*FString::Printf(TEXT("control: the plain ally takes a share: %.2f"), PlainLost), PlainLost > 0.0f))
	{
		return false;
	}
	const float ArmourKeeps =
		1.0f - UCataclysmDamageCalculation::ArmorReduction(Armour, ACataclysmGameMode::DifficultyTierIn(World)) / 100.0f;
	TestTrue(*FString::Printf(TEXT("the armoured ally takes less: %.2f against %.2f"), ArmouredLost, PlainLost),
		ArmouredLost > 0.0f && ArmouredLost < PlainLost);
	TestEqual(TEXT("by what its armour removes from a blow"), ArmouredLost, PlainLost * ArmourKeeps, 0.01f);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesStoreTest,
	"Cataclysm.AoeStrikesAllies.AStoreOfTheWearersSurvivesASwingThatStruckAnAllyAndIsSpentOnTheEnemy")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// The wearer also holds Nothing Wasted at its cap of 100%, and its melee nova adds the store to its next melee
	// blow. A creature's imp 2 m along Y, which is moved 30 m away for the second swing and back for the third; the
	// wearer's imp 2 m along X, summoned after the first.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	const FVector EnemyAt(0.0f, 2.0f * M, 0.0f);
	ACataclysmMinion* Enemy = SpawnImp(Creature, EnemyAt);
	UCataclysmStrikeSkill* Nova = GrantNova(Wearer);
	UCataclysmAbilitySystemComponent* System = SystemOf(Wearer);
	if (!TestNotNull(TEXT("set-up: an enemy imp"), Enemy) || !TestNotNull(TEXT("set-up: a nova"), Nova)
		|| !TestNotNull(TEXT("set-up: an ability system"), System))
	{
		return false;
	}
	Hold(Wearer, {{UCataclysmAreaShareOnAllies::PercentStat, Percent, /*bPointBlankOnly=*/true},
				  {UCataclysmAbilitySystemComponent::MitigatedAddedCapStat, 100.0f, /*bPointBlankOnly=*/false}});

	// THE CONTROL: the enemy's blow with an empty store.
	Nova->SwingOnce();
	const float Plain = Pool - HealthOf(Enemy);
	if (!TestTrue(*FString::Printf(TEXT("control: the enemy's blow, at least the 50 stored below: %.2f"), Plain),
			Plain >= 50.0f))
	{
		return false;
	}
	SetHealth(Enemy, Pool, Pool);
	System->NoteMitigatedDamage(50.0f);
	if (!TestEqual(TEXT("set-up: the store holds 50"), System->StoredMitigatedDamageNow(), 50.0f, 0.01f))
	{
		return false;
	}

	// A SWING THAT STRIKES ONLY THE ALLY.
	ACataclysmMinion* Ally = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the wearer's imp"), Ally))
	{
		return false;
	}
	Enemy->SetActorLocation(FVector(30.0f * M, 30.0f * M, 0.0f));
	Nova->SwingOnce();
	TestEqual(TEXT("set-up: the enemy, 42 m away, is not struck"), HealthOf(Enemy), Pool, 0.01f);
	TestEqual(TEXT("the ally takes half the plain blow, and none of the store"), Pool - HealthOf(Ally),
		Plain * Percent / 100.0f, 0.01f);
	TestEqual(TEXT("and the store survives the swing whole"), System->StoredMitigatedDamageNow(), 50.0f, 0.01f);

	// AND THE NEXT SWING SPENDS IT ON THE ENEMY, AS WITHOUT THE ROW.
	Enemy->SetActorLocation(EnemyAt);
	SetHealth(Ally, Pool, Pool);
	Nova->SwingOnce();
	TestEqual(TEXT("the enemy takes the plain blow and the 50 stored"), Pool - HealthOf(Enemy), Plain + 50.0f, 0.01f);
	TestEqual(TEXT("the store is spent"), System->StoredMitigatedDamageNow(), 0.0f, 0.01f);
	TestEqual(TEXT("and the ally beside it still takes half the plain blow"), Pool - HealthOf(Ally),
		Plain * Percent / 100.0f, 0.01f);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesNotTheWearerTest,
	"Cataclysm.AoeStrikesAllies.TheWearerIsNeverStruckByItsOwnShare")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// The wearer stands at the centre of its own nova, with its imp 2 m away.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer))
	{
		return false;
	}
	ACataclysmMinion* Ally = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	UCataclysmStrikeSkill* Nova = GrantNova(Wearer);
	if (!TestNotNull(TEXT("set-up: an imp"), Ally) || !TestNotNull(TEXT("set-up: a nova"), Nova))
	{
		return false;
	}
	Wear(Wearer);

	Nova->SwingOnce();
	if (!TestTrue(*FString::Printf(TEXT("control: the imp takes a share: %.2f"), Pool - HealthOf(Ally)),
			HealthOf(Ally) < Pool))
	{
		return false;
	}
	TestEqual(TEXT("and the wearer, inside its own swing, loses nothing"), HealthOf(Wearer), Pool, 0.01f);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesLeapTest,
	"Cataclysm.AoeStrikesAllies.ALeapStrikesTheAllyWhereItLandsAndNoneUnderItsArc")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// A point-blank leap of 9 m with a landing of 5 m, along X with no cursor, so it lands 9 m along X. The wearer's
	// imp 1 m past the landing, and another 2 m along X and 2 m to the side of the path, so the leap's sweep does not
	// meet it, 7.3 m from the landing; a creature's imp 2 m to the side of the landing, the control and the enemy of
	// the same defences.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* AtTheLanding = SpawnImp(Wearer, FVector(10.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* UnderTheArc = SpawnImp(Wearer, FVector(2.0f * M, 2.0f * M, 0.0f));
	ACataclysmMinion* Enemy = SpawnImp(Creature, FVector(9.0f * M, 2.0f * M, 0.0f));
	UCataclysmMovementSkill* Leap = GrantSkill<UCataclysmMovementSkill>(
		Wearer, ECataclysmAbilitySlot::Movement, TEXT("Mode=Leap; Range=9; Radius=5"), TEXT("A point-blank leap"),
		PointBlankCell);
	if (!TestNotNull(TEXT("set-up: an imp at the landing"), AtTheLanding)
		|| !TestNotNull(TEXT("set-up: one under the arc"), UnderTheArc)
		|| !TestNotNull(TEXT("set-up: an enemy imp"), Enemy) || !TestNotNull(TEXT("set-up: a leap"), Leap))
	{
		return false;
	}
	Wear(Wearer);

	if (!TestTrue(TEXT("set-up: the leap activates"), Activate(Wearer, Leap))
		|| !TestTrue(*FString::Printf(TEXT("set-up: and lands 9 m along X: %s"), *Wearer->GetActorLocation().ToString()),
			FVector::Dist2D(Wearer->GetActorLocation(), FVector(9.0f * M, 0.0f, 0.0f)) < 0.5f * M))
	{
		return false;
	}
	const float EnemyLost = Pool - HealthOf(Enemy);
	if (!TestTrue(*FString::Printf(TEXT("control: the enemy at the landing is struck: %.2f"), EnemyLost),
			EnemyLost > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("the imp at the landing takes half of what the enemy of the same defences took"),
		Pool - HealthOf(AtTheLanding), EnemyLost * Percent / 100.0f, 0.01f);
	TestEqual(TEXT("and the imp beside the path, 7.3 m from the landing, takes none"), HealthOf(UnderTheArc), Pool,
		0.01f);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesZoneTest,
	"Cataclysm.AoeStrikesAllies.AGroundZoneTheSkillLaysStrikesNoAlly")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// A point-blank Strike all round of 4 m that leaves burning ground of 4 m where it is cast. The wearer's imp 2 m
	// along X and a creature's imp 2 m along Y, both inside the swing and the ground. The zone is swept by hand.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Wearer = SpawnPlayer(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Creature = SpawnCreature(World);
	if (!TestNotNull(TEXT("set-up: a wearer"), Wearer) || !TestNotNull(TEXT("set-up: a creature"), Creature))
	{
		return false;
	}
	ACataclysmMinion* Ally = SpawnImp(Wearer, FVector(2.0f * M, 0.0f, 0.0f));
	ACataclysmMinion* Enemy = SpawnImp(Creature, FVector(0.0f, 2.0f * M, 0.0f));
	UCataclysmStrikeSkill* Nova = GrantNova(
		Wearer, TEXT("Radius=4; Angle=360; GroundRadius=4; GroundDuration=6; GroundPercent=20"));
	if (!TestNotNull(TEXT("set-up: an imp"), Ally) || !TestNotNull(TEXT("set-up: an enemy imp"), Enemy)
		|| !TestNotNull(TEXT("set-up: a nova"), Nova))
	{
		return false;
	}
	Wear(Wearer);

	if (!TestTrue(TEXT("set-up: the nova activates"), Activate(Wearer, Nova))
		|| !TestTrue(*FString::Printf(TEXT("control: the swing itself gives the imp a share: %.2f"),
			Pool - HealthOf(Ally)), HealthOf(Ally) < Pool))
	{
		return false;
	}
	TArray<ACataclysmGroundZone*> Zones;
	for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
	{
		Zones.Add(*It);
	}
	if (!TestEqual(TEXT("set-up: the nova left one zone"), Zones.Num(), 1))
	{
		return false;
	}
	const float AllyBefore = HealthOf(Ally);
	const float EnemyBefore = HealthOf(Enemy);
	Zones[0]->Sweep();
	TestTrue(*FString::Printf(TEXT("control: the zone's sweep burns the enemy inside it: %.2f"),
		EnemyBefore - HealthOf(Enemy)), HealthOf(Enemy) < EnemyBefore);
	TestEqual(TEXT("and gives the imp inside it nothing"), HealthOf(Ally), AllyBefore, 0.01f);
	return true;
}

CATACLYSM_AOE_ALLIES_TEST(FCataclysmAoeStrikesAlliesGadgetCopyTest,
	"Cataclysm.AoeStrikesAllies.AGadgetsCopyOfAPointBlankSkillStrikesTheAllyBesideItAndNotTheGadget")
{
	using namespace CataclysmAoeStrikesAlliesTest;

	// The rig of `Cataclysm.GadgetCopiesSkill.`: a possessed player holding a Demonic Whip, a weapon damage of 100,
	// and a Bolt Turret of its own placed 20 m along Y. The whip's Ultimate, The Gathering, is a point-blank Strike of
	// 14 m all round, and its use is announced by hand so the turret copies it. A creature 3 m along X from the turret
	// is the turret's target; an imp of the player's 3 m along -X from the turret is the ally beside it. Ruled
	// 2026-10-15 by the coordinating session: the casting gadget is excluded.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Wearer =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a player state"), State) || !TestNotNull(TEXT("set-up: a controller"), Controller)
		|| !TestNotNull(TEXT("set-up: a wearer"), Wearer))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Wearer);
	UCataclysmWeaponSlotsComponent* Slots = Wearer->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
	if (!TestNotNull(TEXT("set-up: the wearer's weapon slots"), Slots))
	{
		return false;
	}
	Slots->SetDamageType(TEXT("Demonic"));
	Slots->EquipWeaponType(TEXT("Whip"));
	SetHealth(Wearer, 10000.0f, 10000.0f);
	SetAttribute(Wearer, Combat::GetAttackDamageAttribute(), 100.0f);
	SetAttribute(Wearer, Vital::GetMaxManaAttribute(), 1000.0f);
	SetAttribute(Wearer, Vital::GetManaAttribute(), 1000.0f);

	FCataclysmWeaponSkill Gathering;
	if (!TestTrue(TEXT("set-up: the wearer holds The Gathering. If not, DT_WeaponSkills may be older than "
				   "game/Data/WeaponSkills.csv: run tools/generate_datatable_assets.py"),
			UCataclysmTriggeredSkill::HeldSkillNamed(Wearer, FName(TEXT("The Gathering")), Gathering))
		|| !TestTrue(TEXT("set-up: it is point blank"), Gathering.Tags.HasTag(PointBlankTag()))
		|| !TestTrue(TEXT("set-up: and a skill a row may cast again"), UCataclysmTriggeredSkill::RepeatsFromARow(Gathering)))
	{
		return false;
	}

	const FVector TurretAt(0.0f, 20.0f * M, 0.0f);
	ACataclysmMinion* Turret =
		ACataclysmMinion::Spawn(Wearer, TurretAt, /*Lifetime=*/600.0f, /*bBurns=*/false, TEXT("BoltTurret"));
	ACataclysmEnemyCharacter* Creature =
		World->SpawnActor<ACataclysmEnemyCharacter>(TurretAt + FVector(3.0f * M, 0.0f, 0.0f), FRotator::ZeroRotator);
	ACataclysmMinion* Beside = SpawnImp(Wearer, TurretAt + FVector(-3.0f * M, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a turret"), Turret)
		|| !TestEqual(TEXT("set-up: DT_MinionTypes supplied the BoltTurret row"), Turret->TypeName,
			FString(TEXT("BoltTurret")))
		|| !TestNotNull(TEXT("set-up: a creature"), Creature) || !TestNotNull(TEXT("set-up: an imp"), Beside))
	{
		return false;
	}
	Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	SetHealth(Creature, 100000.0f, 100000.0f);
	SetHealth(Turret, Pool, Pool);
	if (!TestTrue(TEXT("set-up: the turret is the wearer's ally"), UCataclysmTargeting::IsFriendlyTo(Turret, Wearer)))
	{
		return false;
	}

	const FVector Aim = Creature->GetActorLocation();
	UCataclysmCombatEvents::NoteSkillUsed(Wearer, Gathering.Name, Gathering.Tags, Gathering.Slot, &Aim,
										  /*bHasCooldown=*/true);
	Hold(Wearer, {{ACataclysmMinion::GadgetsCopyYourLastSkillStat, 1.0f, false, TEXT("Type.Deployable")},
				  {UCataclysmAreaShareOnAllies::PercentStat, Percent, /*bPointBlankOnly=*/true}});
	if (!TestTrue(TEXT("set-up: the turret's target is the creature"), Turret->SkillCopyTarget() == Creature))
	{
		return false;
	}

	ACataclysmMinion::GadgetStep(Turret);
	if (!TestEqual(TEXT("set-up: the turret casts one copy on its step"), Turret->SkillCopiesMade, 1)
		|| !TestTrue(*FString::Printf(TEXT("control: the copy strikes the turret's target: %.2f"),
			100000.0f - HealthOf(Creature)), HealthOf(Creature) < 100000.0f))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("control: the imp beside the turret is struck for its share: %.2f"),
		Pool - HealthOf(Beside)), HealthOf(Beside) < Pool);
	TestEqual(TEXT("and the turret the copy is cast from, at the centre of its own swing, loses nothing"),
		HealthOf(Turret), Pool, 0.01f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
