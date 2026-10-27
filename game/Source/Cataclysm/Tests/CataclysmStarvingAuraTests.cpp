// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmPotions.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyModifiers.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Starving Aura, the Famine modifier "Emit an aura that reduces healing and
 * health regeneration effects by 50%". Issue #2539.
 *
 * IN THE ENEMY MODIFIERS' GROUP, `Cataclysm.EnemyModifiers.`, beside Abyssal
 * Aura's test, whose rig these copy: a creature at the origin, the player placed
 * at a stated distance, and one pulse driven as four quarter-second steps of
 * `UCataclysmEnemyModifiers::AuraStep`.
 *
 * EVERY FIGURE IS A RATIO TO THE SAME CHARACTER'S READING WITH NO AURA, so the
 * class lines a player carries and the size of each heal cancel out. Each heal
 * starts from 100 health of 1000, so none reaches the maximum.
 *
 * THE CLOCK IS NOT RUN except in the lapse test, so no character's own
 * regeneration timer adds to what is measured. Distances are compared with the
 * aura's 6 m and the capsules' radii: the player's is 42 cm, a minion's 30 cm,
 * and the search counts a capsule that overlaps the sphere.
 */
namespace CataclysmStarvingAuraTest
{
	using Vital = UCataclysmVitalAttributeSet;

	/** A metre, in the centimetres everything in Unreal is measured in. */
	constexpr float StarvingMetre = 100.0f;

	constexpr float StarvingMostHealth = 1000.0f;
	constexpr float StarvingWoundedTo = 100.0f;

