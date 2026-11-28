// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRisenImps.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Every minion, gadget and trap the player makes holds part of the class resource back while it stands. Issue #2787,
 * branch A, from the owner's ruling of 2026-11-20: "All gadgets/traps/minions should hold some reserve. This amount
 * should be different based on what is being used, but they all should reserve some." The figures are the coordinating
 * session's accepted judgements: a risen imp reserves Summon Imp's 10, read from Summon Imp's row; a necrosis imp 5; a
 * Warhound 5; a Tyrant's Minion 10; a skill gadget 15; a skill trap 5. A thrall's 30 and a summoned imp's 10 were
 * reserved before and are tested in `Cataclysm.FervourReserve.`.
 *
 * WHAT IS CHECKED, through the function each kind is made by: each holds its figure back the moment it exists and the
 * spendable Fervour is cut at once; each is refused, with nothing made, when the pool cannot hold it; a second Warhound,
 * Tyrant's Minion or trap stands beside the first and reserves again, and a gadget's move holds nothing more; Crowned
 * takes 5 off every kind, never below 1; nothing is held once everything is gone; and the resource bar is drawn for a
 * character that reserves anything, generator or not.
 *
 * BRANCH B OF ISSUE #2787 CHANGED THREE OF THEM. The owner's ruling of 2026-11-20 removed every count cap on what reserves
 * the class resource, so tests 5, 6 and 8 no longer read a refresh of the one Warhound or Tyrant's Minion standing, or
 * the trap cap of five; they read a second standing beside the first, and the pool refusing the one after it fills.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER WITH ITS PLAYER STATE, so its pool is the real class resource set. The
 * stats that switch each kind on are written by hand, a stat line per call replacing the last, as an item or a node
 * would build it. Nothing waits on a tick. Registered under `Cataclysm.KindReserves.`, which no other group's name
 * begins with.
 *
 * AND `Cataclysm.NoCaps.`, BRANCH B'S GROUP, at the end of this file because it reads the same player: no count caps on
 * what reserves the class resource (a fourth imp, a risen imp beside three), and the percentage that replaced the cap
 * bonuses, `UCataclysmCommand::MinionReservePercentLessStat`, at each end it can take, added across rows and floored.
 */
namespace CataclysmKindReservesTest
{
	/** Centimetres in a metre. */
	constexpr float KindM = 100.0f;

	/** One flat stat for `KindHolds`. */
	struct FKindStat
	{
		const TCHAR* Stat;
		float Value;
		/** The row's one required tag, or null for none. */
		const TCHAR* RequiredTag = nullptr;
	};

	/** A possessed player character, its player state and that state's ability system. */
	struct FKindPlayer
	{
		FKindPlayer(UWorld* World, const FVector& Where)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			System = State->GetCataclysmAbilitySystemComponent();
		}

		bool IsUsable() const { return Character && System; }

		/** REPLACES THE WHOLE STAT LINE with these flat rows. */
		void Holds(const TArray<FKindStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			for (const FKindStat& Each : Rows)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::Enchantment;
				Row.Value = Each.Value;
				if (Each.RequiredTag)
				{
					Row.RequiredTags.AddTag(
						FGameplayTag::RequestGameplayTag(FName(Each.RequiredTag), /*ErrorIfNotFound=*/false));
				}
				FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Each.Stat));
				Line.Base = 0.0f;
				Line.Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		/** A pool of `Maximum`, held full. Written after `Holds`, which does not touch it. */
		void Pool(float Maximum) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Maximum);
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Maximum);
		}

		/** What its pool's reserve totals. */
		float Reserved() const { return UCataclysmCommand::ReservedFervourOf(Character); }

		/** What it may spend: the maximum less the reserve. */
		float Spendable() const
		{
			const UCataclysmClassResourceAttributeSet* Resource = System->GetSet<UCataclysmClassResourceAttributeSet>();
			return Resource ? Resource->MaximumClassResourceAsked() : -1.0f;
		}

		/** The Fervour it holds now. */
		float Held() const
		{
			return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		/** How many things it commands, the dead left out. */
		int32 Commanded() const { return UCataclysmCommand::ThingsCommandedBy(Character).Num(); }

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** The rule the Ritualist's first node grants: an enemy dying with a curse this character laid rises as an imp. */
	const FKindStat KindRisesOnCursedDeath{UCataclysmRisenImps::RisesStat, 1.0f};

	/** The rule an enchantment grants: an enemy killed by necrosis rises as an imp for this many seconds. */
	const FKindStat KindRisesOnNecrosisKill{UCataclysmRisenImps::NecrosisRiseSecondsStat, 5.0f};

	/** Warlord's Will's six-piece seconds, which switch the Warhound on. */
	const FKindStat KindWarhoundSeconds{ACataclysmMinion::WarhoundSecondsStat, 10.0f};

	/** Tyrant's Chains' six-piece seconds, which switch the Tyrant's Minion on. */
	const FKindStat KindTyrantsSeconds{ACataclysmMinion::TyrantsMinionSecondsStat, 10.0f};

	/** Crowned's row: "Each minion reserves 5 less Fervour, never less than 1." */
	const FKindStat KindCrowned{UCataclysmCommand::MinionReserveReductionStat, 5.0f};

	/** An enemy at `Where`, with health enough to be cursed before it is killed. */
	ACataclysmEnemyCharacter* KindEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetHealth(1000.0f);
		}
		return Made;
	}

	/**
	 * An enemy at `Where` dies carrying `Player`'s Shred, the curse the owner's Conflagration aura laid in play. Returns
	 * how many imps `Player` holds afterwards, or -1 when the set-up failed.
	 */
	int32 KindCursedDeath(FAutomationTestBase& Test, UWorld* World, const FKindPlayer& Player, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Victim = KindEnemy(World, Where);
		const FGameplayTag Shred =
			FGameplayTag::RequestGameplayTag(FName(TEXT("Status.Debuff.Shred")), /*ErrorIfNotFound=*/false);
		if (!Test.TestNotNull(TEXT("set-up: a creature to curse"), Victim)
			|| !Test.TestTrue(TEXT("set-up: the player lays Shred on it"),
				   UCataclysmSkillEffects::ApplyTagForDuration(Player.Character, Victim, Shred, 10.0f)))
		{
			return -1;
		}
		UCataclysmTargeting::AbilitySystemOf(Victim)->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		if (!Test.TestTrue(TEXT("set-up: the creature is recorded as dead"), UCataclysmSkillEffects::IsDead(Victim)))
		{
			return -1;
		}
		return UCataclysmCommand::MinionsOfTypeCommandedBy(Player.Character, TEXT("Imp")).Num();
	}

	/** An imp raised for `Player` by a necrosis kill of a fresh enemy at `Where`, or null. */
	ACataclysmMinion* KindNecrosisImp(UWorld* World, const FKindPlayer& Player, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Victim = KindEnemy(World, Where);
		FGameplayTagContainer Killing;
		Killing.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Keyword.DoT.Necrosis")), /*ErrorIfNotFound=*/false));
		return Victim ? UCataclysmRisenImps::RiseOnNecrosisKill(Player.Character, Victim, Where, &Killing) : nullptr;
	}

	/** Kill `Follower` through its health, as a blow would, so it leaves what its commander commands. */
	void KindKill(AActor* Follower)
	{
		if (UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Follower))
		{
			Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		}
	}

	/** A follower's own reserve, or -1 for none. */
	float KindReserveOf(const ACataclysmMinion* Minion)
	{
		return Minion ? Minion->ReservedFervour : -1.0f;
	}
}

