// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Thorns of Glass: "Strikes back at whatever hits it, once per hit, for its own attack damage." Issue #2749 (B2), the
 * owner's ruling of 2026-11-18, which replaced "Reflects 50% of all damage taken back to the attacker": a share of the
 * hit grew with the player's own damage, so the stronger the player, the harder the creature punished them. The
 * mechanism is `UCataclysmRetaliation::StrikeBack`, called by `UCataclysmVitalAttributeSet` under retaliation's own
 * conditions: a hit that got through, never a tick, and never a blow that cannot be retaliated against.
 *
 * THE CHARACTERS ARE REAL: a player pawn with its ability system on a player state, as the game wires one, and creatures
 * of the monsters' side given the row as a spawned creature is, by `ApplyModifierAttributes`. The player is never
 * refreshed, so it holds no class line; its armour, evasion, block chance and resistance are set to nought, so a blow
 * on it takes the figure sent. Every creature's are nought too, so no blow on one is evaded or blocked. Each creature's
 * attack damage is `ThornsCreatureAttackDamage`, written after the row is given.
 *
 * NO ROLL IS LEFT TO CHANCE: `CataclysmTestWorld::MakeWorldThatHasBegunPlay` pins the critical strike roll to 100, which
 * never strikes, and a strike back cannot critically strike anyway. Every reading has a control.
 */
namespace CataclysmThornsOfGlassTest
{
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	constexpr float ThornsMetre = 100.0f;
	constexpr float ThornsCreatureAttackDamage = 40.0f;
	constexpr float ThornsPlayerHealth = 10000.0f;

	float ThornsHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : -1.0f;
	}

	void ThornsSet(const AActor* Actor, const FGameplayAttribute& Attribute, float Value)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			System->SetNumericAttributeBase(Attribute, Value);
		}
	}

	/** No armour, evasion, block chance or resistance, so a blow takes what was sent and none is evaded or blocked. */
	void ThornsUndefended(const AActor* Actor)
	{
		ThornsSet(Actor, Combat::GetArmorAttribute(), 0.0f);
		ThornsSet(Actor, Combat::GetEvasionAttribute(), 0.0f);
		ThornsSet(Actor, Combat::GetBlockChanceAttribute(), 0.0f);
		ThornsSet(Actor, UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
	}

	/** The player back to full health, maximum first, with no energy shield to take any of a blow. */
	void ThornsHeal(const AActor* Player)
	{
		ThornsSet(Player, Vital::GetMaxHealthAttribute(), ThornsPlayerHealth);
		ThornsSet(Player, Vital::GetHealthAttribute(), ThornsPlayerHealth);
		ThornsSet(Player, Vital::GetEnergyShieldAttribute(), 0.0f);
	}

	/** A player pawn with its ability system on a player state, undefended, with an attack damage of 100. */
	ACataclysmPlayerCharacter* SpawnThornsPlayer(UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Player =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (State == nullptr || Player == nullptr)
		{
			return nullptr;
		}
		Player->SetPlayerState(State);
		Player->OnRep_PlayerState();
		ThornsHeal(Player);
		ThornsUndefended(Player);
		ThornsSet(Player, Combat::GetAttackDamageAttribute(), 100.0f);
		ThornsSet(Player, Vital::GetMaxManaAttribute(), 1000.0f);
		ThornsSet(Player, Vital::GetManaAttribute(), 1000.0f);
		return Player;
	}

	/** Thorns of Glass, given as a spawned creature is given its rows, and its attack damage written again after. */
	void GiveThorns(ACataclysmEnemyCharacter* Creature)
	{
		Creature->ModifierRows.AddUnique(FName(UCataclysmEnemyModifiers::ThornsOfGlassRow));
		Creature->ApplyModifierAttributes();
		ThornsSet(Creature, Combat::GetAttackDamageAttribute(), ThornsCreatureAttackDamage);
	}

	/** A creature of the monsters' side with this much health, undefended, and Thorns of Glass when asked. */
	ACataclysmEnemyCharacter* SpawnThornsCreature(UWorld* World, const FVector& Where, bool bThorns,
												  float Health = 100000.0f)
	{
		ACataclysmEnemyCharacter* Creature = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Creature == nullptr)
		{
			return nullptr;
		}
		Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Creature->SetHealth(Health);
		Creature->SetArmour(0.0f);
		ThornsUndefended(Creature);
		ThornsSet(Creature, Combat::GetAttackDamageAttribute(), ThornsCreatureAttackDamage);
		if (bThorns)
		{
			GiveThorns(Creature);
		}
		return Creature;
	}

	/** The player's plain hit at 100% of its attack damage, as a skill's blow is dealt. @return the figure sent. */
	float ThornsPlayerHits(ACataclysmPlayerCharacter* Player, ACataclysmEnemyCharacter* Creature,
						   const FCataclysmHitDelivery& Delivery = FCataclysmHitDelivery())
	{
		return UCataclysmSkillEffects::ApplyHit(Player, Creature, /*DamagePercent=*/100.0f, FGameplayTagContainer(),
												Delivery);
	}
}