	/** A player pawn at a place, with the player state its ability system lives on. */
	ACataclysmPlayerCharacter* StarvingPlayerAt(UWorld* World, const FVector& Where)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Pawn =
			World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!State || !Pawn)
		{
			return nullptr;
		}
		Pawn->SetPlayerState(State);
		Pawn->OnRep_PlayerState();
		return Pawn;
	}

	/** A creature of the monsters' side, carrying Starving Aura unless told not to. */
	ACataclysmEnemyCharacter* StarvingCreatureAt(UWorld* World, const FVector& Where,
												 bool bCarriesTheAura = true)
	{
		ACataclysmEnemyCharacter* Creature =
			World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Creature)
		{
			Creature->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Creature->SetHealth(StarvingMostHealth);
			if (bCarriesTheAura)
			{
				Creature->ModifierRows.Add(FName(UCataclysmEnemyModifiers::StarvingAuraRow));
			}
		}
		return Creature;
	}

	/** One full second of the per-character step, which is one pulse. How many it touched. */
	int32 StarvingPulse(AActor* Creature)
	{
		int32 Touched = 0;
		for (int32 Step = 0; Step < 4; ++Step)
		{
			Touched += UCataclysmEnemyModifiers::AuraStep(Creature, 0.25f);
		}
		return Touched;
	}

	float StarvingHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(Vital::GetHealthAttribute()) : -1.0f;
	}

	/** The reduction in force: the current value, which is what `TopUp` reads. */
	float StarvingReductionOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(Vital::GetHealingReceivedReductionAttribute())
					  : -1.0f;
	}

	/** Maximum first and then current, so the write is not clamped to an old maximum. */
	void StarvingWound(const AActor* Actor)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			System->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), StarvingMostHealth);
			System->SetNumericAttributeBase(Vital::GetHealthAttribute(), StarvingWoundedTo);
		}
	}

	/** What one second of health regeneration, at 40 a second, restores. */
	float RestoredByRegeneration(AActor* Character)
	{
		UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Character);
		if (!System)
		{
			return -1.0f;
		}
		StarvingWound(Character);
		System->SetNumericAttributeBase(Vital::GetHealthRegenAttribute(), 40.0f);
		UCataclysmRegeneration::ApplyStep(Character, /*SecondsInStep=*/1.0f,
										  /*SecondsSinceLastDamage=*/60.0f);
		return StarvingHealthOf(Character) - StarvingWoundedTo;
	}

	/** What one potion restores: drunk from a full slot, and paid out over its three seconds. */
	float RestoredByAPotion(AActor* Character)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
		if (!System)
		{
			return -1.0f;
		}
		StarvingWound(Character);
		System->SetPotionCharges(0, UCataclysmPotions::MaxCharges);
		if (UCataclysmPotions::Drink(Character, 0) != ECataclysmPotionRefusal::None)
		{
			return -1.0f;
		}
		for (int32 Step = 0; Step < 12; ++Step)
		{
			UCataclysmPotions::HealStep(Character, 0.25f);
		}
		return StarvingHealthOf(Character) - StarvingWoundedTo;
	}

	/**
	 * What an item row restores: "on a block, restore 10% of maximum health", as
	 * the equipment refresh hands it over. THE THIRD PATH, through
	 * `UCataclysmAbilitySystemComponent::ApplyPoolAction`, which is neither
	 * regeneration, a potion, leech nor a kill heal.
	 */
	float RestoredByABlockRow(AActor* Character)
	{
		UCataclysmAbilitySystemComponent* System =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Character));
		if (!System)
		{
			return -1.0f;
		}
		StarvingWound(Character);
		FCataclysmPoolAction Row;
		Row.Event = FName(TEXT("block"));
		Row.Pool = FName(TEXT("health"));
		Row.Percent = 10.0f;
		Row.Base = ECataclysmPoolActionBase::Maximum;
		TArray<FCataclysmPoolAction> Rows;
		Rows.Add(Row);
		System->SetPoolActions(MoveTemp(Rows));
		System->NoteBlocked();
		System->SetPoolActions(TArray<FCataclysmPoolAction>());
		return StarvingHealthOf(Character) - StarvingWoundedTo;
	}

	/** What a heal of 100 through `TopUp`, the route every heal of health takes, restores. */
	float RestoredThroughTopUp(AActor* Character)
	{
		UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Character);
		if (!System)
		{
			return -1.0f;
		}
		StarvingWound(Character);
		UCataclysmRegeneration::TopUp(*System, Vital::GetHealthAttribute(),
									  Vital::GetMaxHealthAttribute(), /*Gain=*/100.0f,
									  FGameplayTagContainer());
		return StarvingHealthOf(Character) - StarvingWoundedTo;
	}

	/** Inside over outside, or -1 when the outside reading healed nothing to divide by. */
	float StarvingShare(float Inside, float Outside)
	{
		return Outside > 1.0f ? Inside / Outside : -1.0f;
	}
}

