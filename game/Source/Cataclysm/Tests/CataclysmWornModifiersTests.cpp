// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * Wearer of the Dead (the Death trees' slice C4d3, 2026-11-25): killing an Elite or stronger creature lends its modifiers
 * for 20 seconds, for the five built ones that are a plain number on a player stat, as ruled. Three tests record by hand;
 * one goes through a real player's kill handler, on the scene `Cataclysm.Swarm.` builds; one reads the real rows.
 */

namespace CataclysmWornModifiersTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;
	using CataclysmTestWorld::RunClock;

	constexpr float WornMetre = 100.0f;
	constexpr int32 OverpoweredPlace = 2;

	/** A character on the players' side with 1,000 health, able to be given stat lines. */
	UCataclysmAbilitySystemComponent* WearerAt(UWorld* World, const FVector& Where, AActor** OutActor = nullptr)
	{
		ACataclysmEnemyCharacter* Holder = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (!Holder)
		{
			return nullptr;
		}
		Holder->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Players));
		Holder->SetAttackDamage(0.0f);
		Holder->SetHealth(1000.0f);
		if (OutActor)
		{
			*OutActor = Holder;
		}
		return Cast<UCataclysmAbilitySystemComponent>(Holder->GetAbilitySystemComponent());
	}

	/** A creature of the monsters' side at this rung, carrying exactly these modifier rows. */
	ACataclysmEnemyCharacter* CarrierOf(UWorld* World, const FVector& At, int32 Rung, TArray<FName> Rows,
										float Health = 1000.0f)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(Health);
			// THE RUNG AND THE ROWS ALONE: `SetRarityStep` would also scale the creature and draw modifiers of its own.
			Made->RarityStep = Rung;
			Made->ModifierRows = MoveTemp(Rows);
		}
		return Made;
	}

	FCataclysmStatModifier WornRow(ECataclysmStatBucket Bucket, float Value, int32 Place)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = ECataclysmStatCondition::WearingEnemyModifier;
		Row.ConditionValue = static_cast<float>(Place);
		return Row;
	}

	/** Crit chance at a base of 5 with Overpowered's worn row, and life leech with Bloodthirsty's. */
	void WearOverpoweredAndBloodthirsty(UCataclysmAbilitySystemComponent* System)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		FCataclysmStatInputs& Crit = Lines.Add(FName(TEXT("crit_chance")));
		Crit.Base = 5.0f;
		Crit.Modifiers = {WornRow(ECataclysmStatBucket::Flat, 100.0f, OverpoweredPlace)};
		Lines.Add(FName(TEXT("life_leech"))).Modifiers = {WornRow(ECataclysmStatBucket::Flat, 10.0f, 3)};
		Lines.Add(FName(TEXT("attack_damage"))).Base = 100.0f;
		System->SetStatInputs(MoveTemp(Lines));
	}

	float StatOf(const UCataclysmAbilitySystemComponent* System, const TCHAR* Stat)
	{
		return System->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f);
	}

	const FName Overpowered(TEXT("Generic_Overpowered"));
	const FName TitanicResolve(TEXT("Generic_Titanic_Resolve"));
	const FName ThornsOfGlass(TEXT("Generic_Thorns_of_Glass"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWornModifiersTwentySecondsTest,
	"Cataclysm.WornModifiers.AnElitesWornModifierLastsTwentySecondsAndOnlyOnesWithARowAreWorn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character holding Overpowered's and Bloodthirsty's worn rows kills an Elite carrying Overpowered and Thorns of Glass:
 * one modifier is worn, Overpowered, and its crit chance of 5 reads 105 for 20 seconds; life leech stays 0, Bloodthirsty
 * not being carried; Thorns of Glass, which has no place, is not worn. At 19.5 seconds it still reads 105, at 20.5 it
 * reads 5. A second kill at 10 seconds sets the full 20 again, never stacking. THE CONTROLS: a Common creature carrying
 * Overpowered lends nothing; and a character with no worn row records nothing.
 */
bool FCataclysmWornModifiersTwentySecondsTest::RunTest(const FString&)
{
	using namespace CataclysmWornModifiersTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UCataclysmAbilitySystemComponent* System = WearerAt(World, FVector::ZeroVector);
	UCataclysmAbilitySystemComponent* Bare = WearerAt(World, FVector(0, 30 * WornMetre, 0));
	ACataclysmEnemyCharacter* Elite = CarrierOf(World, FVector(5 * WornMetre, 0, 0), 1, {Overpowered, ThornsOfGlass});
	ACataclysmEnemyCharacter* Common = CarrierOf(World, FVector(-5 * WornMetre, 0, 0), 0, {Overpowered});
	if (!TestNotNull(TEXT("set-up: a wearer"), System) || !TestNotNull(TEXT("set-up: a character without the vow"), Bare)
		|| !Elite || !Common)
	{
		AddError(TEXT("set-up: two creatures"));
		return false;
	}
	WearOverpoweredAndBloodthirsty(System);

	TestEqual(TEXT("control: a Common creature lends nothing"), UCataclysmFervour::WearTheModifiersOf(System, Common), 0);
	TestEqual(TEXT("control: a character with no worn row records nothing"),
			  UCataclysmFervour::WearTheModifiersOf(Bare, Elite), 0);
	TestTrue(TEXT("control: and shows nothing"), UCataclysmFervour::WornModifiersLine(Bare).IsEmpty());
	TestEqual(TEXT("set-up: before the kill, crit chance 5"), StatOf(System, TEXT("crit_chance")), 5.0f, 0.001f);

	TestEqual(TEXT("the Elite lends one modifier: Overpowered"), UCataclysmFervour::WearTheModifiersOf(System, Elite), 1);
	TestEqual(TEXT("worn: crit chance 105"), StatOf(System, TEXT("crit_chance")), 105.0f, 0.001f);
	TestEqual(TEXT("Bloodthirsty is not carried, so life leech stays 0"), StatOf(System, TEXT("life_leech")), 0.0f,
			  0.001f);
	TestEqual(TEXT("the screen shows it with its seconds"), UCataclysmFervour::WornModifiersLine(System),
			  FString(TEXT("Worn: Overpowered 20s")));

	RunClock(World, 10.0f);
	UCataclysmFervour::WearTheModifiersOf(System, Elite);
	TestEqual(TEXT("a second kill at 10 seconds sets the full 20 again"),
			  System->WornEnemyModifierSecondsLeft(OverpoweredPlace), 20.0f, 0.01f);
	RunClock(World, 19.5f);
	TestEqual(TEXT("19.5 seconds after that, still 105"), StatOf(System, TEXT("crit_chance")), 105.0f, 0.001f);
	RunClock(World, 1.0f);
	TestEqual(TEXT("20.5 seconds after it, 5 again"), StatOf(System, TEXT("crit_chance")), 5.0f, 0.001f);
	TestTrue(TEXT("and the line is empty"), UCataclysmFervour::WornModifiersLine(System).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWornModifiersTitanicTest,
	"Cataclysm.WornModifiers.TitanicResolveRaisesMaximumHealthAtOnceAndTheStepAfterTwentySecondsLowersIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Titanic Resolve worn: a maximum of 1,000 with its row, 50% more, is 1,500 at the moment of the kill, not at the next
 * regeneration step. At 20.5 seconds the next regeneration step writes 1,000 again. THE CONTROL: before the kill a
 * regeneration step leaves it at 1,000.
 */
bool FCataclysmWornModifiersTitanicTest::RunTest(const FString&)
{
	using namespace CataclysmWornModifiersTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AActor* WearerActor = nullptr;
	UCataclysmAbilitySystemComponent* System = WearerAt(World, FVector::ZeroVector, &WearerActor);
	ACataclysmEnemyCharacter* Elite = CarrierOf(World, FVector(5 * WornMetre, 0, 0), 1, {TitanicResolve});
	if (!TestNotNull(TEXT("set-up: a wearer"), System) || !Elite)
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& Health = Lines.Add(FName(TEXT("max_health")));
	Health.Base = 1000.0f;
	Health.Modifiers = {WornRow(ECataclysmStatBucket::More, 50.0f, 1)};
	System->SetStatInputs(MoveTemp(Lines));
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	UCataclysmRegeneration::ApplyStep(WearerActor, 1.0f, 100.0f);
	TestEqual(TEXT("control: before the kill a step leaves 1,000"), System->GetNumericAttribute(MaxHealth), 1000.0f,
			  0.01f);
	UCataclysmFervour::WearTheModifiersOf(System, Elite);
	TestEqual(TEXT("worn: 1,500 at once"), System->GetNumericAttribute(MaxHealth), 1500.0f, 0.01f);
	RunClock(World, 20.5f);
	UCataclysmRegeneration::ApplyStep(WearerActor, 1.0f, 100.0f);
	TestEqual(TEXT("20.5 seconds on, a step writes 1,000 again"), System->GetNumericAttribute(MaxHealth), 1000.0f,
			  0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWornModifiersHandlerTest,
	"Cataclysm.WornModifiers.ARealPlayersKillOfAnEliteLendsItsModifierThroughTheKillHandler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole path, on the scene `Cataclysm.Swarm.` builds: a real player holding Overpowered's worn row kills an Elite
 * carrying Overpowered with a blow, and its crit chance of 5 reads 105. THE CONTROL: the same kill of a Common creature
 * carrying Overpowered leaves it at 5.
 */
bool FCataclysmWornModifiersHandlerTest::RunTest(const FString&)
{
	using namespace CataclysmWornModifiersTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Player =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a player state"), State) || !TestNotNull(TEXT("set-up: a player"), Player))
	{
		return false;
	}
	Player->SetPlayerState(State);
	Player->OnRep_PlayerState();
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	if (!TestNotNull(TEXT("set-up: the player's ability system"), System))
	{
		return false;
	}
	System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
	WearOverpoweredAndBloodthirsty(System);
	ACataclysmEnemyCharacter* Common = CarrierOf(World, FVector(3 * WornMetre, 3 * WornMetre, 0), 0, {Overpowered}, 1.0f);
	ACataclysmEnemyCharacter* Elite = CarrierOf(World, FVector(3 * WornMetre, 0, 0), 1, {Overpowered}, 1.0f);
	if (!Common || !Elite)
	{
		AddError(TEXT("set-up: two creatures"));
		return false;
	}
	const auto Blow = [Player](AActor* Target)
	{
		UCataclysmStrikeSkill* Swing = NewObject<UCataclysmStrikeSkill>(Player);
		Swing->SkillName = TEXT("Test Swing");
		Swing->SkillTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Strike"))));
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 0.0f;
		Delivery.Skill = Swing;
		UCataclysmSkillEffects::ApplyHit(Player, Target, /*DamagePercent=*/100.0f, Swing->SkillTags, Delivery);
	};

	Blow(Common);
	if (!TestTrue(TEXT("set-up: the blow killed the Common creature"), UCataclysmSkillEffects::IsDead(Common)))
	{
		return false;
	}
	TestEqual(TEXT("control: a Common creature's kill lends nothing: 5"), StatOf(System, TEXT("crit_chance")), 5.0f,
			  0.001f);
	Blow(Elite);
	if (!TestTrue(TEXT("set-up: the blow killed the Elite"), UCataclysmSkillEffects::IsDead(Elite)))
	{
		return false;
	}
	TestEqual(TEXT("the Elite's kill lends Overpowered: 105"), StatOf(System, TEXT("crit_chance")), 105.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWornModifiersRealRowsTest,
	"Cataclysm.WornModifiers.TheVowsFiveRowsAreTheModifiersTheyCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real rows, read from the passive effects table: the Soul Collector's 200 vow, option 1, has one row for each place
 * of `UCataclysmFervour::WornEnemyModifierRows`, each under `wearing_enemy_modifier` at its place, with the figure of the
 * modifier it copies.
 */
bool FCataclysmWornModifiersRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Stat;
		const TCHAR* Kind;
		float Value;
	};
	const FExpected Expected[] = {{TEXT("max_health"), TEXT("more"), 50.0f},
								  {TEXT("crit_chance"), TEXT("flat"), 100.0f},
								  {TEXT("life_leech"), TEXT("flat"), 10.0f},
								  {TEXT("crowd_control_resistance"), TEXT("flat"), 100.0f},
								  {TEXT("health_regen"), TEXT("flat"), 2.0f}};
	TestEqual(TEXT("the engine's list has five places"), UCataclysmFervour::WornEnemyModifierRows().Num(), 5);
	int32 Found = 0;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(Effects, FName(TEXT("SoulCollector_capstone_200"))))
	{
		if (Row->Option != 1)
		{
			continue;
		}
		++Found;
		TestEqual(TEXT("each worn row waits on a worn modifier"), Row->Condition,
				  FString(TEXT("wearing_enemy_modifier")));
		const int32 Place = FMath::RoundToInt(Row->ConditionValue);
		if (!TestTrue(TEXT("its place is in the list"), Place >= 1 && Place <= 5))
		{
			continue;
		}
		const FExpected& Want = Expected[Place - 1];
		TestEqual(*FString::Printf(TEXT("place %d: the stat"), Place), Row->Stat, FString(Want.Stat));
		TestEqual(*FString::Printf(TEXT("place %d: the kind"), Place), Row->ValueKind, FString(Want.Kind));
		TestEqual(*FString::Printf(TEXT("place %d: the figure"), Place), Row->ValuePerPoint, Want.Value, 0.001f);
	}
	TestEqual(TEXT("five worn rows"), Found, 5);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