#define CATACLYSM_THORNS_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

/**
 * THE CONTROL FIRST: a creature without the row sends nothing back. Then the player hits a creature carrying it, and
 * loses the creature's own attack damage, whole, being undefended.
 */
CATACLYSM_THORNS_TEST(FCataclysmThornsOfGlassStrikesBackTest,
	"Cataclysm.ThornsOfGlass.AHitIsStruckBackForTheCreaturesOwnAttackDamage")
{
	using namespace CataclysmThornsOfGlassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnThornsPlayer(World);
	ACataclysmEnemyCharacter* Plain = SpawnThornsCreature(World, FVector(3.0f * ThornsMetre, 0.0f, 0.0f), false);
	ACataclysmEnemyCharacter* Thorned = SpawnThornsCreature(World, FVector(0.0f, 3.0f * ThornsMetre, 0.0f), true);
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a plain creature"), Plain)
		|| !TestNotNull(TEXT("set-up: a thorned creature"), Thorned))
	{
		return false;
	}

	float Before = ThornsHealthOf(Player);
	const float SentToPlain = ThornsPlayerHits(Player, Plain);
	if (!TestTrue(FString::Printf(TEXT("control: the player's hit was sent (%.2f)"), SentToPlain), SentToPlain > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: a creature without the row strikes back nothing"), ThornsHealthOf(Player), Before, 0.001f);

	Before = ThornsHealthOf(Player);
	ThornsPlayerHits(Player, Thorned);
	TestEqual(FString::Printf(TEXT("a creature carrying the row strikes back for its own attack damage, %.0f"),
							  ThornsCreatureAttackDamage),
			  Before - ThornsHealthOf(Player), ThornsCreatureAttackDamage, 0.01f);
	return true;
}

/**
 * "FOR ITS OWN ATTACK DAMAGE", NOT A SHARE OF THE HIT: a hit of ten and a hit of a thousand are struck back for the same
 * figure. Until issue #2749 the second sent back fifty times what the first did.
 */
CATACLYSM_THORNS_TEST(FCataclysmThornsOfGlassAnyHitTest,
	"Cataclysm.ThornsOfGlass.AHitOfTenAndAHitOfAThousandAreStruckBackForTheSame")
{
	using namespace CataclysmThornsOfGlassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnThornsPlayer(World);
	ACataclysmEnemyCharacter* Thorned = SpawnThornsCreature(World, FVector(3.0f * ThornsMetre, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a thorned creature"), Thorned))
	{
		return false;
	}

	ThornsSet(Player, Combat::GetAttackDamageAttribute(), 10.0f);
	float Before = ThornsHealthOf(Player);
	const float SentSmall = ThornsPlayerHits(Player, Thorned);
	const float BackForSmall = Before - ThornsHealthOf(Player);

	ThornsHeal(Player);
	ThornsSet(Player, Combat::GetAttackDamageAttribute(), 1000.0f);
	Before = ThornsHealthOf(Player);
	const float SentLarge = ThornsPlayerHits(Player, Thorned);
	const float BackForLarge = Before - ThornsHealthOf(Player);

	if (!TestTrue(FString::Printf(TEXT("set-up: the second hit was sent at about a hundred times the first: %.2f and %.2f"),
								  SentSmall, SentLarge),
				  SentSmall > 0.0f && SentLarge >= SentSmall * 50.0f))
	{
		return false;
	}
	TestEqual(TEXT("the small hit is struck back for the creature's attack damage"), BackForSmall,
			  ThornsCreatureAttackDamage, 0.01f);
	TestEqual(TEXT("and the large one for the same"), BackForLarge, BackForSmall, 0.01f);
	return true;
}

/**
 * "ONCE PER HIT": a damage over time tick is not a hit, and is not struck back. The design's Retaliation section says
 * retaliation "answers a hit, not a tick", and the strike back is made under retaliation's own conditions. THE CONTROL
 * AFTER: an ordinary blow on the same creature is struck back, so the first reading is about ticks.
 */
CATACLYSM_THORNS_TEST(FCataclysmThornsOfGlassNoTickTest,
	"Cataclysm.ThornsOfGlass.ADamageOverTimeTickIsNotStruckBack")
{
	using namespace CataclysmThornsOfGlassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnThornsPlayer(World);
	ACataclysmEnemyCharacter* Thorned = SpawnThornsCreature(World, FVector(3.0f * ThornsMetre, 0.0f, 0.0f), true);
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a thorned creature"), Thorned))
	{
		return false;
	}

	FCataclysmHitDelivery OverTime;
	OverTime.bIsDamageOverTime = true;
	const float CreatureBefore = ThornsHealthOf(Thorned);
	float Before = ThornsHealthOf(Player);
	ThornsPlayerHits(Player, Thorned, OverTime);
	if (!TestTrue(FString::Printf(TEXT("set-up: the tick landed on the creature (%.2f)"),
								  CreatureBefore - ThornsHealthOf(Thorned)),
				  ThornsHealthOf(Thorned) < CreatureBefore))
	{
		return false;
	}
	TestEqual(TEXT("a damage over time tick is not struck back"), ThornsHealthOf(Player), Before, 0.001f);

	Before = ThornsHealthOf(Player);
	ThornsPlayerHits(Player, Thorned);
	TestEqual(TEXT("control: while an ordinary blow on the same creature is"), Before - ThornsHealthOf(Player),
			  ThornsCreatureAttackDamage, 0.01f);
	return true;
}

/**
 * ONE AREA BLOW ON THREE THORNED CREATURES IS THREE HITS, AND IS STRUCK BACK THREE TIMES, one for each creature it
 * struck. The blow is a point-blank nova of 4 m, started by `UCataclysmTriggeredSkill::Trigger` -- the call Echo
 * Chamber makes for its copy, so a copy's blows are struck back as the player's own are. It is given the player's own
 * place, as `Cataclysm.AoeStrikesAllies.` gives its nova, only because a test world is never ticked: a start without
 * one waits for an attack clip that never plays.
 */
CATACLYSM_THORNS_TEST(FCataclysmThornsOfGlassAreaBlowTest,
	"Cataclysm.ThornsOfGlass.ATriggeredAreaBlowOnThreeThornedCreaturesIsStruckBackThreeTimes")
{
	using namespace CataclysmThornsOfGlassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnThornsPlayer(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player))
	{
		return false;
	}
	const FVector Here = Player->GetActorLocation();
	const TArray<FVector> Places = {Here + FVector(2.0f * ThornsMetre, 0.0f, 0.0f),
									Here + FVector(-2.0f * ThornsMetre, 0.0f, 0.0f),
									Here + FVector(0.0f, 2.0f * ThornsMetre, 0.0f)};
	TArray<ACataclysmEnemyCharacter*> Thorned;
	TArray<float> CreaturesBefore;
	for (const FVector& Place : Places)
	{
		ACataclysmEnemyCharacter* Creature = SpawnThornsCreature(World, Place, true);
		if (!TestNotNull(TEXT("set-up: a thorned creature"), Creature))
		{
			return false;
		}
		Thorned.Add(Creature);
		CreaturesBefore.Add(ThornsHealthOf(Creature));
	}

	FCataclysmWeaponSkill Nova;
	Nova.Slot = ECataclysmAbilitySlot::Heavy;
	Nova.Name = TEXT("A point-blank nova");
	Nova.Shape = ECataclysmSkillShape::Strike;
	Nova.Params = UCataclysmSkillShapes::ParseParams(TEXT("Radius=4; Angle=360"));
	Nova.Tags = UCataclysmSkillShapes::TagsFromCell(TEXT("Type.AOE.PointBlank, Type.Melee"));
	const FVector Aim = Here + FVector(1.0f * ThornsMetre, 0.0f, 0.0f);

	const float Before = ThornsHealthOf(Player);
	if (!TestTrue(TEXT("set-up: the nova starts from the player's own place"),
				  UCataclysmTriggeredSkill::Trigger(Player, Nova, Aim, /*DamageShare=*/1.0f, /*bPaysCost=*/false, &Here)))
	{
		return false;
	}
	for (int32 Index = 0; Index < Thorned.Num(); ++Index)
	{
		if (!TestTrue(FString::Printf(TEXT("set-up: the one blow struck creature %d (%.2f)"), Index,
									  CreaturesBefore[Index] - ThornsHealthOf(Thorned[Index])),
					  ThornsHealthOf(Thorned[Index]) < CreaturesBefore[Index]))
		{
			return false;
		}
	}
	TestEqual(TEXT("three thorned creatures struck by one blow strike back three times"), Before - ThornsHealthOf(Player),
			  3.0f * ThornsCreatureAttackDamage, 0.01f);
	return true;
}

/**
 * THE STRIKE BACK IS A RETURNED BLOW: announced with the retaliation mark, which the player's hit log reads as
 * "reflected or retaliation", and carrying the no-retaliation mark, so a player with retaliation does not answer it.
 * THE CONTROL FIRST: the creature's own plain blow on the player is announced unmarked, and the player's retaliation
 * answers it. Then the player hits the creature, which takes the player's hit and nothing more.
 */
CATACLYSM_THORNS_TEST(FCataclysmThornsOfGlassReturnedBlowTest,
	"Cataclysm.ThornsOfGlass.AStrikeBackIsMarkedReturnedAndARetaliatingPlayerDoesNotAnswerIt")
{
	using namespace CataclysmThornsOfGlassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnThornsPlayer(World);
	ACataclysmEnemyCharacter* Thorned = SpawnThornsCreature(World, FVector(3.0f * ThornsMetre, 0.0f, 0.0f), true);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a thorned creature"), Thorned)
		|| !TestNotNull(TEXT("set-up: the announcer"), Events))
	{
		return false;
	}
	ThornsSet(Player, Combat::GetRetaliationAttribute(), 20.0f);

	// EVERY ANNOUNCED BLOW ON THE PLAYER, with whether it carried the retaliation mark.
	TArray<bool> Marks;
	const FDelegateHandle Listening = Events->OnHit.AddLambda([&Marks, Player](const FCataclysmHitNotice& Notice)
	{
		if (Notice.Target == Player && Notice.Landed > 0.0f)
		{
			Marks.Add(Notice.bRetaliation);
		}
	});
	ON_SCOPE_EXIT { Events->OnHit.Remove(Listening); };

	float CreatureBefore = ThornsHealthOf(Thorned);
	UCataclysmSkillEffects::ApplyHit(Thorned, Player, /*DamagePercent=*/100.0f, FGameplayTagContainer(),
									 FCataclysmHitDelivery());
	const float Answered = CreatureBefore - ThornsHealthOf(Thorned);
	if (!TestTrue(FString::Printf(TEXT("control: the player's retaliation answers the creature's plain blow (%.2f)"),
								  Answered),
				  Answered > 0.0f))
	{
		return false;
	}

	ThornsHeal(Player);
	CreatureBefore = ThornsHealthOf(Thorned);
	const float Sent = ThornsPlayerHits(Player, Thorned);
	TestEqual(TEXT("the creature takes the player's hit and nothing more: the strike back is not answered"),
			  CreatureBefore - ThornsHealthOf(Thorned), Sent, 0.01f);

	if (TestEqual(TEXT("set-up: two blows on the player were announced"), Marks.Num(), 2))
	{
		TestFalse(TEXT("control: the creature's plain blow carries no retaliation mark"), Marks[0]);
		TestTrue(TEXT("the strike back carries it"), Marks[1]);
	}
	return true;
}

/**
 * A HIT THAT KILLS THE CREATURE IS STILL STRUCK BACK. A labelled judgement of issue #2749 (B2), not ruled in words: the
 * attribute set makes the strike back before it writes the creature's health, which is where a death is handled, as
 * retaliation has always been paid. A thorned creature of ten health, a hit of a hundred.
 */
CATACLYSM_THORNS_TEST(FCataclysmThornsOfGlassKillingHitTest,
	"Cataclysm.ThornsOfGlass.AHitThatKillsTheCreatureIsStillStruckBack")
{
	using namespace CataclysmThornsOfGlassTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerCharacter* Player = SpawnThornsPlayer(World);
	ACataclysmEnemyCharacter* Thorned =
		SpawnThornsCreature(World, FVector(3.0f * ThornsMetre, 0.0f, 0.0f), true, /*Health=*/10.0f);
	if (!TestNotNull(TEXT("set-up: a player"), Player) || !TestNotNull(TEXT("set-up: a thorned creature"), Thorned))
	{
		return false;
	}

	const float Before = ThornsHealthOf(Player);
	ThornsPlayerHits(Player, Thorned);
	if (!TestTrue(TEXT("set-up: the hit killed the creature"), UCataclysmSkillEffects::IsDead(Thorned)))
	{
		return false;
	}
	TestEqual(TEXT("and it struck back as it fell, for its attack damage"), Before - ThornsHealthOf(Player),
			  ThornsCreatureAttackDamage, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