#define CATACLYSM_STARVING_AURA_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_STARVING_AURA_TEST(FCataclysmStarvingAuraHalvesEveryHealTest,
	"Cataclysm.EnemyModifiers.StarvingAuraHalvesRegenerationPotionsAndAnItemRestoreOfWhoeverStandsNear")
{
	using namespace CataclysmStarvingAuraTest;

	// THE ROW, AND THE RULING THAT BUILT IT: the debuff "adds the row's Strength
	// to HealingReceivedReduction", which the owner ruled on 2026-09-12 covers
	// every route that restores health. THREE ROUTES ARE MEASURED and not only
	// read: regeneration, a potion, and an item row's restore, which is neither
	// of those nor leech nor a kill heal. The control is the same player 10 m
	// away, outside the 6 m.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TestNotNull(TEXT("set-up: Famine_Starving_Aura is a row of the enemy modifier table"),
				UCataclysmEnemyModifiers::FindRow(UCataclysmEnemyModifiers::LoadEnemyModifierTable(),
												  FName(UCataclysmEnemyModifiers::StarvingAuraRow)));
	const FCataclysmStatusEffectNumbers Row = UCataclysmSkillEffects::NumbersForEffectTag(
		UCataclysmSkillShapes::StatusTagFor(TEXT("Starving Aura")));
	TestEqual(TEXT("set-up: Debuff_Starving_Aura states a strength of 50"), Row.Strength, 50.0f, 0.01f);
	TestEqual(TEXT("set-up: and a duration of 2 seconds"), Row.DurationSeconds, 2.0f, 0.01f);

	ACataclysmEnemyCharacter* Starver = StarvingCreatureAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Player =
		StarvingPlayerAt(World, FVector(10.0f * StarvingMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature carrying Starving Aura"), Starver)
		|| !TestNotNull(TEXT("set-up: a player 10 m away"), Player))
	{
		return false;
	}

	TestEqual(TEXT("10 m away, the pulse catches nobody"), StarvingPulse(Starver), 0);
	const float RegeneratedOutside = RestoredByRegeneration(Player);
	const float PotionOutside = RestoredByAPotion(Player);
	const float RowOutside = RestoredByABlockRow(Player);
	if (!TestTrue(*FString::Printf(TEXT("set-up: outside, each heal restores something (%.2f, %.2f, %.2f)"),
								   RegeneratedOutside, PotionOutside, RowOutside),
				  RegeneratedOutside > 1.0f && PotionOutside > 1.0f && RowOutside > 1.0f))
	{
		return false;
	}

	Player->SetActorLocation(FVector(3.0f * StarvingMetre, 0.0f, 0.0f));
	TestEqual(TEXT("3 m away, the pulse catches the player"), StarvingPulse(Starver), 1);
	TestEqual(TEXT("and adds 50 to the healing the player receives less of"),
			  StarvingReductionOf(Player), 50.0f, 0.01f);

	TestEqual(TEXT("a regeneration step restores half"),
			  StarvingShare(RestoredByRegeneration(Player), RegeneratedOutside), 0.5f, 0.01f);
	TestEqual(TEXT("a potion restores half"),
			  StarvingShare(RestoredByAPotion(Player), PotionOutside), 0.5f, 0.01f);
	TestEqual(TEXT("an item row's restore, the third path, restores half"),
			  StarvingShare(RestoredByABlockRow(Player), RowOutside), 0.5f, 0.01f);

	return true;
}