// ==========================================================================
// R1. A risen imp reserves Summon Imp's 10, at once, and frees it when it dies
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesRisenImpTest,
	"Cataclysm.KindReserves.ARisenImpReservesSummonImpsTenAtOnceAndFreesItWhenItDies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner's case: a Ritualist whose cursed enemies rise as imps. The figure is Summon Imp's row's, read from the
 * shipped table, so a re-tune of the row moves both. The excess Fervour is cut at once and not given back when the imp
 * dies, the rule of 2026-09-30.
 */
bool FCataclysmKindReservesRisenImpTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const TArray<FCataclysmWeaponSkill> Staff = UCataclysmWeaponSkills::SkillsFor(
		UCataclysmWeaponSkills::LoadGeneratedTable(), UCataclysmRisenImps::ImpSkillWeapon,
		UCataclysmRisenImps::ImpSkillDamageType);
	const FCataclysmWeaponSkill* SummonImp = Staff.FindByPredicate(
		[](const FCataclysmWeaponSkill& Skill) { return Skill.Name == UCataclysmRisenImps::ImpSkillName; });
	if (!TestNotNull(TEXT("set-up: the shipped table holds Summon Imp"), SummonImp)
		|| !TestEqual(TEXT("set-up: Summon Imp's row reserves 10"), SummonImp->Params.FervourReserve, 10.0f, 0.001f))
	{
		return false;
	}

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({KindRisesOnCursedDeath});
	Player.Pool(100.0f);
	TestEqual(TEXT("nothing out reserves nothing"), Player.Reserved(), 0.0f, 0.001f);

	TestEqual(TEXT("a cursed death raises one imp"), KindCursedDeath(*this, World, Player, FVector(4 * KindM, 0, 0)), 1);
	const TArray<ACataclysmMinion*> Imps = UCataclysmCommand::MinionsOfTypeCommandedBy(Player.Character, TEXT("Imp"));
	if (!TestEqual(TEXT("and the player holds it"), Imps.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("the risen imp reserves Summon Imp's 10"), KindReserveOf(Imps[0]), SummonImp->Params.FervourReserve,
		0.001f);
	TestEqual(TEXT("the pool holds 10 back"), Player.Reserved(), 10.0f, 0.001f);
	TestEqual(TEXT("so 90 of 100 can be spent"), Player.Spendable(), 90.0f, 0.001f);
	TestEqual(TEXT("and a full pool is cut to 90 at once"), Player.Held(), 90.0f, 0.001f);

	KindKill(Imps[0]);
	TestEqual(TEXT("a dead imp holds nothing back"), Player.Reserved(), 0.0f, 0.001f);
	TestEqual(TEXT("so the whole pool can be spent again"), Player.Spendable(), 100.0f, 0.001f);
	TestEqual(TEXT("and the 10 cut off is not given back"), Player.Held(), 90.0f, 0.001f);
	return true;
}

// ==========================================================================
// R2. A risen imp the pool cannot hold does not rise
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesRisenImpRefusedTest,
	"Cataclysm.KindReserves.ARisenImpThePoolCannotHoldDoesNotRise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A pool of 15 holds one risen imp at 10 and not a second: the second cursed death raises nothing and destroys nothing.
 * THE CONTROL is a pool of 20, which takes the second, so a second death that raised nothing for another reason would
 * fail here.
 */
bool FCataclysmKindReservesRisenImpRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Small(World, FVector::ZeroVector);
	FKindPlayer Roomy(World, FVector(0, 40 * KindM, 0));
	if (!TestTrue(TEXT("set-up: two possessed players"), Small.IsUsable() && Roomy.IsUsable()))
	{
		return false;
	}
	Small.Holds({KindRisesOnCursedDeath});
	Small.Pool(15.0f);
	Roomy.Holds({KindRisesOnCursedDeath});
	Roomy.Pool(20.0f);

	TestEqual(TEXT("in a pool of 15 the first cursed death raises an imp"),
		KindCursedDeath(*this, World, Small, FVector(4 * KindM, 0, 0)), 1);
	TestEqual(TEXT("and the second raises nothing: 5 is left and an imp reserves 10"),
		KindCursedDeath(*this, World, Small, FVector(8 * KindM, 0, 0)), 1);
	TestEqual(TEXT("so 10 is held"), Small.Reserved(), 10.0f, 0.001f);

	TestEqual(TEXT("control: in a pool of 20 the first raises an imp"),
		KindCursedDeath(*this, World, Roomy, FVector(4 * KindM, 40 * KindM, 0)), 1);
	TestEqual(TEXT("control: and the second raises another"),
		KindCursedDeath(*this, World, Roomy, FVector(8 * KindM, 40 * KindM, 0)), 2);
	TestEqual(TEXT("control: holding 20"), Roomy.Reserved(), 20.0f, 0.001f);
	return true;
}

// ==========================================================================
// R3. Under Crowned a risen imp reserves 5
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesRisenImpCrownedTest,
	"Cataclysm.KindReserves.UnderCrownedARisenImpReservesFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Crowned, "Each minion reserves 5 less Fervour, never less than 1", reaches a risen imp as it reaches a summoned one. */
bool FCataclysmKindReservesRisenImpCrownedTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({KindRisesOnCursedDeath, KindCrowned});
	Player.Pool(100.0f);

	TestEqual(TEXT("a cursed death raises an imp"), KindCursedDeath(*this, World, Player, FVector(4 * KindM, 0, 0)), 1);
	TestEqual(TEXT("which reserves 10 less Crowned's 5"), Player.Reserved(), 5.0f, 0.001f);
	return true;
}

// ==========================================================================
// 4. A necrosis imp reserves 5, and a pool that cannot hold it raises none
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesNecrosisImpTest,
	"Cataclysm.KindReserves.ANecrosisImpReservesFiveAndAFullPoolRaisesNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Enemies killed by necrosis rise as temporary minions for 5-10 seconds." Its own 5, not Summon Imp's 10. A pool of 9
 * holds one and not a second; THE CONTROL, a pool of 10, holds both.
 */
bool FCataclysmKindReservesNecrosisImpTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Small(World, FVector::ZeroVector);
	FKindPlayer Roomy(World, FVector(0, 40 * KindM, 0));
	if (!TestTrue(TEXT("set-up: two possessed players"), Small.IsUsable() && Roomy.IsUsable()))
	{
		return false;
	}
	Small.Holds({KindRisesOnNecrosisKill});
	Small.Pool(9.0f);
	Roomy.Holds({KindRisesOnNecrosisKill});
	Roomy.Pool(10.0f);

	const ACataclysmMinion* First = KindNecrosisImp(World, Small, FVector(4 * KindM, 0, 0));
	if (!TestNotNull(TEXT("a necrosis kill raises an imp"), First))
	{
		return false;
	}
	TestEqual(TEXT("which reserves 5"), KindReserveOf(First), UCataclysmRisenImps::NecrosisImpFervourReserve, 0.001f);
	TestEqual(TEXT("so 5 of 9 is held"), Small.Reserved(), 5.0f, 0.001f);
	TestEqual(TEXT("and the full pool is cut to 4"), Small.Held(), 4.0f, 0.001f);
	TestNull(TEXT("a second, with 4 left, raises none"), KindNecrosisImp(World, Small, FVector(8 * KindM, 0, 0)));
	TestEqual(TEXT("so one is commanded"), Small.Commanded(), 1);

	TestNotNull(TEXT("control: a pool of 10 raises the first"),
		KindNecrosisImp(World, Roomy, FVector(4 * KindM, 40 * KindM, 0)));
	TestNotNull(TEXT("control: and the second"), KindNecrosisImp(World, Roomy, FVector(8 * KindM, 40 * KindM, 0)));
	TestEqual(TEXT("control: holding 10"), Roomy.Reserved(), 10.0f, 0.001f);
	return true;
}

// ==========================================================================
// 5. A Warhound reserves 5, a second reserves 5 more, and a full pool summons none
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesWarhoundTest,
	"Cataclysm.KindReserves.AWarhoundReservesFiveASecondReservesFiveMoreAndAFullPoolSummonsNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Warlord's Will's six-piece Warhound. A second summon makes a second Warhound beside the first, reserving 5 more: the
 * owner's ruling of 2026-11-20 removed every count cap on what reserves the class resource, and with it the one Warhound
 * at most and the refresh a second summon made (Q3, 2026-10-18).
 */
bool FCataclysmKindReservesWarhoundTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	FKindPlayer Small(World, FVector(0, 40 * KindM, 0));
	if (!TestTrue(TEXT("set-up: two possessed players"), Player.IsUsable() && Small.IsUsable()))
	{
		return false;
	}
	Player.Holds({KindWarhoundSeconds});
	Player.Pool(100.0f);
	Small.Holds({KindWarhoundSeconds});
	Small.Pool(4.0f);

	const ACataclysmMinion* Hound = ACataclysmMinion::SummonWarhound(Player.Character);
	if (!TestNotNull(TEXT("a Warhound is summoned"), Hound))
	{
		return false;
	}
	TestEqual(TEXT("which reserves 5"), KindReserveOf(Hound), ACataclysmMinion::WarhoundFervourReserve, 0.001f);
	TestEqual(TEXT("so 95 can be spent"), Player.Spendable(), 95.0f, 0.001f);

	const ACataclysmMinion* Second = ACataclysmMinion::SummonWarhound(Player.Character);
	TestTrue(TEXT("a second summon makes a second Warhound"), Second && Second != Hound);
	TestEqual(TEXT("so two are commanded"), Player.Commanded(), 2);
	TestEqual(TEXT("holding 10"), Player.Reserved(), 2.0f * ACataclysmMinion::WarhoundFervourReserve, 0.001f);

	TestNull(TEXT("a pool of 4 summons none"), ACataclysmMinion::SummonWarhound(Small.Character));
	TestEqual(TEXT("and commands nothing"), Small.Commanded(), 0);
	return true;
}

// ==========================================================================
// 6. A Tyrant's Minion reserves 10, a second reserves 10 more, and a full pool summons none
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesTyrantsMinionTest,
	"Cataclysm.KindReserves.ATyrantsMinionReservesTenASecondReservesTenMoreAndAFullPoolSummonsNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Tyrant's Chains' six-piece minion. A second summon makes a second beside the first, reserving 10 more: the owner's
 * ruling of 2026-11-20 replaced their ruling #3 of 2026-10-17, one Tyrant's Minion at most and a refresh.
 */
bool FCataclysmKindReservesTyrantsMinionTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	FKindPlayer Small(World, FVector(0, 40 * KindM, 0));
	if (!TestTrue(TEXT("set-up: two possessed players"), Player.IsUsable() && Small.IsUsable()))
	{
		return false;
	}
	Player.Holds({KindTyrantsSeconds});
	Player.Pool(100.0f);
	Small.Holds({KindTyrantsSeconds});
	Small.Pool(9.0f);

	const FVector Where(4 * KindM, 0, 0);
	const ACataclysmMinion* Tyrants =
		ACataclysmMinion::SummonTyrantsMinion(Player.Character, /*bLostWasTyrantsMinion=*/false, Where);
	if (!TestNotNull(TEXT("a Tyrant's Minion is summoned"), Tyrants))
	{
		return false;
	}
	TestEqual(TEXT("which reserves 10"), KindReserveOf(Tyrants), ACataclysmMinion::TyrantsMinionFervourReserve, 0.001f);
	TestEqual(TEXT("so 90 can be spent"), Player.Spendable(), 90.0f, 0.001f);

	const ACataclysmMinion* Second = ACataclysmMinion::SummonTyrantsMinion(Player.Character, false, Where);
	TestTrue(TEXT("a second summon makes a second Tyrant's Minion"), Second && Second != Tyrants);
	TestEqual(TEXT("so two are commanded"), Player.Commanded(), 2);
	TestEqual(TEXT("holding 20"), Player.Reserved(), 2.0f * ACataclysmMinion::TyrantsMinionFervourReserve, 0.001f);

	TestNull(TEXT("a pool of 9 summons none"),
		ACataclysmMinion::SummonTyrantsMinion(Small.Character, false, FVector(4 * KindM, 40 * KindM, 0)));
	TestEqual(TEXT("and commands nothing"), Small.Commanded(), 0);
	return true;
}

// ==========================================================================
// 7. A skill gadget reserves 15, and a second press moves it and holds nothing more
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesSkillGadgetTest,
	"Cataclysm.KindReserves.ASkillGadgetReserves15AndASecondPressPlacesAnotherReserving15More",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The slot's press places its gadget, reserving 15. UNDER THE GADGET RULE (V5g, 2026-11-25), a second press places
 * another while the pool can hold it, reserving 15 more; until then a second press moved the one standing (S6). THE
 * CONTROL: a pool of 15 holds one, and its second press places nothing and holds nothing more.
 */
bool FCataclysmKindReservesSkillGadgetTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const ECataclysmAbilitySlot Heavy = ECataclysmAbilitySlot::Heavy;
	const FGameplayTag HeavyTag = CataclysmAbilitySlots::Tag(Heavy);
	const FString HeavyTagName = HeavyTag.GetTagName().ToString();
	const FKindStat HeavyIsAGadget{UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat, 1.0f, *HeavyTagName};
	Player.Holds({HeavyIsAGadget});
	Player.Pool(100.0f);
	if (!TestTrue(TEXT("set-up: the Heavy slot is a skill gadget"), Player.System->SlotIsASkillGadget(Heavy)))
	{
		return false;
	}

	Player.System->PressOfASkillGadgetSlot(HeavyTag);
	Player.System->ReleaseOfASkillGadgetSlot(HeavyTag);
	const ACataclysmMinion* Placed = Player.System->SkillGadgetIn(Heavy);
	if (!TestNotNull(TEXT("the press places the Heavy slot's gadget"), Placed))
	{
		return false;
	}
	TestEqual(TEXT("which reserves 15"), KindReserveOf(Placed), ACataclysmMinion::SkillGadgetFervourReserve, 0.001f);
	TestEqual(TEXT("so 85 can be spent"), Player.Spendable(), 85.0f, 0.001f);

	Player.Character->SetActorLocation(FVector(6 * KindM, 0, 0));
	Player.System->PressOfASkillGadgetSlot(HeavyTag);
	Player.System->ReleaseOfASkillGadgetSlot(HeavyTag);
	TestTrue(TEXT("a second press places another gadget"), Player.System->SkillGadgetIn(Heavy) != Placed);
	TestEqual(TEXT("two things are commanded"), Player.Commanded(), 2);
	TestEqual(TEXT("and 30 is held"), Player.Reserved(), 30.0f, 0.001f);

	// THE CONTROL: a pool of 15 holds one gadget, and its second press places nothing and holds nothing more.
	FKindPlayer Small(World, FVector(0, 40 * KindM, 0));
	if (!TestTrue(TEXT("set-up: a second possessed player"), Small.IsUsable()))
	{
		return false;
	}
	Small.Holds({HeavyIsAGadget});
	Small.Pool(15.0f);
	for (int32 Press = 0; Press < 2; ++Press)
	{
		Small.System->PressOfASkillGadgetSlot(HeavyTag);
		Small.System->ReleaseOfASkillGadgetSlot(HeavyTag);
	}
	TestEqual(TEXT("control: a pool of 15 commands one gadget after two presses"), Small.Commanded(), 1);
	TestEqual(TEXT("control: holding 15"), Small.Reserved(), 15.0f, 0.001f);
	return true;
}

// ==========================================================================
// 8. A skill trap reserves 5; a sixth stands while the pool has room, and the next is refused
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesSkillTrapTest,
	"Cataclysm.KindReserves.ASkillTrapReservesFiveASixthStandsWhileThePoolHasRoomAndTheNextIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A pool of exactly 30 holds six traps at 5. Until branch B of issue #2787 a sixth was over the cap of five (T6, a
 * judgement of 2026-10-19) and destroyed the oldest; the owner's ruling of 2026-11-20 removed every count cap on what
 * reserves the class resource, so the sixth stands beside the other five and the oldest with them. The seventh does not
 * fit, is refused, and destroys nothing.
 */
bool FCataclysmKindReservesSkillTrapTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Full(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Full.IsUsable()))
	{
		return false;
	}
	Full.Holds({});
	Full.Pool(30.0f);

	const ECataclysmAbilitySlot Heavy = ECataclysmAbilitySlot::Heavy;
	ACataclysmMinion* Oldest = ACataclysmMinion::PlaceSkillTrap(Full.Character, Heavy, FVector(3 * KindM, 0, 0));
	if (!TestNotNull(TEXT("the first trap is placed"), Oldest))
	{
		return false;
	}
	TestEqual(TEXT("and reserves 5"), KindReserveOf(Oldest), ACataclysmMinion::SkillTrapFervourReserve, 0.001f);
	const TWeakObjectPtr<ACataclysmMinion> OldestHeld = Oldest;
	for (int32 Index = 1; Index < 5; ++Index)
	{
		ACataclysmMinion::PlaceSkillTrap(Full.Character, Heavy, FVector(3 * KindM, Index * 2 * KindM, 0));
	}
	if (!TestEqual(TEXT("set-up: five traps stand"), ACataclysmMinion::SkillTrapsOf(Full.Character).Num(), 5)
		|| !TestEqual(TEXT("set-up: holding 25 of the pool of 30"), Full.Reserved(), 25.0f, 0.001f))
	{
		return false;
	}

	TestNotNull(TEXT("a sixth is placed"),
		ACataclysmMinion::PlaceSkillTrap(Full.Character, Heavy, FVector(3 * KindM, 10 * 2 * KindM, 0)));
	TestTrue(TEXT("and the oldest still stands"), OldestHeld.IsValid() && !OldestHeld->IsActorBeingDestroyed());
	TestEqual(TEXT("so six stand"), ACataclysmMinion::SkillTrapsOf(Full.Character).Num(), 6);
	TestEqual(TEXT("holding the whole 30"), Full.Reserved(), 30.0f, 0.001f);

	TestNull(TEXT("a seventh does not fit and is refused"),
		ACataclysmMinion::PlaceSkillTrap(Full.Character, Heavy, FVector(3 * KindM, 12 * 2 * KindM, 0)));
	TestTrue(TEXT("destroying nothing: the oldest still stands"),
		OldestHeld.IsValid() && !OldestHeld->IsActorBeingDestroyed());
	TestEqual(TEXT("and six stand"), ACataclysmMinion::SkillTrapsOf(Full.Character).Num(), 6);
	return true;
}

// ==========================================================================
// 9. A full pool refuses a skill gadget and places nothing
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesGadgetRefusedTest,
	"Cataclysm.KindReserves.AFullPoolRefusesASkillGadgetAndPlacesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A pool of 20 holds one gadget at 15 and not a second. THE CONTROL, a pool of 30, holds both. */
