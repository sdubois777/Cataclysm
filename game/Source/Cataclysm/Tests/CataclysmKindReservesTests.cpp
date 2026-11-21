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
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
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
 * spendable Fervour is cut at once; each is refused, with nothing made, when the pool cannot hold it; a refresh or a
 * gadget's move holds nothing more; Crowned takes 5 off every kind, never below 1; nothing is held once everything is
 * gone; and the resource bar is drawn for a character that reserves anything, generator or not.
 *
 * THE PLAYER IS A POSSESSED PLAYER CHARACTER WITH ITS PLAYER STATE, so its pool is the real class resource set. The
 * stats that switch each kind on are written by hand, a stat line per call replacing the last, as an item or a node
 * would build it. Nothing waits on a tick. Registered under `Cataclysm.KindReserves.`, which no other group's name
 * begins with.
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
// 5. A Warhound reserves 5, a refresh holds nothing more, and a full pool summons none
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesWarhoundTest,
	"Cataclysm.KindReserves.AWarhoundReservesFiveARefreshHoldsNothingMoreAndAFullPoolSummonsNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Warlord's Will's six-piece Warhound. A second summon refreshes the one standing (Q3) and reserves nothing more. */
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

	TestTrue(TEXT("a second summon refreshes the one standing"), ACataclysmMinion::SummonWarhound(Player.Character) == Hound);
	TestEqual(TEXT("and still 5 is held"), Player.Reserved(), 5.0f, 0.001f);

	TestNull(TEXT("a pool of 4 summons none"), ACataclysmMinion::SummonWarhound(Small.Character));
	TestEqual(TEXT("and commands nothing"), Small.Commanded(), 0);
	return true;
}

// ==========================================================================
// 6. A Tyrant's Minion reserves 10, a refresh holds nothing more, and a full pool summons none
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesTyrantsMinionTest,
	"Cataclysm.KindReserves.ATyrantsMinionReservesTenARefreshHoldsNothingMoreAndAFullPoolSummonsNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Tyrant's Chains' six-piece minion. A second summon refreshes the one standing (ruling #3) and reserves nothing more. */
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

	TestTrue(TEXT("a second summon refreshes the one standing"),
		ACataclysmMinion::SummonTyrantsMinion(Player.Character, false, Where) == Tyrants);
	TestEqual(TEXT("and still 10 is held"), Player.Reserved(), 10.0f, 0.001f);

	TestNull(TEXT("a pool of 9 summons none"),
		ACataclysmMinion::SummonTyrantsMinion(Small.Character, false, FVector(4 * KindM, 40 * KindM, 0)));
	TestEqual(TEXT("and commands nothing"), Small.Commanded(), 0);
	return true;
}

// ==========================================================================
// 7. A skill gadget reserves 15, and a second press moves it and holds nothing more
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesSkillGadgetTest,
	"Cataclysm.KindReserves.ASkillGadgetReserves15AndASecondPressMovesItAndHoldsNothingMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The slot's press places its gadget, and a second press MOVES the one standing (S6), which the coordinating session
 * ruled on 2026-11-20 is how the gadget is controlled and not a count cap. The move holds nothing more.
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
	TestTrue(TEXT("a second press keeps the same gadget"), Player.System->SkillGadgetIn(Heavy) == Placed);
	TestEqual(TEXT("one thing is commanded"), Player.Commanded(), 1);
	TestEqual(TEXT("and still 15 is held"), Player.Reserved(), 15.0f, 0.001f);
	return true;
}

// ==========================================================================
// 8. A skill trap reserves 5; over the cap the oldest's share is freed before the new one is asked to fit
// ==========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmKindReservesSkillTrapTest,
	"Cataclysm.KindReserves.ASkillTrapReservesFiveAndOverTheCapTheOldestsShareIsFreedFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A pool of exactly 25 holds five traps at 5. A sixth is over the cap of five (T6), which destroys the oldest, and that
 * one's 5 is counted as freed before the sixth is asked to fit, as an evicted imp's is (ruled 2026-09-30): so the sixth
 * is placed. A pool of 9 below the cap holds one and refuses a second, which frees nothing.
 */
bool FCataclysmKindReservesSkillTrapTest::RunTest(const FString&)
{
	using namespace CataclysmKindReservesTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FKindPlayer Full(World, FVector::ZeroVector);
	FKindPlayer Small(World, FVector(0, 40 * KindM, 0));
	if (!TestTrue(TEXT("set-up: two possessed players"), Full.IsUsable() && Small.IsUsable()))
	{
		return false;
	}
	Full.Holds({});
	Full.Pool(25.0f);
	Small.Holds({});
	Small.Pool(9.0f);

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
		|| !TestEqual(TEXT("set-up: holding the whole pool of 25"), Full.Reserved(), 25.0f, 0.001f))
	{
		return false;
	}

	TestNotNull(TEXT("a sixth, over the cap, is placed: the oldest's 5 is freed first"),
		ACataclysmMinion::PlaceSkillTrap(Full.Character, Heavy, FVector(3 * KindM, 10 * 2 * KindM, 0)));
	TestFalse(TEXT("and the oldest is gone"), OldestHeld.IsValid() && !OldestHeld->IsActorBeingDestroyed());
	TestEqual(TEXT("five still stand"), ACataclysmMinion::SkillTrapsOf(Full.Character).Num(), 5);
	TestEqual(TEXT("holding the same 25"), Full.Reserved(), 25.0f, 0.001f);

	TestNotNull(TEXT("a pool of 9 places one trap"),
		ACataclysmMinion::PlaceSkillTrap(Small.Character, Heavy, FVector(3 * KindM, 40 * KindM, 0)));
	TestNull(TEXT("and refuses a second, which frees nothing"),
		ACataclysmMinion::PlaceSkillTrap(Small.Character, Heavy, FVector(5 * KindM, 40 * KindM, 0)));
	TestEqual(TEXT("so one stands"), ACataclysmMinion::SkillTrapsOf(Small.Character).Num(), 1);
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

#endif // WITH_AUTOMATION_TESTS