CATACLYSM_STARVING_AURA_TEST(FCataclysmStarvingAuraLapsesTest,
	"Cataclysm.EnemyModifiers.StarvingAuraLapsesTwoSecondsAfterThePlayerLeaves")
{
	using namespace CataclysmStarvingAuraTest;

	// RE-APPLIED EVERY PULSE FOR TWO SECONDS, so it holds while the player
	// stands in it and lapses two seconds after the player leaves. The clock is
	// run by hand, ending between ticks; while it runs, the creature's own step
	// pulses and catches nobody, because the player is 10 m away by then.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Starver = StarvingCreatureAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Player =
		StarvingPlayerAt(World, FVector(3.0f * StarvingMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature carrying Starving Aura"), Starver)
		|| !TestNotNull(TEXT("set-up: a player 3 m away"), Player))
	{
		return false;
	}

	// THE CONTROL, BEFORE ANY PULSE.
	const float Unstarved = RestoredByRegeneration(Player);
	if (!TestTrue(*FString::Printf(TEXT("set-up: with no aura yet, regeneration restores %.2f"), Unstarved),
				  Unstarved > 1.0f))
	{
		return false;
	}

	TestEqual(TEXT("the pulse catches the player"), StarvingPulse(Starver), 1);
	TestEqual(TEXT("inside, regeneration restores half"),
			  StarvingShare(RestoredByRegeneration(Player), Unstarved), 0.5f, 0.01f);

	Player->SetActorLocation(FVector(10.0f * StarvingMetre, 0.0f, 0.0f));

	CataclysmTestWorld::RunClock(World, 1.5f);
	TestEqual(TEXT("1.5 seconds after leaving, the 50 is still on the player"),
			  StarvingReductionOf(Player), 50.0f, 0.01f);
	TestEqual(TEXT("and regeneration still restores half"),
			  StarvingShare(RestoredByRegeneration(Player), Unstarved), 0.5f, 0.01f);

	CataclysmTestWorld::RunClock(World, 1.0f);
	TestEqual(TEXT("2.5 seconds after leaving, the 50 has lapsed"),
			  StarvingReductionOf(Player), 0.0f, 0.01f);
	TestEqual(TEXT("and regeneration restores in full again"),
			  StarvingShare(RestoredByRegeneration(Player), Unstarved), 1.0f, 0.01f);

	return true;
}

CATACLYSM_STARVING_AURA_TEST(FCataclysmStarvingAuraDoesNotStackTest,
	"Cataclysm.EnemyModifiers.TwoStarvingCreaturesStillHalveHealingAndNoMore")
{
	using namespace CataclysmStarvingAuraTest;

	// RULED AS A JUDGEMENT, 2026-10-26: two Starving creatures do not stack. One
	// single-stack effect, which stays at 50, as Abyssal Aura's does. The second
	// creature's own reach is shown first, so "still 50" is not a creature that
	// never reached the player.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE PLAYER 3 m FROM THE ONE AND 5 m FROM THE OTHER, both inside 6 m; the
	// two creatures 4 m apart.
	ACataclysmEnemyCharacter* First = StarvingCreatureAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Second =
		StarvingCreatureAt(World, FVector(0.0f, 4.0f * StarvingMetre, 0.0f));
	ACataclysmPlayerCharacter* Player =
		StarvingPlayerAt(World, FVector(3.0f * StarvingMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: the first Starving creature"), First)
		|| !TestNotNull(TEXT("set-up: the second Starving creature"), Second)
		|| !TestNotNull(TEXT("set-up: a player near both"), Player))
	{
		return false;
	}
	const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Player);
	if (!TestNotNull(TEXT("set-up: the player has an ability system"), System))
	{
		return false;
	}
	const FGameplayTag StarvingTag = UCataclysmSkillShapes::StatusTagFor(TEXT("Starving Aura"));

	const float Unstarved = RestoredByRegeneration(Player);
	if (!TestTrue(*FString::Printf(TEXT("set-up: with no aura yet, regeneration restores %.2f"), Unstarved),
				  Unstarved > 1.0f))
	{
		return false;
	}

	TestEqual(TEXT("the second creature alone, 5 m away, catches the player"), StarvingPulse(Second), 1);
	TestEqual(TEXT("and puts 50 on the player"), StarvingReductionOf(Player), 50.0f, 0.01f);

	TestEqual(TEXT("the first creature then catches the player too"), StarvingPulse(First), 1);
	TestEqual(TEXT("and the reduction is still 50, not 100"), StarvingReductionOf(Player), 50.0f, 0.01f);
	TestEqual(TEXT("the player carries one Starving Aura effect"), System->GetTagCount(StarvingTag), 1);
	TestEqual(TEXT("and regeneration restores half, not nothing"),
			  StarvingShare(RestoredByRegeneration(Player), Unstarved), 0.5f, 0.01f);

	return true;
}

CATACLYSM_STARVING_AURA_TEST(FCataclysmStarvingAuraAddsToTheFloorTest,
	"Cataclysm.EnemyModifiers.StarvingAuraAddsToAFloorRulesShareAndTheSumRestoresNothing")
{
	using namespace CataclysmStarvingAuraTest;

	// RULED, 2026-10-26: it adds to the floor rules' reduction, and `TopUp`
	// clamps the total at 100. THE FLOOR'S 50 IS WRITTEN AS THE FLOOR WRITES IT:
	// Death's Embrace and Necrotic Ground reach the attribute as its base,
	// through the stat refresh's `SetNumericAttributeBase`. So a refresh after
	// the aura landed is written again, and must not take the aura's share off.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Starver = StarvingCreatureAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Player =
		StarvingPlayerAt(World, FVector(3.0f * StarvingMetre, 0.0f, 0.0f));
	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Player);
	if (!TestNotNull(TEXT("set-up: a creature carrying Starving Aura"), Starver)
		|| !TestNotNull(TEXT("set-up: a player 3 m away"), Player)
		|| !TestNotNull(TEXT("set-up: the player has an ability system"), System))
	{
		return false;
	}
	const FGameplayAttribute Reduction = Vital::GetHealingReceivedReductionAttribute();

	const float Unstarved = RestoredByRegeneration(Player);
	if (!TestTrue(*FString::Printf(TEXT("set-up: with nothing on the player, regeneration restores %.2f"),
								   Unstarved),
				  Unstarved > 1.0f))
	{
		return false;
	}

	System->SetNumericAttributeBase(Reduction, 50.0f);
	TestEqual(TEXT("the floor's 50 alone halves regeneration"),
			  StarvingShare(RestoredByRegeneration(Player), Unstarved), 0.5f, 0.01f);

	TestEqual(TEXT("the pulse catches the player"), StarvingPulse(Starver), 1);
	TestEqual(TEXT("the aura's 50 adds to the floor's: 100 in force"),
			  StarvingReductionOf(Player), 100.0f, 0.01f);
	TestEqual(TEXT("and the floor's 50 is still the base, untouched by the aura"),
			  System->GetNumericAttributeBase(Reduction), 50.0f, 0.01f);
	TestEqual(TEXT("so regeneration restores nothing"), RestoredByRegeneration(Player), 0.0f, 0.01f);
	TestEqual(TEXT("and neither does a potion"), RestoredByAPotion(Player), 0.0f, 0.01f);

	// THE FLOOR'S REFRESH WRITES ITS SHARE AGAIN, which must not erase the aura's.
	System->SetNumericAttributeBase(Reduction, 50.0f);
	TestEqual(TEXT("after the floor writes its 50 again, 100 is still in force"),
			  StarvingReductionOf(Player), 100.0f, 0.01f);

	return true;
}