bool FCataclysmKindReservesGadgetRefusedTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Small(World, FVector::ZeroVector);
	FKindPlayer Roomy(World, FVector(0, 40 * KindM, 0));
	if (!TestTrue(TEXT("set-up: two possessed players"), Small.IsUsable() && Roomy.IsUsable()))
	{
		return false;
	}
	Small.Holds({});
	Small.Pool(20.0f);
	Roomy.Holds({});
	Roomy.Pool(30.0f);

	TestNotNull(TEXT("a pool of 20 places a Heavy gadget"),
		ACataclysmMinion::PlaceSkillGadget(Small.Character, ECataclysmAbilitySlot::Heavy, FVector(3 * KindM, 0, 0)));
	TestNull(TEXT("and refuses a Special one: 5 is left and a gadget reserves 15"),
		ACataclysmMinion::PlaceSkillGadget(Small.Character, ECataclysmAbilitySlot::Special, FVector(5 * KindM, 0, 0)));
	TestEqual(TEXT("so one gadget is commanded"), Small.Commanded(), 1);
	TestEqual(TEXT("holding 15"), Small.Reserved(), 15.0f, 0.001f);

	TestNotNull(TEXT("control: a pool of 30 places the Heavy gadget"),
		ACataclysmMinion::PlaceSkillGadget(Roomy.Character, ECataclysmAbilitySlot::Heavy, FVector(3 * KindM, 40 * KindM, 0)));
	TestNotNull(TEXT("control: and the Special one"),
		ACataclysmMinion::PlaceSkillGadget(Roomy.Character, ECataclysmAbilitySlot::Special, FVector(5 * KindM, 40 * KindM, 0)));
	TestEqual(TEXT("control: holding 30"), Roomy.Reserved(), 30.0f, 0.001f);
	return true;
}

// ==========================================================================
// 10. Crowned takes 5 off every kind no skill makes, never below 1
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesCrownedTest,
	"Cataclysm.KindReserves.CrownedTakesFiveOffEveryKindNeverBelowOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Each minion reserves 5 less Fervour, never less than 1." The necrosis imp, the Warhound and the skill trap state 5,
 * so the floor holds them at 1; the Tyrant's Minion's 10 becomes 5 and the skill gadget's 15 becomes 10. The risen imp's
 * is `UnderCrownedARisenImpReservesFive`.
 */
bool FCataclysmKindReservesCrownedTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({KindCrowned, KindRisesOnNecrosisKill, KindWarhoundSeconds, KindTyrantsSeconds});
	Player.Pool(100.0f);

	TestEqual(TEXT("a necrosis imp's 5 is held at the floor of 1"),
		KindReserveOf(KindNecrosisImp(World, Player, FVector(4 * KindM, 0, 0))), 1.0f, 0.001f);
	TestEqual(TEXT("a Warhound's 5 is held at 1"),
		KindReserveOf(ACataclysmMinion::SummonWarhound(Player.Character)), 1.0f, 0.001f);
	TestEqual(TEXT("a Tyrant's Minion's 10 becomes 5"),
		KindReserveOf(ACataclysmMinion::SummonTyrantsMinion(Player.Character, false, FVector(0, 4 * KindM, 0))), 5.0f,
		0.001f);
	TestEqual(TEXT("a skill gadget's 15 becomes 10"),
		KindReserveOf(ACataclysmMinion::PlaceSkillGadget(Player.Character, ECataclysmAbilitySlot::Heavy,
			FVector(-4 * KindM, 0, 0))), 10.0f, 0.001f);
	TestEqual(TEXT("a skill trap's 5 is held at 1"),
		KindReserveOf(ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy,
			FVector(0, -4 * KindM, 0))), 1.0f, 0.001f);
	TestEqual(TEXT("and the pool holds their sum, 18"), Player.Reserved(), 18.0f, 0.001f);
	return true;
}

// ==========================================================================
// 11. The control: with every kind gone, nothing is held
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesNothingStandingTest,
	"Cataclysm.KindReserves.WithEveryKindGoneNothingIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * One of each of the six kinds holds 10 + 5 + 5 + 10 + 15 + 5 = 50 of a pool of 100. When every one is gone, nothing
 * is held and the whole pool can be spent: a reserve is held while the thing stands and no longer.
 */
bool FCataclysmKindReservesNothingStandingTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({KindRisesOnCursedDeath, KindRisesOnNecrosisKill, KindWarhoundSeconds, KindTyrantsSeconds});
	Player.Pool(100.0f);
	TestEqual(TEXT("before anything is made, nothing is held"), Player.Reserved(), 0.0f, 0.001f);
	TestEqual(TEXT("and all 100 can be spent"), Player.Spendable(), 100.0f, 0.001f);

	TestEqual(TEXT("set-up: a risen imp"), KindCursedDeath(*this, World, Player, FVector(4 * KindM, 0, 0)), 1);
	TestNotNull(TEXT("set-up: a necrosis imp"), KindNecrosisImp(World, Player, FVector(8 * KindM, 0, 0)));
	TestNotNull(TEXT("set-up: a Warhound"), ACataclysmMinion::SummonWarhound(Player.Character));
	TestNotNull(TEXT("set-up: a Tyrant's Minion"),
		ACataclysmMinion::SummonTyrantsMinion(Player.Character, false, FVector(0, 4 * KindM, 0)));
	TestNotNull(TEXT("set-up: a skill gadget"),
		ACataclysmMinion::PlaceSkillGadget(Player.Character, ECataclysmAbilitySlot::Heavy, FVector(-4 * KindM, 0, 0)));
	TestNotNull(TEXT("set-up: a skill trap"),
		ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy, FVector(0, -4 * KindM, 0)));
	if (!TestEqual(TEXT("set-up: six things are commanded"), Player.Commanded(), 6)
		|| !TestEqual(TEXT("set-up: holding 50"), Player.Reserved(), 50.0f, 0.001f))
	{
		return false;
	}

	for (AActor* Each : UCataclysmCommand::ThingsCommandedBy(Player.Character))
	{
		Each->Destroy();
	}
	TestEqual(TEXT("with every one gone, nothing is commanded"), Player.Commanded(), 0);
	TestEqual(TEXT("nothing is held"), Player.Reserved(), 0.0f, 0.001f);
	TestEqual(TEXT("and all 100 can be spent again"), Player.Spendable(), 100.0f, 0.001f);
	return true;
}

