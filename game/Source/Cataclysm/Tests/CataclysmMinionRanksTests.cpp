// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
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
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

/**
 * The Necromancer's ranks of the dead (the Death trees' slice C6d1, 2026-11-25, as ruled): rows that hold while some
 * of the character's own minions stand within reach, Ringed in Bone's immunity to slows read where every slow check
 * reads it, a minion's landed blow announced to its summoner as `minion_hit`, Grave Cold's damage against an enemy
 * carrying Necrosis, Hasty Dead's walk and Grave Pact's damage taken.
 */

namespace CataclysmMinionRanksTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float M = 100.0f;

	/** A row stating `Value` in `Bucket` while at least `Count` of the character's minions stand within 6 metres. */
	FCataclysmStatModifier MinionsWithinSix(ECataclysmStatBucket Bucket, float Value, float Count)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		Row.Condition = ECataclysmStatCondition::MinionsInReachAtLeast;
		Row.ConditionValue = Count;
		Row.ReachMetres = 6.0f;
		return Row;
	}

	/** A plain row with no condition. */
	FCataclysmStatModifier Plain(ECataclysmStatBucket Bucket, float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		return Row;
	}

	/** An `apply_status` action on `minion_hit` naming Necrosis, at `Chance` per cent. */
	FCataclysmPoolAction NecrosisOnMinionHit(float Chance)
	{
		FCataclysmPoolAction Action;
		Action.Event = FName(TEXT("minion_hit"));
		Action.Pool = FName(UCataclysmAbilitySystemComponent::ApplyStatusAction);
		Action.Percent = Chance;
		Action.ApplyStatus = ECataclysmApplyStatus::Chance;
		Action.StatusName = TEXT("Necrosis");
		Action.TriggerKey = FName(TEXT("A_row:apply_status:Necrosis:minion_hit"));
		return Action;
	}

	/** Counts one action event raised on an ability system while it lives. */
	struct FMinionRanksEventCount
	{
		FMinionRanksEventCount(UCataclysmAbilitySystemComponent* InSystem, const TCHAR* InEvent)
			: System(InSystem)
			, Event(InEvent)
		{
			if (System)
			{
				Handle = System->OnActionEvent.AddLambda([this](FName Raised)
				{
					Count += Raised == Event ? 1 : 0;
				});
			}
		}
		~FMinionRanksEventCount()
		{
			if (System)
			{
				System->OnActionEvent.Remove(Handle);
			}
		}
		UCataclysmAbilitySystemComponent* System = nullptr;
		FName Event;
		int32 Count = 0;
		FDelegateHandle Handle;
	};

	/** A bare summoner at the origin with combat and vital sets and a weapon's worth of attack damage. */
	struct FMinionRanksSummoner
	{
		explicit FMinionRanksSummoner(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers, because AddAttributeSetSubobject deduces its type from the argument.
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A monster with a deep pool of health, so no blow below kills it. */
	ACataclysmEnemyCharacter* SpawnSturdyEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Spawned = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Spawned->SetHealth(1'000'000.0f);
		}
		return Spawned;
	}

	float HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	bool CarriesNecrosis(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		const FGameplayTag Necrosis = UCataclysmDebuffs::NecrosisTag();
		return System && Necrosis.IsValid() && System->HasMatchingGameplayTag(Necrosis);
	}

	/** An Imp of `Summoner`'s at `Where`, destroyed with the world. */
	ACataclysmMinion* SpawnImp(AActor* Summoner, const FVector& Where)
	{
		return ACataclysmMinion::Spawn(Summoner, Where, /*Lifetime=*/60.0f, /*bBurns=*/false, TEXT("Imp"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionRanksRingedInBoneTest,
	"Cataclysm.MinionRanks.RingedInBoneHoldsOffASlowWithFiveMinionsWithinSixMetresAndNotWithFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ringed in Bone, as ruled: "While five or more of your minions are within 6 metres of you, you cannot be stunned,
 * slowed or knocked back." Its two rows by hand, as the sheet writes them: `crowd_control_resistance` flat 100 and
 * `immune_to_slow` flat 1, each under `minions_in_reach_at_least` 5 within 6 metres. A player at 5 m a second, held a
 * 15% slow, with four Imps within 3 metres and a fifth at 8: slowed to 4.25, not immune, no resistance. A fifth within
 * reach: immune through `UCataclysmSkillTemplate::IsImmuneTo`, the slow dropped and the walk 5 again, 100 resistance.
 * A 15% haste beside it is kept, 5.75. One Imp gone: four again, and the slow is back beside the haste, 4.8875.
 */
bool FCataclysmMinionRanksRingedInBoneTest::RunTest(const FString&)
{
	using namespace CataclysmMinionRanksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("set-up: an ability system"), AbilitySystem))
	{
		return false;
	}
	AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetMovementSpeedAttribute(), 5.0f);
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a player"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(FName(TEXT("crowd_control_resistance"))).Modifiers = {
		MinionsWithinSix(ECataclysmStatBucket::Flat, 100.0f, 5.0f)};
	Lines.FindOrAdd(FName(TEXT("immune_to_slow"))).Modifiers = {MinionsWithinSix(ECataclysmStatBucket::Flat, 1.0f, 5.0f)};
	AbilitySystem->SetStatInputs(MoveTemp(Lines));

	auto Walk = [Character]()
	{
		Character->RefreshMovementSpeed();
		return Character->GetCharacterMovement()->MaxWalkSpeed / 100.0f;
	};
	auto Resistance = [AbilitySystem]()
	{
		return AbilitySystem->StatForSkill(FName(TEXT("crowd_control_resistance")), FGameplayTagContainer(), 0.0f);
	};

	// FOUR WITHIN 3 METRES, AT THE CORNERS OF A SQUARE, AND A FIFTH AT 8: four count and the fifth does not.
	TArray<ACataclysmMinion*> Near;
	for (const FVector& At : {FVector(3.0f * M, 0, 0), FVector(-3.0f * M, 0, 0), FVector(0, 3.0f * M, 0),
							  FVector(0, -3.0f * M, 0)})
	{
		Near.Add(SpawnImp(Character, At));
	}
	ACataclysmMinion* Far = SpawnImp(Character, FVector(8.0f * M, 0, 0));
	for (const ACataclysmMinion* Imp : Near)
	{
		if (!TestNotNull(TEXT("set-up: an Imp within reach"), Imp))
		{
			return false;
		}
	}
	if (!TestNotNull(TEXT("set-up: an Imp beyond reach"), Far))
	{
		return false;
	}
	TestEqual(TEXT("set-up: five Imps answer to the player"), UCataclysmCommand::ThingsCommandedBy(Character).Num(), 5);

	const float Unslowed = Walk();
	AbilitySystem->HoldAuraShare(ECataclysmAuraShareKind::MovementReduced, Character, 15.0f, 60.0f);
	const float FourSlowed = Walk();
	const bool bFourImmune = UCataclysmSkillTemplate::IsImmuneTo(Character, TEXT("Slow"));
	const float FourResistance = Resistance();

	ACataclysmMinion* Fifth = SpawnImp(Character, FVector(2.0f * M, 2.0f * M, 0));
	if (!TestNotNull(TEXT("set-up: a fifth Imp within reach"), Fifth))
	{
		return false;
	}
	const float FiveSlowed = Walk();
	const bool bFiveImmune = UCataclysmSkillTemplate::IsImmuneTo(Character, TEXT("Slow"));
	const float FiveResistance = Resistance();
	AbilitySystem->HoldAuraShare(ECataclysmAuraShareKind::MovementRaised, PlayerState, 15.0f, 60.0f);
	const float FiveSlowedAndFaster = Walk();

	Near[0]->Destroy();
	const float FourAgainSlowedAndFaster = Walk();
	const bool bFourAgainImmune = UCataclysmSkillTemplate::IsImmuneTo(Character, TEXT("Slow"));

	AddInfo(FString::Printf(TEXT("RINGED IN BONE: %.4f unslowed; four within reach %.4f slowed, immune %s, resistance "
								 "%.1f; five %.4f slowed, immune %s, resistance %.1f, %.4f with a haste; four again "
								 "%.4f, immune %s (metres a second)"),
							Unslowed, FourSlowed, bFourImmune ? TEXT("yes") : TEXT("no"), FourResistance, FiveSlowed,
							bFiveImmune ? TEXT("yes") : TEXT("no"), FiveResistance, FiveSlowedAndFaster,
							FourAgainSlowedAndFaster, bFourAgainImmune ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("set-up: the player walks at 5"), Unslowed, 5.0f, 0.01f);
	TestEqual(TEXT("CONTROL: with four within reach the slow applies, 4.25"), FourSlowed, 4.25f, 0.01f);
	TestFalse(TEXT("CONTROL: and the player is not immune to a slow"), bFourImmune);
	TestEqual(TEXT("CONTROL: nor resists crowd control"), FourResistance, 0.0f, 0.001f);
	TestTrue(TEXT("with five within 6 metres the player is immune to a slow"), bFiveImmune);
	TestEqual(TEXT("so the slow is dropped and the walk is 5"), FiveSlowed, 5.0f, 0.01f);
	TestEqual(TEXT("and crowd control resistance is 100"), FiveResistance, 100.0f, 0.001f);
	TestEqual(TEXT("and a haste held beside it is kept, 5.75"), FiveSlowedAndFaster, 5.75f, 0.01f);
	TestFalse(TEXT("one Imp gone, four again: not immune"), bFourAgainImmune);
	TestEqual(TEXT("and the slow is back beside the haste, 5 x 1.15 x 0.85"), FourAgainSlowedAndFaster, 4.8875f, 0.01f);
	TestFalse(TEXT("CONTROL: immunity to slows is not immunity to stuns"),
			  UCataclysmSkillTemplate::IsImmuneTo(Character, TEXT("Stun")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionRanksThreeOrFiveTest,
	"Cataclysm.MinionRanks.TheMinionCountComparesAtLeastAndCountsOnlyTheCharactersOwnFollowers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `minions_in_reach_at_least` is "at least": Gravekeeper's Hide's armor row holds with three and with four, and not
 * with two. And it counts the character's OWN followers: another summoner's three Imps beside it count for nothing.
 */
bool FCataclysmMinionRanksThreeOrFiveTest::RunTest(const FString&)
{
	using namespace CataclysmMinionRanksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionRanksSummoner Mine(World);
	FMinionRanksSummoner Theirs(World);
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(FName(TEXT("armor"))).Modifiers = {MinionsWithinSix(ECataclysmStatBucket::Flat, 10.0f, 3.0f)};
	Mine.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	auto Armor = [&Mine]()
	{
		return Mine.AbilitySystem->StatForSkill(FName(TEXT("armor")), FGameplayTagContainer(), 0.0f);
	};

	// ANOTHER SUMMONER'S THREE, beside the bare actors at the origin.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!TestNotNull(TEXT("set-up: another summoner's Imp"),
						 SpawnImp(Theirs.Actor, FVector(1.0f * M, (Index - 1) * 2.0f * M, 0))))
		{
			return false;
		}
	}
	const float NoneOfMine = Armor();
	SpawnImp(Mine.Actor, FVector(-2.0f * M, 0, 0));
	SpawnImp(Mine.Actor, FVector(0, -2.0f * M, 0));
	const float TwoOfMine = Armor();
	SpawnImp(Mine.Actor, FVector(0, 2.0f * M, 0));
	const float ThreeOfMine = Armor();
	SpawnImp(Mine.Actor, FVector(-2.0f * M, 2.0f * M, 0));
	const float FourOfMine = Armor();

	AddInfo(FString::Printf(TEXT("ARMOR UNDER THREE MINIONS: %.1f with another's three, %.1f with two of mine, %.1f "
								 "with three, %.1f with four"),
							NoneOfMine, TwoOfMine, ThreeOfMine, FourOfMine));
	TestEqual(TEXT("set-up: the other summoner commands three"), UCataclysmCommand::ThingsCommandedBy(Theirs.Actor).Num(), 3);
	TestEqual(TEXT("another summoner's minions count for nothing"), NoneOfMine, 0.0f, 0.001f);
	TestEqual(TEXT("CONTROL: two of mine are fewer than three"), TwoOfMine, 0.0f, 0.001f);
	TestEqual(TEXT("three of mine hold the row"), ThreeOfMine, 10.0f, 0.001f);
	TestEqual(TEXT("and four, at least three, still hold it"), FourOfMine, 10.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionRanksGraveColdTest,
	"Cataclysm.MinionRanks.AMinionsBlowRaisesMinionHitOnceAndGraveColdsNecrosisRaisesItsDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Cold Hands and Grave Cold, as ruled (P2): a minion's blow raises `minion_hit` on its summoner ONCE per blow, with
 * the enemy struck, and not `hit_dealt` (that is Conduit's). The summoner holds Grave Cold's two rows by hand: an
 * `apply_status` of Necrosis at 100% on `minion_hit`, and `minion_damage` 15 more under `target_carries_necrosis`.
 * Before any blow the multiplier against the enemy is 1; the Imp strikes until the enemy carries Necrosis, and the
 * multiplier against it is then 1.15 while a second, clean enemy's stays 1.
 */
bool FCataclysmMinionRanksGraveColdTest::RunTest(const FString&)
{
	using namespace CataclysmMinionRanksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionRanksSummoner Summoner(World);
	ACataclysmEnemyCharacter* Victim = SpawnSturdyEnemy(World, FVector(4.0f * M, 0, 0));
	ACataclysmEnemyCharacter* Clean = SpawnSturdyEnemy(World, FVector(-4.0f * M, 0, 0));
	ACataclysmMinion* Imp = SpawnImp(Summoner.Actor, FVector(3.0f * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a victim"), Victim) || !TestNotNull(TEXT("set-up: a clean enemy"), Clean)
		|| !TestNotNull(TEXT("set-up: an Imp"), Imp))
	{
		return false;
	}
	// A VICTIM OF 2,000 HEALTH, so its blow of about 220 lands on a living enemy. Until the owner's ruling of 2026-11-26
	// this was needed to clear the tenth of maximum health #917 asked (against the sturdy million no Necrosis could land,
	// which window death3-w2 measured); since that ruling any blow that dealt more than nought carries the ailment.
	Victim->SetHealth(2000.0f);

	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatModifier MoreAgainstNecrosis = Plain(ECataclysmStatBucket::More, 15.0f);
	MoreAgainstNecrosis.Condition = ECataclysmStatCondition::TargetCarriesNecrosis;
	Lines.FindOrAdd(FName(TEXT("minion_damage"))).Modifiers = {MoreAgainstNecrosis};
	Summoner.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	Summoner.AbilitySystem->SetPoolActions({NecrosisOnMinionHit(100.0f)});

	auto Against = [&Summoner, Imp](const AActor* Target)
	{
		return UCataclysmCommand::SummonerMultiplierAgainst(Summoner.Actor, TEXT("minion_damage"), Target, Imp->TypeTags);
	};
	const float Before = Against(Victim);
	const bool bCarriedBefore = CarriesNecrosis(Victim);

	FMinionRanksEventCount MinionHits(Summoner.AbilitySystem, TEXT("minion_hit"));
	FMinionRanksEventCount HitsDealt(Summoner.AbilitySystem, TEXT("hit_dealt"));
	// AN EVADED BLOW LANDS NOTHING, so the Imp strikes until one lands; twenty is room to spare.
	int32 Strikes = 0;
	const float VictimHealth = HealthOf(Victim);
	while (Strikes < 20 && !CarriesNecrosis(Victim))
	{
		Imp->AttackTarget(Victim);
		++Strikes;
	}

	AddInfo(FString::Printf(TEXT("GRAVE COLD: %d strike(s), %d minion_hit, %d hit_dealt; victim %.1f -> %.1f health; "
								 "multiplier %.4f before, %.4f after, %.4f against the clean enemy"),
							Strikes, MinionHits.Count, HitsDealt.Count, VictimHealth, HealthOf(Victim), Before,
							Against(Victim), Against(Clean)));
	TestFalse(TEXT("set-up: the victim carries no Necrosis before the blow"), bCarriedBefore);
	TestEqual(TEXT("CONTROL: against an enemy without Necrosis the multiplier is 1"), Before, 1.0f, 0.001f);
	if (!TestTrue(TEXT("a landed blow of the minion's applied Necrosis through minion_hit"), CarriesNecrosis(Victim)))
	{
		return false;
	}
	TestEqual(TEXT("minion_hit is raised once for each blow"), MinionHits.Count, Strikes);
	TestEqual(TEXT("and hit_dealt not at all: without Conduit the blow is not the summoner's"), HitsDealt.Count, 0);
	TestEqual(TEXT("against the enemy carrying Necrosis, 15% more"), Against(Victim), 1.15f, 0.001f);
	TestEqual(TEXT("CONTROL: and a second enemy without it stays at 1"), Against(Clean), 1.0f, 0.001f);
	TestFalse(TEXT("CONTROL: which carries none"), CarriesNecrosis(Clean));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionRanksNoActionTest,
	"Cataclysm.MinionRanks.WithNoRowOnMinionHitAMinionsBlowAppliesNoNecrosis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The control for the test above: the same Imp and enemy, the summoner holding no action. Five blows, five
 * `minion_hit`, and the enemy carries no Necrosis, so the Necrosis above came from the row and not from the Imp.
 */
bool FCataclysmMinionRanksNoActionTest::RunTest(const FString&)
{
	using namespace CataclysmMinionRanksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionRanksSummoner Summoner(World);
	ACataclysmEnemyCharacter* Victim = SpawnSturdyEnemy(World, FVector(4.0f * M, 0, 0));
	ACataclysmMinion* Imp = SpawnImp(Summoner.Actor, FVector(3.0f * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: a victim"), Victim) || !TestNotNull(TEXT("set-up: an Imp"), Imp))
	{
		return false;
	}
	// THE SAME 2,000-HEALTH VICTIM AS THE TEST ABOVE, so the only difference from it is the row: the Imp's blow could
	// carry an ailment had the summoner held one.
	Victim->SetHealth(2000.0f);
	FMinionRanksEventCount MinionHits(Summoner.AbilitySystem, TEXT("minion_hit"));
	for (int32 Strike = 0; Strike < 5; ++Strike)
	{
		Imp->AttackTarget(Victim);
	}
	TestEqual(TEXT("five blows raise five minion_hit"), MinionHits.Count, 5);
	TestFalse(TEXT("and with no row on it nothing applies Necrosis"), CarriesNecrosis(Victim));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionRanksHastyDeadTest,
	"Cataclysm.MinionRanks.HastyDeadRaisesAFollowersWalkByTheSummonersMinionMovementSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Hasty Dead at 8 points: `minion_movement_speed` 16% increased. A follower's walk is its commander's walk times the
 * share `UCataclysmCommand::CommandedWalkSpeedCmPerSecond` holds, and now times the commander's minion movement speed:
 * 1.16 of what it was. `minion_attack_speed` already had its reader; this slice adds only the row.
 */
bool FCataclysmMinionRanksHastyDeadTest::RunTest(const FString&)
{
	using namespace CataclysmMinionRanksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		PlayerState ? PlayerState->GetCataclysmAbilitySystemComponent() : nullptr;
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: an ability system"), AbilitySystem) || !TestNotNull(TEXT("set-up: a player"), Character))
	{
		return false;
	}
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();
	ACataclysmMinion* Imp = SpawnImp(Character, FVector(3.0f * M, 0, 0));
	if (!TestNotNull(TEXT("set-up: an Imp"), Imp))
	{
		return false;
	}

	const float Before = UCataclysmCommand::CommandedWalkSpeedCmPerSecond(Imp);
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(FName(TEXT("minion_movement_speed"))).Modifiers = {Plain(ECataclysmStatBucket::Increased, 16.0f)};
	AbilitySystem->SetStatInputs(MoveTemp(Lines));
	const float After = UCataclysmCommand::CommandedWalkSpeedCmPerSecond(Imp);

	AddInfo(FString::Printf(TEXT("HASTY DEAD: a follower's walk %.2f cm/s before, %.2f after"), Before, After));
	if (!TestTrue(TEXT("set-up: the follower walks at all"), Before > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("16% increased minion movement speed: 1.16 of the walk"), After / Before, 1.16f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionRanksGravePactTest,
	"Cataclysm.MinionRanks.GravePactMakesTheSummonersMinionsTakeThirtyPercentLess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Grave Pact: `minion_damage_taken` 30 less, read where a blow's damage taken is multiplied. The same enemy strikes
 * the same Imp twice through `UCataclysmSkillEffects::ApplyHit`, without and with the row: the second costs 0.7 of
 * the first. The enemy cannot critically strike, and a blow the Imp evades is struck again.
 */
bool FCataclysmMinionRanksGravePactTest::RunTest(const FString&)
{
	using namespace CataclysmMinionRanksTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FMinionRanksSummoner Summoner(World);
	ACataclysmEnemyCharacter* Striker = SpawnSturdyEnemy(World, FVector(4.0f * M, 0, 0));
	ACataclysmMinion* Imp = SpawnImp(Summoner.Actor, FVector(3.0f * M, 0, 0));
	UAbilitySystemComponent* StrikerSystem = UCataclysmTargeting::AbilitySystemOf(Striker);
	UAbilitySystemComponent* ImpSystem = UCataclysmTargeting::AbilitySystemOf(Imp);
	if (!TestNotNull(TEXT("set-up: a striker"), StrikerSystem) || !TestNotNull(TEXT("set-up: an Imp"), ImpSystem))
	{
		return false;
	}
	StrikerSystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
	StrikerSystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 0.0f);
	ImpSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1'000'000.0f);
	ImpSystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1'000'000.0f);

	auto LandedLoss = [this, Striker, Imp]()
	{
		for (int32 Try = 0; Try < 20; ++Try)
		{
			const float Was = HealthOf(Imp);
			FCataclysmDamageResult Resolved;
			UCataclysmSkillEffects::ApplyHit(Striker, Imp, 100.0f, FGameplayTagContainer(), FCataclysmHitDelivery(),
											 &Resolved);
			if (!Resolved.bEvaded)
			{
				return Was - HealthOf(Imp);
			}
		}
		AddError(TEXT("twenty blows were all evaded"));
		return -1.0f;
	};

	const float Without = LandedLoss();
	TMap<FName, FCataclysmStatInputs> Lines;
	Lines.FindOrAdd(FName(TEXT("minion_damage_taken"))).Modifiers = {Plain(ECataclysmStatBucket::More, -30.0f)};
	Summoner.AbilitySystem->SetStatInputs(MoveTemp(Lines));
	const float With = LandedLoss();

	AddInfo(FString::Printf(TEXT("GRAVE PACT: the Imp lost %.3f without the row, %.3f with it"), Without, With));
	if (!TestTrue(TEXT("set-up: the blow hurt the Imp"), Without > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("30% less damage taken: 0.7 of the loss"), With / Without, 0.7f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmMinionRanksRealRowsTest,
	"Cataclysm.MinionRanks.TheEightNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The real rows of the eight nodes, read from the passive effects table. */
bool FCataclysmMinionRanksRealRowsTest::RunTest(const FString&)
{
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the passive effects table"), Effects))
	{
		return false;
	}
	struct FExpected
	{
		const TCHAR* Node;
		const TCHAR* StatOrAction;
		const TCHAR* Kind;
		float Value;
		const TCHAR* Condition;
		float ConditionValue;
	};
	const FExpected Expected[] = {
		{TEXT("Necromancer_basic_spine_005"), TEXT("max_health"), TEXT("increased"), 1.0f, TEXT(""), 0.0f},
		{TEXT("Necromancer_basic_spine_005"), TEXT("armor"), TEXT("increased"), 1.0f, TEXT("minions_in_reach_at_least"), 3.0f},
		{TEXT("Necromancer_basic_gr_a0"), TEXT("damage_reduction"), TEXT("increased"), 1.0f, TEXT("minions_in_reach_at_least"), 5.0f},
		{TEXT("Necromancer_keystone_gr_kC"), TEXT("immune_to_slow"), TEXT("flat"), 1.0f, TEXT("minions_in_reach_at_least"), 5.0f},
		{TEXT("Necromancer_keystone_spine_002"), TEXT("minion_damage"), TEXT("more"), 15.0f, TEXT("target_carries_necrosis"), 0.0f},
		{TEXT("Necromancer_basic_ch_b0"), TEXT("minion_attack_speed"), TEXT("increased"), 2.0f, TEXT(""), 0.0f},
		{TEXT("Necromancer_basic_ch_b0"), TEXT("minion_movement_speed"), TEXT("increased"), 2.0f, TEXT(""), 0.0f},
		{TEXT("Necromancer_keystone_lg_kC"), TEXT("minion_damage_taken"), TEXT("more"), -30.0f, TEXT(""), 0.0f},
		{TEXT("Necromancer_basic_spine_004"), TEXT("apply_status"), TEXT(""), 4.0f, TEXT(""), 0.0f},
		{TEXT("Necromancer_keystone_spine_002"), TEXT("apply_status"), TEXT(""), 100.0f, TEXT(""), 0.0f},
	};
	for (const FExpected& Want : Expected)
	{
		int32 Found = 0;
		for (const FCataclysmPassiveEffectRow* Row : UCataclysmPassiveTree::EffectsFor(Effects, FName(Want.Node)))
		{
			const bool bAction = FString(Want.StatOrAction) == TEXT("apply_status");
			if (bAction ? Row->Action != Want.StatOrAction : Row->Stat != Want.StatOrAction)
			{
				continue;
			}
			++Found;
			const FString Label = FString::Printf(TEXT("%s %s"), Want.Node, Want.StatOrAction);
			TestEqual(*(Label + TEXT(": the value")), Row->ValuePerPoint, Want.Value, 0.001f);
			TestEqual(*(Label + TEXT(": the condition")), Row->Condition, FString(Want.Condition));
			if (bAction)
			{
				TestEqual(*(Label + TEXT(": on minion_hit")), Row->ActionEvent, FString(TEXT("minion_hit")));
				TestEqual(*(Label + TEXT(": Necrosis")), Row->Ailment, FString(TEXT("Necrosis")));
			}
			else
			{
				TestEqual(*(Label + TEXT(": the kind")), Row->ValueKind, FString(Want.Kind));
			}
			if (FString(Want.Condition) == TEXT("minions_in_reach_at_least"))
			{
				TestEqual(*(Label + TEXT(": the count")), Row->ConditionValue, Want.ConditionValue, 0.001f);
				TestEqual(*(Label + TEXT(": within 6 metres")), Row->ReachMetres, 6.0f, 0.001f);
			}
		}
		TestEqual(*FString::Printf(TEXT("%s %s: one row"), Want.Node, Want.StatOrAction), Found, 1);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