CATACLYSM_STARVING_AURA_TEST(FCataclysmStarvingAuraMedicTest,
	"Cataclysm.EnemyModifiers.AMedicCarryingStarvingAuraReducesNobody")
{
	using namespace CataclysmStarvingAuraTest;

	// A CREATURE THAT TAKES NO HOSTILE ACTION STARVES NOBODY, the gate Hellfire
	// and Abyssal Aura already take (issue #1680). THE CONTROL: the same
	// creature, before it is made the floor's medic, catches a player.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Starver = StarvingCreatureAt(World, FVector::ZeroVector);
	ACataclysmPlayerCharacter* Caught =
		StarvingPlayerAt(World, FVector(3.0f * StarvingMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature carrying Starving Aura"), Starver)
		|| !TestNotNull(TEXT("set-up: a player 3 m away"), Caught))
	{
		return false;
	}

	TestEqual(TEXT("an ordinary Starving creature catches the player"), StarvingPulse(Starver), 1);
	TestEqual(TEXT("and puts 50 on them"), StarvingReductionOf(Caught), 50.0f, 0.01f);

	// A SECOND PLAYER, 3 m THE OTHER WAY, arriving after that pulse.
	ACataclysmPlayerCharacter* Spared =
		StarvingPlayerAt(World, FVector(-3.0f * StarvingMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a second player 3 m the other way"), Spared))
	{
		return false;
	}
	const float SparedBefore = RestoredByRegeneration(Spared);
	if (!TestTrue(*FString::Printf(TEXT("set-up: before the medic's pulse, regeneration restores %.2f"),
								   SparedBefore),
				  SparedBefore > 1.0f))
	{
		return false;
	}

	// ONE THING CHANGES.
	Starver->bHealsAlliesForTheFloorRule = true;
	Starver->SecondsSinceAuraPulse = 0.0f;
	if (!TestTrue(TEXT("set-up: the floor's medic takes no hostile action"), Starver->TakesNoHostileAction()))
	{
		return false;
	}

	TestEqual(TEXT("the same creature as the floor's medic catches nobody"), StarvingPulse(Starver), 0);
	TestEqual(TEXT("and puts nothing on the second player"), StarvingReductionOf(Spared), 0.0f, 0.01f);
	TestEqual(TEXT("whose regeneration restores in full"),
			  StarvingShare(RestoredByRegeneration(Spared), SparedBefore), 1.0f, 0.01f);

	return true;
}

CATACLYSM_STARVING_AURA_TEST(FCataclysmStarvingAuraMinionTest,
	"Cataclysm.EnemyModifiers.StarvingAuraReachesThePlayersMinionAndNotTheCreaturesAlly")
{
	using namespace CataclysmStarvingAuraTest;

	// RULED AS A JUDGEMENT, 2026-10-26: it reaches whatever the creature's sphere
	// search counts as its enemy, the player and the player's minions, as Abyssal
	// Aura does, and not the creature's own allies. THE PLAYER IS 10 m AWAY, so
	// the minion is caught for being the player's and not for being the player.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmEnemyCharacter* Starver = StarvingCreatureAt(World, FVector::ZeroVector);
	ACataclysmEnemyCharacter* Ally =
		StarvingCreatureAt(World, FVector(0.0f, -3.0f * StarvingMetre, 0.0f), /*bCarriesTheAura=*/false);
	ACataclysmPlayerCharacter* Player =
		StarvingPlayerAt(World, FVector(10.0f * StarvingMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a creature carrying Starving Aura"), Starver)
		|| !TestNotNull(TEXT("set-up: its ally 3 m away, carrying nothing"), Ally)
		|| !TestNotNull(TEXT("set-up: a player 10 m away"), Player))
	{
		return false;
	}
	ACataclysmMinion* Minion = ACataclysmMinion::Spawn(
		Player, FVector(0.0f, 3.0f * StarvingMetre, 0.0f), /*Lifetime=*/20.0f, /*bBurns=*/false);
	ON_SCOPE_EXIT { if (IsValid(Minion)) { Minion->Destroy(); } };
	if (!TestNotNull(TEXT("set-up: the player's minion 3 m from the creature"), Minion))
	{
		return false;
	}

	const TArray<AActor*> Enemies = UCataclysmTargeting::FindEnemiesInSphere(
		World, Starver, Starver->GetActorLocation(), UCataclysmEnemyModifiers::AuraRadiusCm);
	if (!TestTrue(TEXT("set-up: the creature's search counts the minion as its enemy"), Enemies.Contains(Minion))
		|| !TestFalse(TEXT("set-up: and not its ally"), Enemies.Contains(Ally)))
	{
		return false;
	}

	const float MinionBefore = RestoredThroughTopUp(Minion);
	const float AllyBefore = RestoredThroughTopUp(Ally);
	if (!TestTrue(*FString::Printf(TEXT("set-up: with no aura yet, a heal restores %.2f to the minion and %.2f "
										"to the ally"), MinionBefore, AllyBefore),
				  MinionBefore > 1.0f && AllyBefore > 1.0f))
	{
		return false;
	}

	TestEqual(TEXT("the pulse catches the minion alone"), StarvingPulse(Starver), 1);
	TestEqual(TEXT("the minion carries the 50"), StarvingReductionOf(Minion), 50.0f, 0.01f);
	TestEqual(TEXT("and is healed half"), StarvingShare(RestoredThroughTopUp(Minion), MinionBefore), 0.5f, 0.01f);
	TestEqual(TEXT("the creature's ally carries nothing"), StarvingReductionOf(Ally), 0.0f, 0.01f);
	TestEqual(TEXT("and is healed in full"), StarvingShare(RestoredThroughTopUp(Ally), AllyBefore), 1.0f, 0.01f);
	TestEqual(TEXT("and the player 10 m away carries nothing"), StarvingReductionOf(Player), 0.0f, 0.01f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
