// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBoneGiants.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmNecromancerArmy.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Components/SphereComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Necromancer's army, the Death line's finishing slice B (2026-11-26): the Legion limb's permanent minions, the
 * spine's husks raised on a kill, and four vow options (The Restless Dead, Grave Swap, Lich Form, Shared Flesh). Every
 * figure is read from the passive effect table and every node is taken at its full points, so a re-tuned row moves the
 * expectation with it (issue #2928). Each case carries a control.
 */

namespace CataclysmNecromancerArmyTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	constexpr float M = 100.0f;

	/**
	 * A commander a sphere search can find and a pool can hold reserves in: vital, combat, resistance and class resource
	 * sets, 100,000 health, a pool of 150 and no armour, so a blow's share is linear. It belongs to no team, which reads
	 * as hostile to every other body; its minions take its team.
	 */
	struct FArmyCommander
	{
		FArmyCommander(UWorld* World, const FVector& Where)
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

			Set(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			Set(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
			Set(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
			Set(UCataclysmCombatAttributeSet::GetArmorAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FArmyCommander()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		void Set(const FGameplayAttribute& Attribute, float Value) const
		{
			AbilitySystem->SetNumericAttributeBase(Attribute, Value);
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A real creature on the monsters' side with 100,000 health and no defences, for a kill to be made on. */
	struct FArmyMonster
	{
		FArmyMonster(UWorld* World, const FVector& Where)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Actor->SetHealth(100000.0f);
			Actor->SetArmour(0.0f);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent());
			check(AbilitySystem);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(), 0.0f);
		}

		~FArmyMonster()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** Pins `Cataclysm.NecromancerArmyRoll` for one case, and puts it back. */
	struct FPinnedArmyRoll
	{
		explicit FPinnedArmyRoll(float Roll)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.NecromancerArmyRoll"));
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}
		~FPinnedArmyRoll()
		{
			if (Variable)
			{
				Variable->Set(-1.0f, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
	};

	/** A node's maximum points, from the node table; nought for a node it does not hold. */
	int32 MaxPointsOf(const TCHAR* Node)
	{
		const FCataclysmPassiveNodeRow* Row =
			UCataclysmPassiveTree::FindNode(UCataclysmPassiveTree::LoadNodeTable(), FName(Node));
		return Row ? Row->MaxPoints : 0;
	}

	/** A row's value per point, from the effect table: the first on `Stat` of `Option`. Nought when there is none. */
	float PerPointOf(const TCHAR* Node, const TCHAR* Stat, int32 Option = 0)
	{
		for (const FCataclysmPassiveEffectRow* Effect :
			 UCataclysmPassiveTree::EffectsFor(UCataclysmPassiveTree::LoadEffectTable(), FName(Node)))
		{
			if (Effect->Stat == Stat && Effect->Option == Option)
			{
				return Effect->ValuePerPoint;
			}
		}
		return 0.0f;
	}

	/** What a node at its full points is worth on `Stat`: the row's value times the node's maximum points. */
	float FullOf(const TCHAR* Node, const TCHAR* Stat, int32 Option = 0)
	{
		return PerPointOf(Node, Stat, Option) * static_cast<float>(Option > 0 ? 1 : MaxPointsOf(Node));
	}

	/**
	 * Give `Commander` the real rows of these nodes at their full points, each capstone's `Option` chosen, read as a
	 * Death weapon reads them: what `UCataclysmPassiveTree::ModifiersFor` builds, put on its stat lines.
	 */
	void GiveNodes(const FArmyCommander& Commander, const TArray<const TCHAR*>& Nodes,
				   const TArray<TPair<const TCHAR*, int32>>& Options = {})
	{
		FCataclysmPassiveAllocation Allocation;
		for (const TCHAR* Node : Nodes)
		{
			Allocation.Add(FName(Node), FMath::Max(1, MaxPointsOf(Node)));
		}
		for (const TPair<const TCHAR*, int32>& Chosen : Options)
		{
			Allocation.Add(FName(Chosen.Key), 1);
			Allocation.SetChosenOption(FName(Chosen.Key), Chosen.Value);
		}
		TMap<FName, FCataclysmStatInputs> Inputs;
		for (TPair<FName, TArray<FCataclysmStatModifier>>& Line : UCataclysmPassiveTree::ModifiersFor(
				 Allocation, UCataclysmPassiveTree::LoadNodeTable(), UCataclysmPassiveTree::LoadEffectTable(),
				 {FName(TEXT("Death"))}))
		{
			Inputs.FindOrAdd(Line.Key).Modifiers = MoveTemp(Line.Value);
		}
		Commander.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
	}

	/** One generated Death row by its weapon and designed slot, or an empty one. */
	FCataclysmWeaponSkill ArmyRowOf(const TCHAR* WeaponType, ECataclysmAbilitySlot Slot)
	{
		for (const FCataclysmWeaponSkill& Skill :
			 UCataclysmWeaponSkills::SkillsFor(UCataclysmWeaponSkills::LoadGeneratedTable(), WeaponType, TEXT("Death")))
		{
			if (UCataclysmWeaponSkills::DesignedSlotOf(Skill) == Slot)
			{
				return Skill;
			}
		}
		return FCataclysmWeaponSkill();
	}

	/** A generated row granted in its designed slot at level 100, stamped as a weapon's grant stamps it. */
	UCataclysmSummonSkill* GrantArmyRow(const FArmyCommander& Commander, const FCataclysmWeaponSkill& Row)
	{
		const FGameplayAbilitySpecHandle Handle = Commander.AbilitySystem->GiveAbilityInSlot(
			UCataclysmSummonSkill::StaticClass(), UCataclysmWeaponSkills::DesignedSlotOf(Row), /*Level=*/100,
			Commander.Actor);
		FGameplayAbilitySpec* Spec =
			Handle.IsValid() ? Commander.AbilitySystem->FindAbilitySpecFromHandle(Handle) : nullptr;
		UCataclysmSummonSkill* Instance = Spec ? Cast<UCataclysmSummonSkill>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			UCataclysmWeaponSkills::StampOnto(*Instance, Row);
		}
		return Instance;
	}

	/** The Greatsword's Barrow Guard (`Permanent=1`), granted. */
	UCataclysmSummonSkill* BarrowGuardSkill(const FArmyCommander& Commander)
	{
		return GrantArmyRow(Commander, ArmyRowOf(TEXT("Greatsword"), ECataclysmAbilitySlot::Ultimate));
	}

	/** The Greataxe's Shallow Graves (husks that last 8 seconds), granted. */
	UCataclysmSummonSkill* ShallowGravesSkill(const FArmyCommander& Commander)
	{
		return GrantArmyRow(Commander, ArmyRowOf(TEXT("Greataxe"), ECataclysmAbilitySlot::Special));
	}

	float HealthOf(const AActor* Actor, bool bMaximum = false)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(bMaximum ? UCataclysmVitalAttributeSet::GetMaxHealthAttribute()
															 : UCataclysmVitalAttributeSet::GetHealthAttribute())
					  : 0.0f;
	}

	void SetHealthOf(const AActor* Actor, float Health, bool bMaximum = false)
	{
		if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			System->SetNumericAttributeBase(bMaximum ? UCataclysmVitalAttributeSet::GetMaxHealthAttribute()
													 : UCataclysmVitalAttributeSet::GetHealthAttribute(),
											Health);
		}
	}

	/** A type row's health at the commander's level, before any row of the commander's. */
	float TypeHealthFor(const AActor* Commander, const TCHAR* Type)
	{
		const FCataclysmMinionTypeRow* Row = ACataclysmMinion::FindType(ACataclysmMinion::LoadTypeTable(), Type);
		float Health = 0.0f;
		float Damage = 0.0f;
		if (Row)
		{
			ACataclysmMinion::TypeFiguresAtLevel(*Row, ACataclysmMinion::LevelOfSummonerFor(Commander), Health, Damage);
		}
		return Health;
	}

	/** A blow that cannot be evaded or critical, so a share of it is exact. */
	FCataclysmHitDelivery PlainBlow(AActor* DealtBy = nullptr)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bCannotBeEvaded = true;
		Delivery.bCannotCriticallyStrike = true;
		Delivery.bCannotBeRetaliatedAgainst = true;
		Delivery.DealtBy = DealtBy;
		return Delivery;
	}

	void ClearArmy(const AActor* Commander)
	{
		for (AActor* Each : UCataclysmCommand::ThingsCommandedBy(Commander))
		{
			Each->Destroy();
		}
	}

	int32 CountOf(const AActor* Commander, const TCHAR* Type)
	{
		return UCataclysmCommand::MinionsOfTypeCommandedBy(Commander, Type).Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyPermanentTagTest,
	"Cataclysm.NecromancerArmy.APermanentRaiseCarriesTheTagAndAHuskDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Your permanent minions" is `Minion.Permanent`: Barrow Guard's guard (`Permanent=1`) has no lifespan, carries the tag
 * and is a permanent minion; every thrall's tags carry it. CONTROL: Shallow Graves' husk, which lasts 8 seconds, has a
 * lifespan and neither.
 */
bool FCataclysmArmyPermanentTagTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* Guard = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Husk = Graves ? Graves->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: a guard"), Guard) || !TestNotNull(TEXT("set-up: a husk"), Husk))
	{
		return false;
	}
	const FGameplayTag Permanent = UCataclysmNecromancerArmy::PermanentTag();
	TestTrue(TEXT("the tag is registered"), Permanent.IsValid());
	TestEqual(TEXT("the guard has no lifespan"), Guard->GetLifeSpan(), 0.0f);
	TestTrue(TEXT("and carries the tag"), Guard->TypeTags.HasTagExact(Permanent));
	TestTrue(TEXT("so it is a permanent minion"), UCataclysmNecromancerArmy::IsPermanentMinion(Guard));
	TestTrue(TEXT("every thrall's tags carry it"), UCataclysmCommand::ThrallMinionTags().HasTagExact(Permanent));
	TestTrue(TEXT("CONTROL: the husk lasts seconds"), Husk->GetLifeSpan() > 0.0f);
	TestFalse(TEXT("CONTROL: and carries no tag"), Husk->TypeTags.HasTagExact(Permanent));
	TestFalse(TEXT("CONTROL: so it is no permanent minion"), UCataclysmNecromancerArmy::IsPermanentMinion(Husk));
	TestTrue(TEXT("a Death summon's minion rose from the dead"), UCataclysmNecromancerArmy::RoseFromTheDead(Guard));
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyStandingArmyTest,
	"Cataclysm.NecromancerArmy.StandingArmyRaisesAPermanentMinionsBlowAndLiveHealthOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Standing Army at its full points: a guard's blow is multiplied by one plus the row times the points, and its maximum
 * health rises by that share of its type's health at its next step, after it was raised. CONTROL: a husk's blow and
 * health do not move.
 */
bool FCataclysmArmyStandingArmyTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* Guard = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Husk = Graves ? Graves->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: a guard and a husk"), Guard && Husk ? Guard : nullptr))
	{
		return false;
	}
	const TCHAR* Node = TEXT("Necromancer_basic_lg_stem0");
	const float Damage = FullOf(Node, TEXT("minion_damage"));
	const float Health = FullOf(Node, TEXT("minion_health"));
	TestTrue(TEXT("set-up: Standing Army's rows are in the table"), Damage > 0.0f && Health > 0.0f);
	const float GuardHealthBefore = HealthOf(Guard, true);
	const float HuskHealthBefore = HealthOf(Husk, true);

	GiveNodes(Commander, {Node});
	UCataclysmNecromancerArmy::HealthStep(Guard);
	UCataclysmNecromancerArmy::HealthStep(Husk);

	const float ForGuard = UCataclysmCommand::SummonerMultiplierAgainst(Commander.Actor, TEXT("minion_damage"),
																		nullptr, Guard->TypeTags, Guard);
	const float ForHusk = UCataclysmCommand::SummonerMultiplierAgainst(Commander.Actor, TEXT("minion_damage"),
																	   nullptr, Husk->TypeTags, Husk);
	AddInfo(FString::Printf(TEXT("STANDING ARMY: guard x%.3f, husk x%.3f; guard health %.0f -> %.0f"), ForGuard,
							ForHusk, GuardHealthBefore, HealthOf(Guard, true)));
	TestEqual(TEXT("the guard's blow is raised by the row"), ForGuard, 1.0f + Damage / 100.0f, 0.001f);
	TestEqual(TEXT("and its health, live, by the row's share of its type's"), HealthOf(Guard, true),
			  GuardHealthBefore + TypeHealthFor(Commander.Actor, TEXT("BarrowGuard")) * Health / 100.0f, 0.5f);
	TestEqual(TEXT("CONTROL: the husk's blow is not"), ForHusk, 1.0f, 0.001f);
	TestEqual(TEXT("CONTROL: nor its health"), HealthOf(Husk, true), HuskHealthBefore, 0.01f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyLoneRevenantTest,
	"Cataclysm.NecromancerArmy.LoneRevenantMultipliesAPermanentMinionsBlowHealthAndReserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Lone Revenant, with Lighter Bones at its full points: a guard's blow and live health are multiplied by one plus the
 * "more" rows, and Barrow Guard's reserve by (1 - Lighter Bones) and then the reserve's "more". The flag the Abomination
 * reads is held. CONTROL: Shallow Graves' husk reserves what it did, and its blow is unchanged.
 */
bool FCataclysmArmyLoneRevenantTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	if (!TestNotNull(TEXT("set-up: two summon skills"), Guards && Graves ? Guards : nullptr))
	{
		return false;
	}
	const float GuardReserveBefore = Guards->FervourReserveOfOne(Commander.Actor);
	const float HuskReserveBefore = Graves->FervourReserveOfOne(Commander.Actor);
	ACataclysmMinion* Guard = Guards->SummonOne();
	ACataclysmMinion* Husk = Graves->SummonOne();
	if (!TestNotNull(TEXT("set-up: a guard and a husk"), Guard && Husk ? Guard : nullptr))
	{
		return false;
	}
	const float GuardHealthBefore = HealthOf(Guard, true);
	TestFalse(TEXT("CONTROL: with no row Lone Revenant is not held"),
			  UCataclysmNecromancerArmy::LoneRevenantHeld(Commander.Actor));

	const TCHAR* Revenant = TEXT("Necromancer_keystone_lg_kA");
	const TCHAR* Lighter = TEXT("Necromancer_basic_lg_b0");
	GiveNodes(Commander, {Revenant, Lighter});
	UCataclysmNecromancerArmy::HealthStep(Guard);
	const float More = PerPointOf(Revenant, TEXT("minion_damage"));
	const float HealthMore = PerPointOf(Revenant, TEXT("minion_health"));
	const float ReserveMore = PerPointOf(Revenant, TEXT("minion_reserve"));
	const float Less = FullOf(Lighter, TEXT("minion_reserve_percent_less"));

	const float GuardReserve = Guards->FervourReserveOfOne(Commander.Actor);
	AddInfo(FString::Printf(TEXT("LONE REVENANT: reserve %.2f -> %.2f, husk's %.2f -> %.2f"), GuardReserveBefore,
							GuardReserve, HuskReserveBefore, Graves->FervourReserveOfOne(Commander.Actor)));
	TestTrue(TEXT("Lone Revenant is held"), UCataclysmNecromancerArmy::LoneRevenantHeld(Commander.Actor));
	TestEqual(TEXT("the guard's blow is multiplied by the more row"),
			  UCataclysmCommand::SummonerMultiplierAgainst(Commander.Actor, TEXT("minion_damage"), nullptr,
														   Guard->TypeTags, Guard),
			  1.0f + More / 100.0f, 0.001f);
	TestEqual(TEXT("its health too, live"), HealthOf(Guard, true),
			  GuardHealthBefore * (1.0f + HealthMore / 100.0f), 1.0f);
	TestEqual(TEXT("Barrow Guard's reserve is lighter and then more"), GuardReserve,
			  FMath::Max(1.0f, GuardReserveBefore * (1.0f - Less / 100.0f) * (1.0f + ReserveMore / 100.0f)), 0.01f);
	TestEqual(TEXT("CONTROL: the husk's reserve is as it was"), Graves->FervourReserveOfOne(Commander.Actor),
			  HuskReserveBefore, 0.001f);
	TestEqual(TEXT("CONTROL: and its blow"),
			  UCataclysmCommand::SummonerMultiplierAgainst(Commander.Actor, TEXT("minion_damage"), nullptr,
														   Husk->TypeTags, Husk),
			  1.0f, 0.001f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyLoneRevenantAbominationTest,
	"Cataclysm.NecromancerArmy.LoneRevenantGivesTheAbominationFiveTimesNotTwentyFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Lone Revenant's 400% reaches the Abomination exactly once: the coordinating session's ruling at the bundle's assembly
 * (2026-11-26), "Abomination with Lone Revenant = base x 5, not x 25". Two paths could each apply it: slice B's
 * `minion_health` and `minion_damage` rows, which require `Minion.Permanent`, and slice D's multiplier on
 * `lone_revenant_held`. The Abomination carries no `Minion.Permanent`, so only slice D's applies.
 *
 * Two commanders, each killing a creature of the same kind, each raising an Abomination from that kill. One holds Lone
 * Revenant's real rows at full points, the other nothing. CONTROL: the same rows give a minion carrying
 * `Minion.Permanent` five times its health and blow, so they are live and would have made it 25 had they reached.
 */
bool FCataclysmArmyLoneRevenantAbominationTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Plain(World, FVector(0.0f, -20.0f * M, 0.0f));
	FArmyCommander Revenant(World, FVector(0.0f, 20.0f * M, 0.0f));
	FArmyMonster ForPlain(World, FVector(5.0f * M, -20.0f * M, 0.0f));
	FArmyMonster ForRevenant(World, FVector(5.0f * M, 20.0f * M, 0.0f));
	GiveNodes(Revenant, {TEXT("Necromancer_keystone_lg_kA")});
	TestTrue(TEXT("set-up: Lone Revenant is held"), UCataclysmNecromancerArmy::LoneRevenantHeld(Revenant.Actor));
	TestFalse(TEXT("CONTROL: and not by the other"), UCataclysmNecromancerArmy::LoneRevenantHeld(Plain.Actor));

	const auto RaiseFor = [](const FArmyCommander& Commander, const FArmyMonster& Victim)
	{
		FCataclysmDeathNotice Notice;
		Notice.Victim = Victim.Actor;
		Notice.Killer = Commander.Actor;
		Notice.Location = Victim.Actor->GetActorLocation();
		UCataclysmBoneGiants::NoteDeath(Commander.Actor, Notice);
		return UCataclysmBoneGiants::RaiseAbomination(Commander.Actor, Notice.Location);
	};
	ACataclysmMinion* PlainOne = RaiseFor(Plain, ForPlain);
	ACataclysmMinion* RevenantOne = RaiseFor(Revenant, ForRevenant);
	if (!TestNotNull(TEXT("CONTROL: the commander without Lone Revenant raises one"), PlainOne)
		|| !TestNotNull(TEXT("the commander with it raises one"), RevenantOne))
	{
		return false;
	}
	const FGameplayTag Permanent = UCataclysmNecromancerArmy::PermanentTag();
	TestFalse(TEXT("the Abomination carries no Minion.Permanent"), RevenantOne->TypeTags.HasTagExact(Permanent));

	// EACH AGAINST ITS OWN VICTIM, so a difference between the two creatures cannot read as Lone Revenant's.
	const float Five = 1.0f + UCataclysmBoneGiants::LoneRevenantMorePercent / 100.0f;
	const float HealthShare = UCataclysmBoneGiants::AbominationHealthSharePercent / 100.0f;
	const float BlowShare = UCataclysmBoneGiants::AbominationDamageSharePercent / 100.0f;
	const auto BlowAtASwing = [](const FArmyCommander& Commander, const ACataclysmMinion* Giant)
	{
		return Giant->OwnDamagePerHit
			* UCataclysmCommand::SummonerMultiplierAgainst(Commander.Actor, TEXT("minion_damage"), nullptr,
														   Giant->TypeTags, Giant);
	};
	const float PlainHealth = HealthOf(PlainOne, true);
	const float RevenantHealth = HealthOf(RevenantOne, true);
	const float PlainBlow = BlowAtASwing(Plain, PlainOne);
	const float RevenantBlow = BlowAtASwing(Revenant, RevenantOne);
	AddInfo(FString::Printf(TEXT("LONE REVENANT'S ABOMINATION: health %.1f -> %.1f, blow %.3f -> %.3f"), PlainHealth,
							RevenantHealth, PlainBlow, RevenantBlow));
	TestEqual(TEXT("CONTROL: without it, 10% of its creature's health"), PlainHealth,
			  HealthOf(ForPlain.Actor, true) * HealthShare, 0.5f);
	TestEqual(TEXT("CONTROL: and 5% of its blow"), PlainBlow, ForPlain.Actor->OwnAttackDamage() * BlowShare, 0.01f);
	TestEqual(TEXT("with it, five times that health, not 25"), RevenantHealth,
			  HealthOf(ForRevenant.Actor, true) * HealthShare * Five, 0.5f);
	TestEqual(TEXT("and five times that blow at a swing, not 25"), RevenantBlow,
			  ForRevenant.Actor->OwnAttackDamage() * BlowShare * Five, 0.01f);

	// CONTROL: THE ROWS ARE LIVE. A minion carrying `Minion.Permanent` is given five times by them.
	FGameplayTagContainer AsAPermanentMinion = RevenantOne->TypeTags;
	AsAPermanentMinion.AddTag(Permanent);
	TestEqual(TEXT("CONTROL: Lone Revenant's health row gives a permanent minion five times"),
			  UCataclysmCommand::SummonerMultiplierFor(Revenant.Actor, TEXT("minion_health"), AsAPermanentMinion), Five,
			  0.001f);
	TestEqual(TEXT("CONTROL: and its damage row five times"),
			  UCataclysmCommand::SummonerMultiplierAgainst(Revenant.Actor, TEXT("minion_damage"), nullptr,
														   AsAPermanentMinion, RevenantOne),
			  Five, 0.001f);
	ClearArmy(Plain.Actor);
	ClearArmy(Revenant.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyOldBonesTest,
	"Cataclysm.NecromancerArmy.OldBonesGrowsWithEveryFullThirtySecondsUpToFourTimes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Old Bones at its full points: a guard commanded 65 seconds ago is two steps in, one commanded 500 seconds ago is
 * held at four. CONTROL: one just raised is at none, and a husk that stood 65 seconds gains nothing.
 */
bool FCataclysmArmyOldBonesTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	// THE CLOCK STARTED AT 1,000 SECONDS, so the command times set back below stay above nought: a fresh test world's
	// clock is at nought, and `FillAskingMinionState` reads no age from a command time below it. Window dfb-w1 read every
	// guard at 1.000 for that reason.
	World->TimeSeconds = 1000.0f;
	ACataclysmMinion* Fresh = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Older = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Oldest = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Husk = Graves ? Graves->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: three guards and a husk"), Fresh && Older && Oldest && Husk ? Fresh : nullptr))
	{
		return false;
	}
	const float Now = World->GetTimeSeconds();
	Older->CommandedSinceSeconds = Now - 65.0f;
	Oldest->CommandedSinceSeconds = Now - 500.0f;
	Husk->CommandedSinceSeconds = Now - 65.0f;
	const TCHAR* Node = TEXT("Necromancer_basic_lg_a0");
	const float Each = FullOf(Node, TEXT("minion_damage"));
	GiveNodes(Commander, {Node});
	auto For = [&Commander](const ACataclysmMinion* Minion)
	{
		return UCataclysmCommand::SummonerMultiplierAgainst(Commander.Actor, TEXT("minion_damage"), nullptr,
															Minion->TypeTags, Minion);
	};
	AddInfo(FString::Printf(TEXT("OLD BONES: %.3f fresh, %.3f at 65 s, %.3f at 500 s, husk %.3f"), For(Fresh),
							For(Older), For(Oldest), For(Husk)));
	TestTrue(TEXT("set-up: Old Bones' row is in the table"), Each > 0.0f);
	TestEqual(TEXT("two full 30 seconds, two steps"), For(Older), 1.0f + 2.0f * Each / 100.0f, 0.001f);
	TestEqual(TEXT("held at four"), For(Oldest), 1.0f + 4.0f * Each / 100.0f, 0.001f);
	TestEqual(TEXT("CONTROL: none for one just raised"), For(Fresh), 1.0f, 0.001f);
	TestEqual(TEXT("CONTROL: none for a husk"), For(Husk), 1.0f, 0.001f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyFirstAmongTest,
	"Cataclysm.NecromancerArmy.FirstAmongTheDeadReachesOnlyThePermanentMinionWithTheMostHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * First Among the Dead at its full points: of two guards, the one with more maximum health deals more and gains health
 * at its step. CONTROL: the other gains neither.
 */
bool FCataclysmArmyFirstAmongTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	ACataclysmMinion* Big = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Small = Guards ? Guards->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: two guards"), Big && Small ? Big : nullptr))
	{
		return false;
	}
	SetHealthOf(Big, HealthOf(Big, true) + 500.0f, /*bMaximum=*/true);
	const float BigBefore = HealthOf(Big, true);
	const float SmallBefore = HealthOf(Small, true);
	const TCHAR* Node = TEXT("Necromancer_basic_lg_a1");
	const float Damage = FullOf(Node, TEXT("minion_damage"));
	const float Health = FullOf(Node, TEXT("minion_health"));
	GiveNodes(Commander, {Node});
	UCataclysmNecromancerArmy::HealthStep(Big);
	UCataclysmNecromancerArmy::HealthStep(Small);
	auto For = [&Commander](const ACataclysmMinion* Minion)
	{
		return UCataclysmCommand::SummonerMultiplierAgainst(Commander.Actor, TEXT("minion_damage"), nullptr,
															Minion->TypeTags, Minion);
	};
	AddInfo(FString::Printf(TEXT("FIRST AMONG: big x%.3f %.0f -> %.0f, small x%.3f %.0f -> %.0f"), For(Big), BigBefore,
							HealthOf(Big, true), For(Small), SmallBefore, HealthOf(Small, true)));
	TestTrue(TEXT("the bigger is first"), UCataclysmNecromancerArmy::HasTheMostMaximumHealth(Commander.Actor, Big));
	TestEqual(TEXT("its blow is raised"), For(Big), 1.0f + Damage / 100.0f, 0.001f);
	TestEqual(TEXT("and its health"), HealthOf(Big, true),
			  BigBefore + TypeHealthFor(Commander.Actor, TEXT("BarrowGuard")) * Health / 100.0f, 0.5f);
	TestFalse(TEXT("CONTROL: the smaller is not first"),
			  UCataclysmNecromancerArmy::HasTheMostMaximumHealth(Commander.Actor, Small));
	TestEqual(TEXT("CONTROL: its blow is not raised"), For(Small), 1.0f, 0.001f);
	TestEqual(TEXT("CONTROL: nor its health"), HealthOf(Small, true), SmallBefore, 0.01f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyMotleyHostTest,
	"Cataclysm.NecromancerArmy.MotleyHostCountsEachKindBeyondTheFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Motley Host at its full points: two guards are one kind and gain nothing; a husk beside them is a second kind and
 * every minion's blow gains one step, the husk's included.
 */
bool FCataclysmArmyMotleyHostTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* One = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Two = Guards ? Guards->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: two guards"), One && Two && Graves ? One : nullptr))
	{
		return false;
	}
	const TCHAR* Node = TEXT("Necromancer_basic_lg_stem2");
	const float Each = FullOf(Node, TEXT("minion_damage"));
	GiveNodes(Commander, {Node});
	auto For = [&Commander](const ACataclysmMinion* Minion)
	{
		return UCataclysmCommand::SummonerMultiplierAgainst(Commander.Actor, TEXT("minion_damage"), nullptr,
															Minion->TypeTags, Minion);
	};
	const int32 KindsOfGuards = UCataclysmNecromancerArmy::KindsBeyondTheFirst(
		UCataclysmCommand::ThingsCommandedBy(Commander.Actor));
	const float WithGuards = For(One);
	ACataclysmMinion* Husk = Graves->SummonOne();
	const int32 KindsWithAHusk = UCataclysmNecromancerArmy::KindsBeyondTheFirst(
		UCataclysmCommand::ThingsCommandedBy(Commander.Actor));
	AddInfo(FString::Printf(TEXT("MOTLEY HOST: %d beyond with guards (x%.3f), %d with a husk (x%.3f, husk x%.3f)"),
							KindsOfGuards, WithGuards, KindsWithAHusk, For(One), Husk ? For(Husk) : 0.0f));
	TestEqual(TEXT("CONTROL: two guards are one kind"), KindsOfGuards, 0);
	TestEqual(TEXT("CONTROL: and gain nothing"), WithGuards, 1.0f, 0.001f);
	TestEqual(TEXT("a husk is a second kind"), KindsWithAHusk, 1);
	TestEqual(TEXT("so a guard's blow gains a step"), For(One), 1.0f + Each / 100.0f, 0.001f);
	if (TestNotNull(TEXT("set-up: a husk"), Husk))
	{
		TestEqual(TEXT("and the husk's, whom Motley Host's Minion Damage reaches too"), For(Husk),
				  1.0f + Each / 100.0f, 0.001f);
	}
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyMastersEdgeTest,
	"Cataclysm.NecromancerArmy.MastersEdgeGivesAPermanentMinionAShareOfYourCriticalStrike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Master's Edge at its full points, the commander at 40% chance and 200% multiplier: a guard strikes critically at the
 * row's share of 40, for 150 plus the share of 200, and a pinned roll of 0 lands it, one of 100 does not. CONTROL: a
 * husk keeps no chance and 150.
 */
bool FCataclysmArmyMastersEdgeTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	Commander.Set(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 40.0f);
	Commander.Set(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(), 200.0f);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* Guard = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Husk = Graves ? Graves->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: a guard and a husk"), Guard && Husk ? Guard : nullptr))
	{
		return false;
	}
	const TCHAR* Node = TEXT("Necromancer_basic_lg_a2");
	const float Share = FullOf(Node, TEXT("minion_crit_share_of_yours_percent"));
	GiveNodes(Commander, {Node});
	float Chance = 0.0f;
	float Multiplier = 0.0f;
	UCataclysmNecromancerArmy::CriticalFiguresFor(Guard, Chance, Multiplier);
	float HuskChance = -1.0f;
	float HuskMultiplier = 0.0f;
	UCataclysmNecromancerArmy::CriticalFiguresFor(Husk, HuskChance, HuskMultiplier);
	float Landed = 0.0f;
	float Missed = 0.0f;
	{
		FPinnedArmyRoll Pinned(0.0f);
		Landed = UCataclysmNecromancerArmy::CriticalFactorForBlow(Guard);
	}
	{
		FPinnedArmyRoll Pinned(100.0f);
		Missed = UCataclysmNecromancerArmy::CriticalFactorForBlow(Guard);
	}
	AddInfo(FString::Printf(TEXT("MASTER'S EDGE: %.1f%% for %.0f%%; x%.2f landed, x%.2f not; husk %.1f%% %.0f%%"),
							Chance, Multiplier, Landed, Missed, HuskChance, HuskMultiplier));
	TestEqual(TEXT("the guard's chance is the share of 40"), Chance, 40.0f * Share / 100.0f, 0.001f);
	TestEqual(TEXT("its multiplier 150 and the share of 200"), Multiplier, 150.0f + 200.0f * Share / 100.0f, 0.001f);
	TestEqual(TEXT("a roll under the chance multiplies the blow"), Landed, Multiplier / 100.0f, 0.001f);
	TestEqual(TEXT("CONTROL: a roll above it does not"), Missed, 1.0f, 0.001f);
	TestEqual(TEXT("CONTROL: the husk has no chance"), HuskChance, 0.0f, 0.001f);
	TestEqual(TEXT("CONTROL: and its own 150"), HuskMultiplier, 150.0f, 0.001f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyBoneLordReforgedTest,
	"Cataclysm.NecromancerArmy.BoneLordRaisesMoreAndReforgedHealsWhenThereIsNoRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * After a press of Barrow Guard: Bone Lord's two more guards when it raised any, and Reforged's heal of the most
 * wounded guard by its row times its points of maximum health when it raised none for want of room. CONTROLS: with no
 * row a press raises no more; a press refused for anything but room heals nothing; and Shallow Graves, whose husks are
 * not permanent, raises no more.
 */
bool FCataclysmArmyBoneLordReforgedTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* Hurt = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Less = Guards ? Guards->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: two guards"), Hurt && Less && Graves ? Hurt : nullptr))
	{
		return false;
	}
	const int32 NoneWithNoRow = UCataclysmNecromancerArmy::AfterASummon(Commander.Actor, Guards, 2, false);

	const TCHAR* Lord = TEXT("Necromancer_keystone_lg_kB");
	const TCHAR* Reforged = TEXT("Necromancer_basic_lg_stem1");
	GiveNodes(Commander, {Lord, Reforged});
	const int32 Wanted = FMath::RoundToInt32(PerPointOf(Lord, TEXT("summon_raises_more_permanent_minions")));
	const int32 FromGraves = UCataclysmNecromancerArmy::MorePermanentToRaise(Commander.Actor, Graves);
	const int32 More = UCataclysmNecromancerArmy::AfterASummon(Commander.Actor, Guards, 2, false);

	const float Maximum = HealthOf(Hurt, true);
	SetHealthOf(Hurt, Maximum * 0.25f);
	SetHealthOf(Less, HealthOf(Less, true) * 0.9f);
	UCataclysmNecromancerArmy::AfterASummon(Commander.Actor, Guards, 0, /*bRefusedForRoom=*/false);
	const float AfterNotForRoom = HealthOf(Hurt);
	UCataclysmNecromancerArmy::AfterASummon(Commander.Actor, Guards, 0, /*bRefusedForRoom=*/true);
	const float Heal = FullOf(Reforged, TEXT("summon_without_room_heals_permanent_percent"));

	AddInfo(FString::Printf(TEXT("BONE LORD: %d more with no row, %d with it (%d wanted), %d for Shallow Graves; "
								 "REFORGED: %.0f -> %.0f of %.0f"),
							NoneWithNoRow, More, Wanted, FromGraves, Maximum * 0.25f, HealthOf(Hurt), Maximum));
	TestEqual(TEXT("CONTROL: with no row a press raises no more"), NoneWithNoRow, 0);
	TestEqual(TEXT("Bone Lord raises its two more"), More, Wanted);
	TestEqual(TEXT("so six guards stand"), CountOf(Commander.Actor, TEXT("BarrowGuard")), 2 + Wanted);
	TestEqual(TEXT("CONTROL: Shallow Graves raises no more"), FromGraves, 0);
	TestEqual(TEXT("CONTROL: a press refused for anything but room heals nothing"), AfterNotForRoom, Maximum * 0.25f,
			  0.01f);
	TestEqual(TEXT("Reforged heals the most wounded guard by its share of maximum health"), HealthOf(Hurt),
			  FMath::Min(Maximum, Maximum * 0.25f + Maximum * Heal / 100.0f), 0.5f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyWillNotStayDownTest,
	"Cataclysm.NecromancerArmy.WillNotStayDownRaisesAFallenGuardAgainThreeSecondsLater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Will Not Stay Down at its full points, the roll pinned at 0: a guard that dies rises again where it fell 3 seconds
 * later, and not before. CONTROL: with the roll pinned at 100 it does not.
 */
bool FCataclysmArmyWillNotStayDownTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	if (!TestNotNull(TEXT("set-up: Barrow Guard"), Guards))
	{
		return false;
	}
	GiveNodes(Commander, {TEXT("Necromancer_basic_lg_b1")});
	int32 Standing[2] = {0, 0};
	int32 Early[2] = {0, 0};
	for (int32 Case = 0; Case < 2; ++Case)
	{
		FPinnedArmyRoll Pinned(Case == 0 ? 0.0f : 100.0f);
		ACataclysmMinion* Guard = Guards->SummonOne();
		if (!TestNotNull(TEXT("set-up: a guard"), Guard))
		{
			return false;
		}
		SetHealthOf(Guard, 0.0f);
		// THE CLOCK RUN IN FRAMES, by `CataclysmTestWorld::RunClock`: two `FTimerManager::Tick` calls in one engine frame
		// tick once (`HasBeenTickedThisFrame`), so window dfb-w1 reached only half the rise and saw none.
		CataclysmTestWorld::RunClock(World, UCataclysmNecromancerArmy::RisesAgainAfterSeconds * 0.5f);
		Early[Case] = CountOf(Commander.Actor, TEXT("BarrowGuard"));
		CataclysmTestWorld::RunClock(World, UCataclysmNecromancerArmy::RisesAgainAfterSeconds * 0.6f);
		Standing[Case] = CountOf(Commander.Actor, TEXT("BarrowGuard"));
		ClearArmy(Commander.Actor);
	}
	AddInfo(FString::Printf(TEXT("WILL NOT STAY DOWN: %d then %d at roll 0; %d then %d at roll 100"), Early[0],
							Standing[0], Early[1], Standing[1]));
	TestEqual(TEXT("not risen before 3 seconds"), Early[0], 0);
	TestEqual(TEXT("risen again after them"), Standing[0], 1);
	TestEqual(TEXT("CONTROL: a roll above the chance raises nothing"), Standing[1], 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmySecondDeathTest,
	"Cataclysm.NecromancerArmy.SecondDeathRaisesAnImpThatDiesAsAHuskAsLargeAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Second Death: an Imp, which did not rise from the dead, dies and rises where it fell as a husk with the Imp's maximum
 * health. CONTROLS: a Barrow Guard, a Death summon's, raises nothing; and with no row the Imp raises nothing.
 */
bool FCataclysmArmySecondDeathTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	if (!TestNotNull(TEXT("set-up: Barrow Guard"), Guards))
	{
		return false;
	}
	ACataclysmMinion* Plain = ACataclysmMinion::Spawn(Commander.Actor, FVector(3.0f * M, 0, 0), 60.0f, false,
													  TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: an Imp"), Plain))
	{
		return false;
	}
	SetHealthOf(Plain, 0.0f);
	const int32 HusksWithNoRow = CountOf(Commander.Actor, TEXT("Husk"));

	GiveNodes(Commander, {TEXT("Necromancer_keystone_spine_001")});
	ACataclysmMinion* Guard = Guards->SummonOne();
	if (Guard)
	{
		SetHealthOf(Guard, 0.0f);
	}
	const int32 HusksFromAGuard = CountOf(Commander.Actor, TEXT("Husk"));
	ACataclysmMinion* Imp = ACataclysmMinion::Spawn(Commander.Actor, FVector(-3.0f * M, 0, 0), 60.0f, false,
													TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: a second Imp"), Imp))
	{
		return false;
	}
	const float ImpHealth = HealthOf(Imp, true);
	SetHealthOf(Imp, 0.0f);
	const TArray<ACataclysmMinion*> Husks = UCataclysmCommand::MinionsOfTypeCommandedBy(Commander.Actor, TEXT("Husk"));
	AddInfo(FString::Printf(TEXT("SECOND DEATH: %d husks with no row, %d after a guard, %d after an Imp of %.0f"),
							HusksWithNoRow, HusksFromAGuard, Husks.Num(), ImpHealth));
	TestEqual(TEXT("CONTROL: with no row an Imp raises nothing"), HusksWithNoRow, 0);
	TestEqual(TEXT("CONTROL: a Barrow Guard rose from the dead and raises nothing"), HusksFromAGuard, 0);
	if (TestEqual(TEXT("the Imp rises as one husk"), Husks.Num(), 1))
	{
		TestEqual(TEXT("as large as it was"), HealthOf(Husks[0], true), ImpHealth, 0.5f);
		TestTrue(TEXT("where it fell"), FVector::Dist2D(Husks[0]->GetActorLocation(), FVector(-3.0f * M, 0, 0)) < 1.0f);
		TestTrue(TEXT("and it rose from the dead, so it will not rise again"),
				 UCataclysmNecromancerArmy::RoseFromTheDead(Husks[0]));
	}
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyCarrionHostTest,
	"Cataclysm.NecromancerArmy.HarvestOfTheHostRaisesAHuskFromAPermanentMinionsKillOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Harvest of the Host: a monster a guard kills rises as a husk where it fell. CONTROLS: one the commander kills itself
 * raises nothing, and one a husk kills raises nothing (a husk is not permanent).
 */
bool FCataclysmArmyCarrionHostTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* Guard = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Husk = Graves ? Graves->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: a guard and a husk"), Guard && Husk ? Guard : nullptr))
	{
		return false;
	}
	GiveNodes(Commander, {TEXT("Necromancer_keystone_spine_003")});
	const int32 Before = CountOf(Commander.Actor, TEXT("Husk"));
	auto KillBy = [World](AActor* Killer, const FVector& Where)
	{
		FArmyMonster Monster(World, Where);
		UCataclysmSkillEffects::ApplyDirectDamage(Killer, Monster.Actor, 1.0e7f, PlainBlow(Killer));
		return UCataclysmSkillEffects::IsDead(Monster.Actor);
	};
	const bool bByCommander = KillBy(Commander.Actor, FVector(10.0f * M, 0, 0));
	const int32 AfterCommander = CountOf(Commander.Actor, TEXT("Husk"));
	const bool bByHusk = KillBy(Husk, FVector(10.0f * M, 5.0f * M, 0));
	const int32 AfterHusk = CountOf(Commander.Actor, TEXT("Husk"));
	const bool bByGuard = KillBy(Guard, FVector(10.0f * M, -5.0f * M, 0));
	const int32 AfterGuard = CountOf(Commander.Actor, TEXT("Husk"));
	AddInfo(FString::Printf(TEXT("HARVEST: husks %d, %d after the commander's kill, %d after a husk's, %d after a "
								 "guard's"), Before, AfterCommander, AfterHusk, AfterGuard));
	TestTrue(TEXT("set-up: every monster died"), bByCommander && bByHusk && bByGuard);
	TestEqual(TEXT("CONTROL: the commander's own kill raises nothing"), AfterCommander, Before);
	TestEqual(TEXT("CONTROL: a husk's kill raises nothing"), AfterHusk, Before);
	TestEqual(TEXT("a guard's kill rises as a husk"), AfterGuard, Before + 1);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyRestlessDeadTest,
	"Cataclysm.NecromancerArmy.TheRestlessDeadRaiseAQuarterOfAHuskFromYourKill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Restless Dead, the roll pinned at 0: a monster the commander kills rises as a skeleton, a husk's body with a
 * quarter of its health and blow, lasting the husk's seconds. CONTROLS: with the roll pinned at 100 nothing rises, and
 * with no row nothing rises.
 */
bool FCataclysmArmyRestlessDeadTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	auto Kill = [World, &Commander](float Roll)
	{
		FPinnedArmyRoll Pinned(Roll);
		FArmyMonster Monster(World, FVector(10.0f * M, 0, 0));
		UCataclysmSkillEffects::ApplyDirectDamage(Commander.Actor, Monster.Actor, 1.0e7f, PlainBlow());
	};
	Kill(0.0f);
	const int32 WithNoRow = CountOf(Commander.Actor, TEXT("Husk"));
	GiveNodes(Commander, {}, {{TEXT("Necromancer_capstone_25"), 1}});
	Kill(100.0f);
	const int32 RolledAbove = CountOf(Commander.Actor, TEXT("Husk"));
	Kill(0.0f);
	const TArray<ACataclysmMinion*> Risen = UCataclysmCommand::MinionsOfTypeCommandedBy(Commander.Actor, TEXT("Husk"));

	// A HUSK'S OWN FIGURES AT THIS LEVEL, from a husk Shallow Graves raises for the same commander.
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* Whole = Graves ? Graves->SummonOne() : nullptr;
	AddInfo(FString::Printf(TEXT("RESTLESS DEAD: %d with no row, %d at roll 100, %d at roll 0"), WithNoRow, RolledAbove,
							Risen.Num()));
	TestEqual(TEXT("CONTROL: with no row nothing rises"), WithNoRow, 0);
	TestEqual(TEXT("CONTROL: a roll above the chance raises nothing"), RolledAbove, 0);
	if (TestEqual(TEXT("one skeleton rises"), Risen.Num(), 1) && TestNotNull(TEXT("set-up: a whole husk"), Whole))
	{
		TestEqual(TEXT("with a quarter of a husk's health"), HealthOf(Risen[0], true),
				  HealthOf(Whole, true) * UCataclysmNecromancerArmy::SkeletonShareOfAHusk, 0.5f);
		TestEqual(TEXT("and a quarter of its blow"), Risen[0]->OwnDamagePerHit,
				  Whole->OwnDamagePerHit * UCataclysmNecromancerArmy::SkeletonShareOfAHusk, 0.01f);
		TestTrue(TEXT("for the husk's seconds"), Risen[0]->GetLifeSpan() > 0.0f);
	}
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyLichFormTest,
	"Cataclysm.NecromancerArmy.LichFormRaisesAHuskWhereASkillLandsForTenSecondsAfterAFullBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Lich Form: the bar filled, a skill used raises a husk at the point it was aimed at, through the skill-use notice.
 * CONTROLS: before the bar has filled nothing rises, and the basic attack raises nothing.
 */
bool FCataclysmArmyLichFormTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	GiveNodes(Commander, {}, {{TEXT("Necromancer_capstone_100"), 1}});
	const FVector Aim(8.0f * M, 2.0f * M, 0.0f);
	const FGameplayTagContainer NoTags;
	Commander.AbilitySystem->ActOnSkillUse(FName(TEXT("A skill")), &NoTags, Aim, /*bBasicAttack=*/false,
										   /*bHasCooldown=*/true);
	const int32 BeforeFull = CountOf(Commander.Actor, TEXT("Husk"));
	Commander.AbilitySystem->NoteClassResourceFull();
	Commander.AbilitySystem->ActOnSkillUse(FName(TEXT("The basic attack")), &NoTags, Aim, /*bBasicAttack=*/true,
										   /*bHasCooldown=*/false);
	const int32 AfterBasic = CountOf(Commander.Actor, TEXT("Husk"));
	Commander.AbilitySystem->ActOnSkillUse(FName(TEXT("A skill")), &NoTags, Aim, /*bBasicAttack=*/false,
										   /*bHasCooldown=*/true);
	const TArray<ACataclysmMinion*> Husks = UCataclysmCommand::MinionsOfTypeCommandedBy(Commander.Actor, TEXT("Husk"));
	AddInfo(FString::Printf(TEXT("LICH FORM: %d before the bar filled, %d after a basic attack, %d after a skill"),
							BeforeFull, AfterBasic, Husks.Num()));
	TestEqual(TEXT("CONTROL: nothing before the bar fills"), BeforeFull, 0);
	TestEqual(TEXT("CONTROL: nothing for the basic attack"), AfterBasic, 0);
	if (TestEqual(TEXT("a skill raises one husk"), Husks.Num(), 1))
	{
		TestTrue(TEXT("where it was aimed"), FVector::Dist2D(Husks[0]->GetActorLocation(), Aim) < 1.0f);
	}
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyGraveSwapTest,
	"Cataclysm.NecromancerArmy.GraveSwapChoosesTheMinionNearestTheAimedPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Grave Swap: of two guards, a move aimed beside the far one trades places with it, each standing where the other
 * stood. CONTROLS: aimed at empty ground nothing is chosen, and with no row nothing is chosen.
 */
bool FCataclysmArmyGraveSwapTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	ACataclysmMinion* Near = ACataclysmMinion::Spawn(Commander.Actor, FVector(3.0f * M, 0, 0), 60.0f, false,
													 TEXT("Imp"));
	ACataclysmMinion* Far = ACataclysmMinion::Spawn(Commander.Actor, FVector(9.0f * M, 0, 0), 60.0f, false,
													TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: two Imps"), Near && Far ? Near : nullptr))
	{
		return false;
	}
	AActor* Partner = nullptr;
	FVector SelfTo;
	FVector PartnerTo;
	const bool bWithNoRow = UCataclysmNecromancerArmy::ChooseGraveSwapPartner(
		Commander.Actor, FVector(9.2f * M, 0, 0), 12.0f * M, Partner, SelfTo, PartnerTo);
	GiveNodes(Commander, {}, {{TEXT("Necromancer_capstone_50"), 1}});
	const bool bAtNothing = UCataclysmNecromancerArmy::ChooseGraveSwapPartner(
		Commander.Actor, FVector(6.0f * M, 6.0f * M, 0), 12.0f * M, Partner, SelfTo, PartnerTo);
	const bool bAtFar = UCataclysmNecromancerArmy::ChooseGraveSwapPartner(
		Commander.Actor, FVector(9.2f * M, 0, 0), 12.0f * M, Partner, SelfTo, PartnerTo);
	AddInfo(FString::Printf(TEXT("GRAVE SWAP: %d with no row, %d at nothing, %d at the far Imp (%s)"),
							bWithNoRow, bAtNothing, bAtFar, *GetNameSafe(Partner)));
	TestFalse(TEXT("CONTROL: with no row nothing is chosen"), bWithNoRow);
	TestFalse(TEXT("CONTROL: aimed at empty ground nothing is chosen"), bAtNothing);
	TestTrue(TEXT("aimed beside the far Imp, it is chosen"), bAtFar && Partner == Far);
	TestTrue(TEXT("the mover goes where it stood"), FVector::Dist2D(SelfTo, Far->GetActorLocation()) < 1.0f);
	TestTrue(TEXT("and it where the mover stood"), FVector::Dist2D(PartnerTo, FVector::ZeroVector) < 1.0f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmySharedFleshTest,
	"Cataclysm.NecromancerArmy.SharedFleshSplitsAHitEvenlyBetweenYouAndYourMinions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Shared Flesh with two Imps: a hit of 3,000 costs the commander a third of what it costs with no row, and each Imp
 * takes some. CONTROL: with no row the commander takes it all and the Imps nothing; a tick is not split.
 */
bool FCataclysmArmySharedFleshTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	FArmyCommander Attacker(World, FVector(5.0f * M, 5.0f * M, 0));
	ACataclysmMinion* One = ACataclysmMinion::Spawn(Commander.Actor, FVector(3.0f * M, 0, 0), 60.0f, false,
													TEXT("Imp"));
	ACataclysmMinion* Two = ACataclysmMinion::Spawn(Commander.Actor, FVector(-3.0f * M, 0, 0), 60.0f, false,
													TEXT("Imp"));
	if (!TestNotNull(TEXT("set-up: two Imps"), One && Two ? One : nullptr))
	{
		return false;
	}
	const float Hit = 3000.0f;
	float Before = Commander.Health();
	UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Commander.Actor, Hit, PlainBlow());
	const float LostWithNoRow = Before - Commander.Health();
	const float ImpWithNoRow = HealthOf(One, true) - HealthOf(One);

	GiveNodes(Commander, {}, {{TEXT("Necromancer_capstone_100"), 2}});
	const float TickKept = UCataclysmNecromancerArmy::SplitWithMinions(Commander.AbilitySystem, Attacker.Actor, Hit,
																	   /*bIsDamageOverTime=*/true);
	Before = Commander.Health();
	const float OneBefore = HealthOf(One);
	UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Commander.Actor, Hit, PlainBlow());
	const float Lost = Before - Commander.Health();
	AddInfo(FString::Printf(TEXT("SHARED FLESH: %.0f lost with no row, %.0f with it; an Imp lost %.0f"), LostWithNoRow,
							Lost, OneBefore - HealthOf(One)));
	TestTrue(TEXT("set-up: the hit lands"), LostWithNoRow > 0.0f);
	TestEqual(TEXT("CONTROL: with no row an Imp takes nothing"), ImpWithNoRow, 0.0f, 0.001f);
	TestEqual(TEXT("CONTROL: a tick is not split"), TickKept, Hit, 0.001f);
	TestEqual(TEXT("the commander keeps a third of the hit"), Lost, LostWithNoRow / 3.0f, 1.0f);
	TestTrue(TEXT("and an Imp takes its part"), HealthOf(One) < OneBefore);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyCalledToHeelTest,
	"Cataclysm.NecromancerArmy.CalledToHeelLowersTheDamageAPermanentMinionNearYouTakes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Called to Heel at its full points: of two guards struck alike, the one 3 metres away takes less by the row; the one 15
 * metres away takes the whole. CONTROL: a husk 3 metres away takes the whole.
 */
bool FCataclysmArmyCalledToHeelTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	FArmyCommander Attacker(World, FVector(0, 20.0f * M, 0));
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* Near = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Far = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Husk = Graves ? Graves->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: two guards and a husk"), Near && Far && Husk ? Near : nullptr))
	{
		return false;
	}
	Near->SetActorLocation(FVector(3.0f * M, 0, 0));
	Far->SetActorLocation(FVector(15.0f * M, 0, 0));
	Husk->SetActorLocation(FVector(-3.0f * M, 0, 0));
	const TCHAR* Node = TEXT("Necromancer_basic_lg_c1");
	const float Reduced = FullOf(Node, TEXT("minion_damage_taken"));
	GiveNodes(Commander, {Node});
	auto Lost = [&Attacker](ACataclysmMinion* Minion)
	{
		const float Before = HealthOf(Minion);
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker.Actor, Minion, 100.0f, PlainBlow());
		return Before - HealthOf(Minion);
	};
	const float NearLost = Lost(Near);
	const float FarLost = Lost(Far);
	const float HuskLost = Lost(Husk);
	AddInfo(FString::Printf(TEXT("CALLED TO HEEL: %.2f near, %.2f far, %.2f the husk"), NearLost, FarLost, HuskLost));
	TestTrue(TEXT("set-up: the blows land"), FarLost > 0.0f);
	TestEqual(TEXT("the near guard takes less by the row"), NearLost, FarLost * (1.0f + Reduced / 100.0f), 0.05f);
	TestEqual(TEXT("CONTROL: the husk takes the whole"), HuskLost, FarLost, 0.05f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyMendingDeadTest,
	"Cataclysm.NecromancerArmy.MendingDeadRegeneratesAPermanentMinionEverySecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Mending Dead at its full points: a guard at half health regenerates its rows' share of its maximum health in a
 * one-second step. CONTROL: a husk at half health does not.
 */
bool FCataclysmArmyMendingDeadTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	ACataclysmMinion* Guard = Guards ? Guards->SummonOne() : nullptr;
	ACataclysmMinion* Husk = Graves ? Graves->SummonOne() : nullptr;
	if (!TestNotNull(TEXT("set-up: a guard and a husk"), Guard && Husk ? Guard : nullptr))
	{
		return false;
	}
	const TCHAR* Node = TEXT("Necromancer_basic_lg_c0");
	// EVERY STEP'S ROW, one each 2 points: the rows of a "for every 2 points" sentence are fixed figures from their
	// points, so the node at its full points holds them all.
	float Percent = 0.0f;
	for (const FCataclysmPassiveEffectRow* Effect :
		 UCataclysmPassiveTree::EffectsFor(UCataclysmPassiveTree::LoadEffectTable(), FName(Node)))
	{
		Percent += Effect->MinPoints <= MaxPointsOf(Node) ? Effect->ValuePerPoint : 0.0f;
	}
	GiveNodes(Commander, {Node});
	SetHealthOf(Guard, HealthOf(Guard, true) * 0.5f);
	SetHealthOf(Husk, HealthOf(Husk, true) * 0.5f);
	const float GuardBefore = HealthOf(Guard);
	const float HuskBefore = HealthOf(Husk);
	UCataclysmNecromancerArmy::Step(Guard, 1.0f);
	UCataclysmNecromancerArmy::Step(Husk, 1.0f);
	AddInfo(FString::Printf(TEXT("MENDING DEAD: %.1f%% a second; guard %.0f -> %.0f, husk %.0f -> %.0f"), Percent,
							GuardBefore, HealthOf(Guard), HuskBefore, HealthOf(Husk)));
	TestTrue(TEXT("set-up: Mending Dead's rows are in the table"), Percent > 0.0f);
	TestEqual(TEXT("the guard regenerates its share"), HealthOf(Guard),
			  GuardBefore + HealthOf(Guard, true) * Percent / 100.0f, 0.5f);
	TestEqual(TEXT("CONTROL: the husk does not"), HealthOf(Husk), HuskBefore, 0.01f);
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyRiseTogetherTest,
	"Cataclysm.NecromancerArmy.RiseTogetherRaisesASecondGuardBesideTheFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Rise Together at its full points, the roll pinned at 0: one guard raised by Barrow Guard raises a second beside it,
 * which does not roll again. CONTROLS: with the roll pinned at 100 one guard stands; Shallow Graves' husk, which is not
 * permanent, raises no second with the roll at 0.
 */
bool FCataclysmArmyRiseTogetherTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FArmyCommander Commander(World, FVector::ZeroVector);
	UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
	UCataclysmSummonSkill* Graves = ShallowGravesSkill(Commander);
	if (!TestNotNull(TEXT("set-up: two summon skills"), Guards && Graves ? Guards : nullptr))
	{
		return false;
	}
	const TCHAR* Node = TEXT("Necromancer_basic_lg_b2");
	GiveNodes(Commander, {Node});
	const float Chance = UCataclysmNecromancerArmy::ChanceToRaiseASecond(Commander.Actor, Guards);
	int32 Above = 0;
	{
		FPinnedArmyRoll Pinned(100.0f);
		Guards->SummonOne();
		Above = CountOf(Commander.Actor, TEXT("BarrowGuard"));
	}
	ClearArmy(Commander.Actor);
	ACataclysmMinion* First = nullptr;
	int32 Husks = 0;
	{
		FPinnedArmyRoll Pinned(0.0f);
		First = Guards->SummonOne();
		Graves->SummonOne();
		Husks = CountOf(Commander.Actor, TEXT("Husk"));
	}
	const TArray<ACataclysmMinion*> Standing =
		UCataclysmCommand::MinionsOfTypeCommandedBy(Commander.Actor, TEXT("BarrowGuard"));
	AddInfo(FString::Printf(TEXT("RISE TOGETHER: %.0f%%; %d guard(s) at roll 100, %d at roll 0, %d husk(s)"), Chance,
							Above, Standing.Num(), Husks));
	TestEqual(TEXT("the chance is the row's at full points"), Chance,
			  FullOf(Node, TEXT("permanent_minion_raises_a_second_chance")), 0.001f);
	TestEqual(TEXT("CONTROL: a roll above the chance raises one"), Above, 1);
	TestEqual(TEXT("CONTROL: a husk raises no second"), Husks, 1);
	if (TestEqual(TEXT("a roll under it raises a second, and the second does not roll again"), Standing.Num(), 2)
		&& TestNotNull(TEXT("set-up: the first guard"), First))
	{
		const ACataclysmMinion* Second = Standing[0] == First ? Standing[1] : Standing[0];
		TestTrue(TEXT("beside the first"),
				 FVector::Dist2D(Second->GetActorLocation(), First->GetActorLocation())
					 <= UCataclysmNecromancerArmy::SecondRisesBesideCm + 1.0f);
	}
	ClearArmy(Commander.Actor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmArmyRiseTogetherUseTest,
	"Cataclysm.NecromancerArmy.RiseTogetherRollsForEachGuardAUseRaisesWhileTheReserveFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Rise Together's chance, `permanent_minion_raises_a_second_chance`, ROLLED FOR EACH PERMANENT MINION A USE RAISES, the
 * second only when the reserve fits: the coordinating session's ruling at the bundle's assembly (2026-11-26), "a Barrow
 * Guard use at 100% raises 4; at 0 raises 2; with no room, no second". Each case USES the Greatsword's Barrow Guard,
 * which raises two, on a commander of its own. At 100%, four; with room for three and a half guards' reserve, three:
 * the second guard's second has no room. THE CONTROL: at nought, two. The roll is pinned at 99, under 100, so the 100%
 * case cannot miss on a roll of exactly 100.
 */
bool FCataclysmArmyRiseTogetherUseTest::RunTest(const FString&)
{
	using namespace CataclysmNecromancerArmyTest;
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FPinnedArmyRoll Pinned(99.0f);

	// A USE AT `ChancePercent`, with a pool of `ReservesOfRoom` guards' reserve when it is above nought; the guards
	// standing after it.
	const auto Use = [this, World](float ChancePercent, float ReservesOfRoom, float AcrossMetres) -> int32
	{
		FArmyCommander Commander(World, FVector(0.0f, AcrossMetres * M, 0.0f));
		UCataclysmSummonSkill* Guards = BarrowGuardSkill(Commander);
		if (!TestNotNull(TEXT("set-up: Barrow Guard"), Guards))
		{
			return -1;
		}
		if (ChancePercent > 0.0f)
		{
			FCataclysmStatModifier Chance;
			Chance.Bucket = ECataclysmStatBucket::Flat;
			Chance.Source = ECataclysmModifierSource::PassiveKeystone;
			Chance.Value = ChancePercent;
			TMap<FName, FCataclysmStatInputs> Inputs;
			Inputs.FindOrAdd(FName(UCataclysmNecromancerArmy::RaisesASecondChanceStat)).Modifiers = {Chance};
			Commander.AbilitySystem->SetStatInputs(MoveTemp(Inputs));
		}
		TestEqual(TEXT("set-up: the chance is what the case gives"),
				  UCataclysmNecromancerArmy::ChanceToRaiseASecond(Commander.Actor, Guards), ChancePercent, 0.001f);
		if (ReservesOfRoom > 0.0f)
		{
			const float Pool = Guards->FervourReserveOfOne(Commander.Actor) * ReservesOfRoom;
			Commander.Set(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Pool);
			Commander.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Pool);
		}
		if (!TestTrue(TEXT("set-up: the use is made"),
					  Commander.AbilitySystem->TryActivateAbility(Guards->GetCurrentAbilitySpecHandle(),
																  /*bAllowRemoteActivation=*/false)))
		{
			return -1;
		}
		const int32 Standing = CountOf(Commander.Actor, TEXT("BarrowGuard"));
		ClearArmy(Commander.Actor);
		return Standing;
	};

	const int32 AtAHundred = Use(100.0f, 0.0f, 0.0f);
	const int32 AtNought = Use(0.0f, 0.0f, 40.0f);
	const int32 ShortOfRoom = Use(100.0f, 3.5f, 80.0f);
	AddInfo(FString::Printf(TEXT("RISE TOGETHER, A USE: %d guard(s) at 100%%, %d at 0%%, %d at 100%% with room for 3.5"),
							AtAHundred, AtNought, ShortOfRoom));
	TestEqual(TEXT("at 100%, a use raising two raises four: each guard rolled"), AtAHundred, 4);
	TestEqual(TEXT("CONTROL: at nought, two"), AtNought, 2);
	TestEqual(TEXT("with room for three, three: no second without room"), ShortOfRoom, 3);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