// ==========================================================================
// 12. The resource bar is drawn for a reserve with no generator, and not with neither
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesBarTest,
	"Cataclysm.KindReserves.TheBarIsDrawnForAReserveWithNoGeneratorAndNotWithNeither",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmCombatOverlay::FervourOf` is what the heads-up display asks before it draws the resource bar. A player
 * with no generator and nothing reserved is not given one (issue #954); holding a skill trap, it is, with the trap's 5
 * shown as the reserved section, labelled; with the trap gone, the bar goes again.
 */
bool FCataclysmKindReservesBarTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({});
	Player.Pool(100.0f);

	float Fervour = -1.0f;
	float Spendable = -1.0f;
	TestFalse(TEXT("with no generator and nothing reserved, no bar is drawn"),
		UCataclysmCombatOverlay::FervourOf(Player.Character, Fervour, Spendable));

	ACataclysmMinion* Trap =
		ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy, FVector(3 * KindM, 0, 0));
	if (!TestNotNull(TEXT("set-up: a skill trap is placed"), Trap))
	{
		return false;
	}
	TestTrue(TEXT("holding a trap, the bar is drawn"), UCataclysmCombatOverlay::FervourOf(Player.Character, Fervour, Spendable));
	TestEqual(TEXT("its spendable part is 95"), Spendable, 95.0f, 0.001f);
	TestEqual(TEXT("its reserved section is 5"), UCataclysmCombatOverlay::ReservedFervourOf(Player.Character), 5.0f,
		0.001f);
	TestEqual(TEXT("and it is labelled"), UCataclysmCombatOverlay::ReservedTextFor(5.0f), FString(TEXT("5 reserved")));

	Trap->Destroy();
	TestFalse(TEXT("with the trap gone, no bar is drawn again"),
		UCataclysmCombatOverlay::FervourOf(Player.Character, Fervour, Spendable));
	return true;
}

// ==========================================================================
// Cataclysm.NoCaps. Branch B of issue #2787: no count caps, and the reserve percentage that replaced the cap bonuses
// ==========================================================================

namespace CataclysmNoCapsTest
{
	using namespace CataclysmKindReservesTest;

	/** Summon Imp's shipped row since branch B: no `MaxActive`, a reserve of 10. */
	const TCHAR* const NoCapsImpRow = TEXT("Count=1; Duration=20; Radius=3; Minions=Imp:1; FervourReserve=10");

	/** Summon Imp granted in the Special slot of `Player`, with that row, or null. */
	UCataclysmSummonSkill* NoCapsGrantImps(const FKindPlayer& Player)
	{
		const FGameplayAbilitySpecHandle Handle = Player.System->GiveAbilityInSlot(
			UCataclysmSummonSkill::StaticClass(), ECataclysmAbilitySlot::Special, /*Level=*/1, Player.Character);
		FGameplayAbilitySpec* Spec = Player.System->FindAbilitySpecFromHandle(Handle);
		UCataclysmSummonSkill* Skill = Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Skill)
		{
			Skill->SkillName = TEXT("Summon Imp");
			Skill->Params = UCataclysmSkillShapes::ParseParams(NoCapsImpRow);
			Skill->SkillTags = ACataclysmMinion::TagsOfType(TEXT("Imp"));
		}
		return Skill;
	}

	/** The reserve percentage as one flat row, untagged or requiring `Tag`. */
	FKindStat PercentLess(float Value, const TCHAR* Tag = nullptr)
	{
		return FKindStat{UCataclysmCommand::MinionReservePercentLessStat, Value, Tag};
	}

	/** What one of a kind reserves for `Player`, stated `Stated`, with the tags its minion type row carries. */
	float ReserveOfType(const FKindPlayer& Player, float Stated, const TCHAR* Type)
	{
		return UCataclysmCommand::ReserveFor(Player.Character, Stated, ACataclysmMinion::TagsOfType(Type));
	}
}

// ==========================================================================
// N1. A fourth imp is summoned beside three, and the pool refuses the one that does not fit
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoCapsFourthImpTest,
	"Cataclysm.NoCaps.AFourthImpIsSummonedBesideThreeAndThePoolRefusesTheOneThatDoesNotFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Until branch B, "Up to 3 imps may be active at once. Summoning a fourth destroys the oldest, which explodes"; the
 * owner's rulings of 2026-11-20 removed the cap and dropped that sentence with it. A pool of 45 holds four imps at 10:
 * the fourth press makes a fourth and destroys nothing, and the fifth, which would need 50, makes nothing and destroys
 * nothing. Summoned through `SummonOne`, the call a press makes, so no cooldown or cost stands between the presses.
 */
bool FCataclysmNoCapsFourthImpTest::RunTest(const FString&)
{
	using namespace CataclysmNoCapsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({});
	Player.Pool(45.0f);
	UCataclysmSummonSkill* Skill = NoCapsGrantImps(Player);
	if (!TestNotNull(TEXT("set-up: Summon Imp is granted"), Skill))
	{
		return false;
	}

	const TWeakObjectPtr<ACataclysmMinion> First = Skill->SummonOne();
	Skill->SummonOne();
	Skill->SummonOne();
	if (!TestTrue(TEXT("set-up: the first imp stands"), First.IsValid())
		|| !TestEqual(TEXT("set-up: three imps stand"), Skill->LivingMinionCount(), 3))
	{
		return false;
	}

	TestNotNull(TEXT("a fourth press makes a fourth imp"), Skill->SummonOne());
	TestEqual(TEXT("so four stand"), Skill->LivingMinionCount(), 4);
	TestTrue(TEXT("and the first was not destroyed to make room"),
		First.IsValid() && !First->IsActorBeingDestroyed());
	TestEqual(TEXT("holding 40"), Player.Reserved(), 40.0f, 0.001f);

	TestNull(TEXT("a fifth, needing 50 of a pool of 45, is refused"), Skill->SummonOne());
	TestEqual(TEXT("so four still stand"), Skill->LivingMinionCount(), 4);
	TestTrue(TEXT("and the first still stands"), First.IsValid() && !First->IsActorBeingDestroyed());
	return true;
}

// ==========================================================================
// N2. A risen imp rises beside three imps already standing
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoCapsRisenImpTest,
	"Cataclysm.NoCaps.ARisenImpRisesBesideThreeImpsAlreadyStanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The risen imps shared Summon Imp's cap of three (ruled 2026-09-27), so with three imps standing an enemy dying cursed
 * raised nothing. With the caps gone it rises beside them, reserving Summon Imp's 10. The three are spawned directly,
 * which reserves nothing (`Cataclysm.FervourReserve.AMinionSpawnedDirectlyReservesNothing`), so the 10 held is the
 * risen imp's alone.
 */
bool FCataclysmNoCapsRisenImpTest::RunTest(const FString&)
{
	using namespace CataclysmNoCapsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({KindRisesOnCursedDeath});
	Player.Pool(100.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		ACataclysmMinion::Spawn(Player.Character, FVector(2 * KindM, Index * KindM, 0), /*Lifetime=*/60.0f,
			/*bBurns=*/false, TEXT("Imp"));
	}
	if (!TestEqual(TEXT("set-up: three imps stand"),
			UCataclysmCommand::MinionsOfTypeCommandedBy(Player.Character, TEXT("Imp")).Num(), 3)
		|| !TestEqual(TEXT("set-up: reserving nothing"), Player.Reserved(), 0.0f, 0.001f))
	{
		return false;
	}

	TestEqual(TEXT("an enemy dying cursed rises as a fourth imp"),
		KindCursedDeath(*this, World, Player, FVector(6 * KindM, 0, 0)), 4);
	TestEqual(TEXT("reserving Summon Imp's 10"), Player.Reserved(), 10.0f, 0.001f);
	return true;
}

// ==========================================================================
// N3. A percent less takes its share off what a kind reserves, after Crowned's points
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoCapsPercentLessTest,
	"Cataclysm.NoCaps.APercentLessTakesItsShareOffWhatAKindReservesAfterCrowned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your minions reserve 12% reduced Fervour" (The Swarm's row until the demonic tree rework of 2026-11-24 gave the
 * keystone a minion at each kill; enchantments still grant the stat) takes an imp's 10 to 8.8, and a summoned imp holds that. The
 * minion enchantment's two ends, 15 and 30, take it to 8.5 and 7. With Crowned's 5 points as well, the points come off
 * first and the percentage second, a judgement of issue #2787: 30% less of 5 is 3.5, where the other order would be 2.
 */
bool FCataclysmNoCapsPercentLessTest::RunTest(const FString&)
{
	using namespace CataclysmNoCapsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({});
	Player.Pool(100.0f);
	TestEqual(TEXT("control: with nothing held an imp reserves its 10"), ReserveOfType(Player, 10.0f, TEXT("Imp")),
		10.0f, 0.001f);

	Player.Holds({PercentLess(12.0f)});
	TestEqual(TEXT("12% reduced makes it 8.8"), ReserveOfType(Player, 10.0f, TEXT("Imp")), 8.8f, 0.001f);
	UCataclysmSummonSkill* Skill = NoCapsGrantImps(Player);
	if (!TestNotNull(TEXT("set-up: Summon Imp is granted"), Skill))
	{
		return false;
	}
	TestEqual(TEXT("and a summoned imp holds 8.8"), KindReserveOf(Skill->SummonOne()), 8.8f, 0.001f);

	Player.Holds({PercentLess(15.0f)});
	TestEqual(TEXT("the enchantment's low end, 15% less, makes it 8.5"), ReserveOfType(Player, 10.0f, TEXT("Imp")),
		8.5f, 0.001f);
	Player.Holds({PercentLess(30.0f)});
	TestEqual(TEXT("its high end, 30% less, makes it 7"), ReserveOfType(Player, 10.0f, TEXT("Imp")), 7.0f, 0.001f);

	Player.Holds({KindCrowned, PercentLess(30.0f)});
	TestEqual(TEXT("with Crowned, 5 points off first and then 30% less: 3.5"),
		ReserveOfType(Player, 10.0f, TEXT("Imp")), 3.5f, 0.001f);
	return true;
}

// ==========================================================================
// N4. A percent more raises what a kind reserves
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoCapsPercentMoreTest,
	"Cataclysm.NoCaps.APercentMoreRaisesWhatAKindReserves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your minions reserve 25%-65% increased Fervour", the negative enchantment, is the stat at minus 25 to minus 65: an
 * imp's 10 becomes 12.5 at one end and 16.5 at the other, and at 25% increased a pool of 30 holds two imps where it
 * held three.
 */
bool FCataclysmNoCapsPercentMoreTest::RunTest(const FString&)
{
	using namespace CataclysmNoCapsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({PercentLess(-25.0f)});
	Player.Pool(30.0f);
	TestEqual(TEXT("25% more makes an imp's 10 into 12.5"), ReserveOfType(Player, 10.0f, TEXT("Imp")), 12.5f, 0.001f);
	Player.Holds({PercentLess(-65.0f)});
	TestEqual(TEXT("65% more makes it 16.5"), ReserveOfType(Player, 10.0f, TEXT("Imp")), 16.5f, 0.001f);

	Player.Holds({PercentLess(-25.0f)});
	UCataclysmSummonSkill* Skill = NoCapsGrantImps(Player);
	if (!TestNotNull(TEXT("set-up: Summon Imp is granted"), Skill))
	{
		return false;
	}
	Skill->SummonOne();
	Skill->SummonOne();
	TestNull(TEXT("at 25% more a pool of 30 refuses a third imp"), Skill->SummonOne());
	TestEqual(TEXT("so two stand, holding 25"), Player.Reserved(), 25.0f, 0.001f);
	return true;
}

// ==========================================================================
// N5. Every percentage reaching a kind adds into one figure, and a tagged row reaches only its kinds
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoCapsPercentagesAddTest,
	"Cataclysm.NoCaps.EveryPercentReachingAKindAddsIntoOneFigureAndATaggedRowReachesOnlyItsKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three rows at once: 15% less untagged (the minion enchantment), 30% less requiring `Type.Deployable` (the deployable
 * one) and 25% less requiring `Type.Trap` (the trap one). `game/Data/MinionTypes.csv` tags an imp neither, a skill
 * gadget `Type.Deployable`, and a skill trap both. So an imp takes 15%, 10 to 8.5; a gadget 45%, 15 to 8.25; and a trap
 * 70%, 5 to 1.5. The gadget and the trap are placed, so what they hold is read from what was made.
 */
bool FCataclysmNoCapsPercentagesAddTest::RunTest(const FString&)
{
	using namespace CataclysmNoCapsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({PercentLess(15.0f), PercentLess(30.0f, TEXT("Type.Deployable")), PercentLess(25.0f, TEXT("Type.Trap"))});
	Player.Pool(100.0f);

	TestEqual(TEXT("an imp takes the untagged 15% only: 8.5"), ReserveOfType(Player, 10.0f, TEXT("Imp")), 8.5f, 0.001f);
	TestEqual(TEXT("a skill gadget takes 15% and the deployables' 30%: 8.25"),
		KindReserveOf(ACataclysmMinion::PlaceSkillGadget(Player.Character, ECataclysmAbilitySlot::Heavy,
			FVector(4 * KindM, 0, 0))), 8.25f, 0.001f);
	TestEqual(TEXT("a skill trap takes all three, 70%: 1.5"),
		KindReserveOf(ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy,
			FVector(0, 4 * KindM, 0))), 1.5f, 0.001f);
	return true;
}

// ==========================================================================
// N6. No reduction takes a reserve below one point
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmNoCapsFloorTest,
	"Cataclysm.NoCaps.NoReductionTakesAReserveBelowOnePoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The floor is Crowned's own "never less than 1", `UCataclysmCommand::SmallestMinionReserve`, AND SINCE 2026-11-27 THE
 * SUMMED PERCENT STOPS AT 75 (`UCataclysmCommand::MostReservePercentLess`), ruled by the coordinating session with the
 * auras' mana reserve: "The 75% cap on summed reductions applies to BOTH mana and Fervour". 75% less of a trap's 5 is
 * 1.25, the control at the cap; 90% less is held there and is 1.25 too; Crowned's 5 off a trap's 5 would be nothing and
 * is 1; and 150% less of an imp's 10, held at 75%, is 2.5 rather than a reserve below nothing. Until then 80% less of a
 * trap's 5 was 1 and 90% less was the floor.
 */
bool FCataclysmNoCapsFloorTest::RunTest(const FString&)
{
	using namespace CataclysmNoCapsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({});
	Player.Pool(100.0f);

	Player.Holds({PercentLess(75.0f)});
	TestEqual(TEXT("control: 75% less of a trap's 5, the cap, is 1.25"),
		ReserveOfType(Player, ACataclysmMinion::SkillTrapFervourReserve, TEXT("SkillTrap")), 1.25f, 0.001f);
	Player.Holds({PercentLess(90.0f)});
	TestEqual(TEXT("90% less of a trap's 5 is held at 75% and is 1.25"),
		ReserveOfType(Player, ACataclysmMinion::SkillTrapFervourReserve, TEXT("SkillTrap")), 1.25f, 0.001f);
	Player.Holds({KindCrowned});
	TestEqual(TEXT("Crowned's 5 off a trap's 5 would leave nothing and leaves 1"),
		ReserveOfType(Player, ACataclysmMinion::SkillTrapFervourReserve, TEXT("SkillTrap")),
		UCataclysmCommand::SmallestMinionReserve, 0.001f);
	Player.Holds({PercentLess(150.0f)});
	TestEqual(TEXT("150% less of an imp's 10 is held at 75%: 2.5, not a reserve below nothing"),
		ReserveOfType(Player, 10.0f, TEXT("Imp")), 2.5f, 0.001f);
	return true;
}

// ==========================================================================
// A full bar is the maximum less what minions hold back (ruled 2026-11-25, the Death trees' slice C)
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesFullWithAnImpTest,
	"Cataclysm.KindReserves.AnImpOutLeavesTheBarFullAtTheMaximumLessItsReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Every gain stops at the maximum less the reserve, so before the ruling a character with an imp out never reached
 * "full": `class_resource_at_maximum` never held for it and the full-bar crossing never opened its window. A pool of
 * 100 with a necrosis imp reserving 5 is full at 95. THE CONTROL: a player with no imp is not full at 95, and is at 100.
 */
bool FCataclysmKindReservesFullWithAnImpTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer WithImp(World, FVector::ZeroVector);
	FKindPlayer Alone(World, FVector(0, 40 * KindM, 0));
	if (!TestTrue(TEXT("set-up: two possessed players"), WithImp.IsUsable() && Alone.IsUsable()))
	{
		return false;
	}
	WithImp.Holds({KindRisesOnNecrosisKill});
	WithImp.Pool(100.0f);
	Alone.Holds({});
	Alone.Pool(100.0f);

	const auto IsFull = [](const FKindPlayer& Player)
	{
		return UCataclysmStatPipeline::ConditionHolds(
			ECataclysmStatCondition::ClassResourceAtMaximum, 0.0f, Player.System->CurrentConditions());
	};

	if (!TestNotNull(TEXT("set-up: a necrosis kill raises an imp"),
			KindNecrosisImp(World, WithImp, FVector(4 * KindM, 0, 0))))
	{
		return false;
	}
	TestEqual(TEXT("set-up: it holds back 5"), WithImp.Reserved(), 5.0f, 0.001f);
	TestEqual(TEXT("set-up: and the full pool is cut to 95"), WithImp.Held(), 95.0f, 0.001f);
	TestTrue(TEXT("95 of 100 with 5 held back is a full bar"), IsFull(WithImp));

	// THE CROSSING: below the top, then back to it, opens the window again.
	CataclysmTestWorld::RunClock(World, 2.0f);
	WithImp.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 90.0f);
	TestFalse(TEXT("90 is not full"), IsFull(WithImp));
	TestEqual(TEXT("and the window opened at the start has aged 2 seconds"),
		WithImp.System->SecondsSinceClassResourceFull(), 2.0f, 0.1f);
	WithImp.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 95.0f);
	TestTrue(TEXT("back at 95 it is full again"), IsFull(WithImp));
	TestEqual(TEXT("and reaching it opened the window again"),
		WithImp.System->SecondsSinceClassResourceFull(), 0.0f, 0.001f);

	// THE CONTROL: with nothing held back, 95 is not the top and 100 is.
	Alone.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 95.0f);
	TestFalse(TEXT("control: 95 of 100 with nothing held back is not full"), IsFull(Alone));
	Alone.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
	TestTrue(TEXT("control: and 100 is"), IsFull(Alone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesFullMeasureTest,
	"Cataclysm.KindReserves.FullMeasureCountsAFullBarForASoulCollectorWithAMinionOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Death class reading a full bar through a real row. Full Measure, eight points: "+2% increased damage per point while
 * your Fervour is at its maximum", read from `DT_PassiveEffects` as the game reads it. With a minion holding back 5 of
 * a pool of 100, the row holds at 95. THE CONTROL: the same character below the top gets nothing.
 */
bool FCataclysmKindReservesFullMeasureTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Player(World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_a_a2")), 8);
	const TMap<FName, TArray<FCataclysmStatModifier>> Rows = UCataclysmPassiveTree::ModifiersFor(
		Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(),
		{FName(TEXT("Death"))});
	const FName Damage(TEXT("attack_damage"));
	if (!TestTrue(TEXT("set-up: Full Measure's rows carry attack damage"), Rows.Contains(Damage)))
	{
		return false;
	}
	TMap<FName, FCataclysmStatInputs> Lines;
	FCataclysmStatInputs& DamageLine = Lines.FindOrAdd(Damage);
	DamageLine.Base = 100.0f;
	DamageLine.Modifiers = Rows[Damage];
	FCataclysmStatModifier Rises;
	Rises.Bucket = ECataclysmStatBucket::Flat;
	Rises.Source = ECataclysmModifierSource::Enchantment;
	Rises.Value = KindRisesOnNecrosisKill.Value;
	FCataclysmStatInputs& RiseLine = Lines.FindOrAdd(FName(KindRisesOnNecrosisKill.Stat));
	RiseLine.Base = 0.0f;
	RiseLine.Modifiers = {Rises};
	Player.System->SetStatInputs(MoveTemp(Lines));
	Player.Pool(100.0f);

	if (!TestNotNull(TEXT("set-up: a necrosis kill raises a minion"),
			KindNecrosisImp(World, Player, FVector(4 * KindM, 0, 0))))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the pool is cut to 95"), Player.Held(), 95.0f, 0.001f);

	const FGameplayTagContainer NoTags;
	TestEqual(TEXT("at 95 with 5 held back, Full Measure's 16% increased applies"),
		Player.System->StatForSkill(Damage, NoTags, 0.0f), 116.0f, 0.01f);

	// THE CONTROL: below the top, nothing.
	Player.System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 90.0f);
	TestEqual(TEXT("control: at 90 it does not"), Player.System->StatForSkill(Damage, NoTags, 0.0f), 100.0f, 0.01f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
