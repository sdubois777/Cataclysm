// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmBerserkerStates.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWarFervour.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmPlayerEffects.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * THREE BERSERKER NODES of 2026-11-25, on the coordinating session's rulings of that day: Skull Splitter
 * (`Berserker_twoh_003`), Deathmark (`Berserker_crit_004`) and Blood Price (`Berserker_trunk_008`).
 *
 * WHAT IS EXERCISED THROUGH ITS REAL SITE: the critical strike (`ACataclysmPlayerCharacter::OnSomethingWasHit`, a hit
 * notice announced on a real possessed player, as `Cataclysm.WarFervour.` announces one), the pool emptying
 * (`ACataclysmPlayerCharacter::OnClassResourceChanged`, reached through the out-of-combat decay), the damage step
 * (`UCataclysmDamageCalculation::Resolve`) and a blow (`UCataclysmSkillEffects::ApplyHit`). THE ROWS are read through THE
 * GENERATED TABLE and judged by the pipeline with a state written by hand, since a test player wields no weapon.
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmSkullDeathmarkBloodTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** Sets a console variable and puts it back at scope exit. */
	struct FSkullDeathmarkBloodPinned
	{
		FSkullDeathmarkBloodPinned(const TCHAR* Name, float Value)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Variable)
			{
				Previous = Variable->GetFloat();
				Variable->Set(Value, ECVF_SetByConsole);
			}
		}
		~FSkullDeathmarkBloodPinned()
		{
			if (Variable)
			{
				Variable->Set(Previous, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
		float Previous = 0.0f;
	};

	/** One flat stat line, optionally stated under one condition. */
	struct FSkullDeathmarkBloodStat
	{
		const TCHAR* Stat = nullptr;
		float Value = 0.0f;
		ECataclysmStatCondition Condition = ECataclysmStatCondition::Always;
	};

	/** REPLACES THE WHOLE STAT LINE of `System` with these flat rows. */
	void HoldSkullDeathmarkBloodStats(UCataclysmAbilitySystemComponent* System,
									  const TArray<FSkullDeathmarkBloodStat>& Rows)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const FSkullDeathmarkBloodStat& Each : Rows)
		{
			FCataclysmStatModifier Row;
			Row.Bucket = ECataclysmStatBucket::Flat;
			Row.Source = ECataclysmModifierSource::PassiveKeystone;
			Row.Value = Each.Value;
			Row.Condition = Each.Condition;
			FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(Each.Stat));
			Line.Base = 0.0f;
			Line.Modifiers.Add(Row);
		}
		System->SetStatInputs(MoveTemp(Stats));
	}

	/** A possessed player and its ability system, with a pool of `Maximum` holding `Held`. */
	struct FSkullDeathmarkBloodPlayer
	{
		explicit FSkullDeathmarkBloodPlayer(UWorld* World)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			System = State->GetCataclysmAbilitySystemComponent();
		}
		bool IsUsable() const { return Character && System; }
		float Fervour() const
		{
			return System->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}
		void Pool(float Maximum, float Held) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Maximum);
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Held);
		}
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A hostile creature `X` centimetres along the line, for a critical strike to land on. */
	ACataclysmEnemyCharacter* SkullDeathmarkBloodEnemy(UWorld* World, float X)
	{
		ACataclysmEnemyCharacter* Enemy =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector(X, 0.0f, 0.0f), FRotator::ZeroRotator);
		if (Enemy)
		{
			Enemy->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		}
		return Enemy;
	}

	/** The Cataclysm ability system of `Actor`, or null. */
	UCataclysmAbilitySystemComponent* SkullDeathmarkBloodSystemOf(const ACataclysmEnemyCharacter* Actor)
	{
		return Actor ? Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent()) : nullptr;
	}

	/** A hit notice announced as the game announces one: `Player`'s blow on `Target`, critical or not, with no tags. */
	void SkullDeathmarkBloodAnnounce(UCataclysmCombatEvents* Events, const FSkullDeathmarkBloodPlayer& Player,
									 AActor* Target, bool bCritical)
	{
		static const FGameplayTagContainer NoTags;
		FCataclysmHitNotice Notice;
		Notice.Attacker = Player.Character;
		Notice.Target = Target;
		Notice.SkillTags = &NoTags;
		Notice.DealtToHealth = 50.0f;
		Notice.bCritical = bCritical;
		Events->OnHit.Broadcast(Notice);
	}

	/** A bare actor holding the four combat sets and a class resource set, with a hundred thousand health. */
	struct FSkullDeathmarkBloodFighter
	{
		FSkullDeathmarkBloodFighter(UWorld* World, float AttackDamage)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			// Raw pointers on purpose: AddAttributeSetSubobject is a template and a TObjectPtr deduces the wrapper.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmAllResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 100000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 100000.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), AttackDamage);
		}
		~FSkullDeathmarkBloodFighter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}
		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** One blow of `Attacker`'s on `Defender` at 100% of its attack, melee or not. */
	FCataclysmDamageResult SkullDeathmarkBloodBlow(const FSkullDeathmarkBloodFighter& Attacker,
												   const FSkullDeathmarkBloodFighter& Defender, bool bMelee)
	{
		FCataclysmHitDelivery Delivery;
		Delivery.bIsMelee = bMelee;
		FCataclysmDamageResult Result;
		UCataclysmSkillEffects::ApplyHit(Attacker.Actor, Defender.Actor, 100.0f, FGameplayTagContainer(), Delivery,
										 &Result);
		return Result;
	}

	/** What the generated rows of `Node` at `Points` give `Stat`, judged by the pipeline in `State`. */
	float SkullDeathmarkBloodGenerated(const UDataTable* Nodes, const UDataTable* Effects, const TCHAR* Node, int32 Points,
									   const TCHAR* Stat, const FCataclysmStatConditions& State)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), Points);
		const TMap<FName, TArray<FCataclysmStatModifier>> Granted =
			UCataclysmPassiveTree::ModifiersFor(Allocation, Nodes, Effects, {FName(TEXT("War"))});
		const TArray<FCataclysmStatModifier>* Rows = Granted.Find(FName(Stat));
		return Rows ? UCataclysmStatPipeline::Evaluate(0.0f, *Rows, FGameplayTagContainer(), State).Final : 0.0f;
	}
}

#define CATACLYSM_SKULL_DEATHMARK_BLOOD_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// The rows, as the game loads them
// ---------------------------------------------------------------------------

CATACLYSM_SKULL_DEATHMARK_BLOOD_TEST(FCataclysmSkullDeathmarkBloodRowsTest,
	"Cataclysm.SkullDeathmarkBlood.TheGeneratedRowsGiveEachFigureAtItsPoints")
{
	using namespace CataclysmSkullDeathmarkBloodTest;
	// THE GENERATED ROWS, judged by the pipeline. SKULL SPLITTER in Wrath with a two-handed weapon: 6% critical strike
	// multiplier and 10% armour a stack a point, a cost of 3, 2 seconds, a cap of 4 to 4 points and 6 from 5 (the cap's
	// edge, each side the other's control). ITS CONDITIONS' CONTROLS: out of Wrath the percent and the cost are nought
	// and the multiplier is not; with one hand all three are nought. DEATHMARK: 3% a point, a cost of 10 less 1 a point, 5
	// seconds. BLOOD PRICE: 5% a point of the maximum, 3 seconds.
	const UDataTable* Nodes = UCataclysmPassiveTree::LoadNodeTable();
	const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
	if (!TestNotNull(TEXT("set-up: the node table loads"), Nodes)
		|| !TestNotNull(TEXT("set-up: the effect table loads"), Effects))
	{
		return false;
	}
	const TCHAR* Skull = TEXT("Berserker_twoh_003");
	const TCHAR* Mark = TEXT("Berserker_crit_004");
	const TCHAR* Blood = TEXT("Berserker_trunk_008");
	const int32 SkullRows = UCataclysmPassiveTree::EffectsFor(Effects, FName(Skull)).Num();
	const int32 MarkRows = UCataclysmPassiveTree::EffectsFor(Effects, FName(Mark)).Num();
	const int32 BloodRows = UCataclysmPassiveTree::EffectsFor(Effects, FName(Blood)).Num();
	if (SkullRows != 6 || MarkRows != 4 || BloodRows != 2)
	{
		AddError(FString::Printf(
			TEXT("DT_PassiveEffects holds %d, %d and %d rows for Skull Splitter, Deathmark and Blood Price, not 6, 4 and "
				 "2. If game/Data/PassiveEffects.csv has them, the asset is older than the CSV: run  python "
				 "tools/run_editor_python.py tools/generate_datatable_assets.py"),
			SkullRows, MarkRows, BloodRows));
		return false;
	}

	FCataclysmStatConditions WrathTwoHanded;
	WrathTwoHanded.bInWrath = true;
	WrathTwoHanded.WeaponHands = 2;
	FCataclysmStatConditions CalmTwoHanded = WrathTwoHanded;
	CalmTwoHanded.bInWrath = false;
	FCataclysmStatConditions WrathOneHanded = WrathTwoHanded;
	WrathOneHanded.WeaponHands = 1;
	const auto At = [&](const TCHAR* Node, int32 Points, const TCHAR* Stat, const FCataclysmStatConditions& State)
	{
		return SkullDeathmarkBloodGenerated(Nodes, Effects, Node, Points, Stat, State);
	};
	const TCHAR* Multiplier = TEXT("crit_multiplier");

	TestEqual(TEXT("Skull Splitter at 4 points: a multiplier of 24"), At(Skull, 4, Multiplier, WrathTwoHanded), 24.0f,
			  0.001f);
	TestEqual(TEXT("40% a stack"), At(Skull, 4, FCataclysmWarFervour::CritArmorRemovedInWrathStat, WrathTwoHanded), 40.0f,
			  0.001f);
	TestEqual(TEXT("a cost of 3"), At(Skull, 4, FCataclysmWarFervour::CritArmorFervourCostStat, WrathTwoHanded), 3.0f,
			  0.001f);
	TestEqual(TEXT("2 seconds"), At(Skull, 4, FCataclysmWarFervour::CritArmorSecondsStat, WrathTwoHanded), 2.0f, 0.001f);
	TestEqual(TEXT("and a cap of 4 at 4 points"), At(Skull, 4, FCataclysmWarFervour::CritArmorStackCapStat, WrathTwoHanded),
			  4.0f, 0.001f);
	TestEqual(TEXT("a cap of 6 at 5 points, the other side of the edge"),
			  At(Skull, 5, FCataclysmWarFervour::CritArmorStackCapStat, WrathTwoHanded), 6.0f, 0.001f);
	TestEqual(TEXT("and at 8"), At(Skull, 8, FCataclysmWarFervour::CritArmorStackCapStat, WrathTwoHanded), 6.0f, 0.001f);
	TestEqual(TEXT("a cap of 4 at 1 point"), At(Skull, 1, FCataclysmWarFervour::CritArmorStackCapStat, WrathTwoHanded),
			  4.0f, 0.001f);

	TestEqual(TEXT("control: out of Wrath, no armour a stack"),
			  At(Skull, 5, FCataclysmWarFervour::CritArmorRemovedInWrathStat, CalmTwoHanded), 0.0f, 0.001f);
	TestEqual(TEXT("control: out of Wrath, no cost"),
			  At(Skull, 5, FCataclysmWarFervour::CritArmorFervourCostStat, CalmTwoHanded), 0.0f, 0.001f);
	TestEqual(TEXT("and out of Wrath the multiplier still holds, 30 at 5 points"), At(Skull, 5, Multiplier, CalmTwoHanded),
			  30.0f, 0.001f);
	TestEqual(TEXT("control: one hand, no multiplier"), At(Skull, 5, Multiplier, WrathOneHanded), 0.0f, 0.001f);
	TestEqual(TEXT("control: one hand, no armour a stack"),
			  At(Skull, 5, FCataclysmWarFervour::CritArmorRemovedInWrathStat, WrathOneHanded), 0.0f, 0.001f);
	TestEqual(TEXT("control: one hand, no cost"),
			  At(Skull, 5, FCataclysmWarFervour::CritArmorFervourCostStat, WrathOneHanded), 0.0f, 0.001f);

	FCataclysmStatConditions Anywhere;
	TestEqual(TEXT("Deathmark at 1 point: 3%"), At(Mark, 1, FCataclysmWarFervour::DeathmarkPercentStat, Anywhere), 3.0f,
			  0.001f);
	TestEqual(TEXT("a cost of 9"), At(Mark, 1, FCataclysmWarFervour::DeathmarkFervourCostStat, Anywhere), 9.0f, 0.001f);
	TestEqual(TEXT("5 seconds"), At(Mark, 1, FCataclysmWarFervour::DeathmarkSecondsStat, Anywhere), 5.0f, 0.001f);
	TestEqual(TEXT("at 6 points: 18%"), At(Mark, 6, FCataclysmWarFervour::DeathmarkPercentStat, Anywhere), 18.0f, 0.001f);
	TestEqual(TEXT("a cost of 4"), At(Mark, 6, FCataclysmWarFervour::DeathmarkFervourCostStat, Anywhere), 4.0f, 0.001f);
	TestEqual(TEXT("still 5 seconds"), At(Mark, 6, FCataclysmWarFervour::DeathmarkSecondsStat, Anywhere), 5.0f, 0.001f);

	TestEqual(TEXT("Blood Price at 1 point: 5%"), At(Blood, 1, FCataclysmWarFervour::BloodPricePercentStat, Anywhere), 5.0f,
			  0.001f);
	TestEqual(TEXT("at 8 points: 40%"), At(Blood, 8, FCataclysmWarFervour::BloodPricePercentStat, Anywhere), 40.0f, 0.001f);
	TestEqual(TEXT("3 seconds"), At(Blood, 8, FCataclysmWarFervour::BloodPriceSecondsStat, Anywhere), 3.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// Skull Splitter: a critical strike in Wrath pays 3 and places a stack
// ---------------------------------------------------------------------------

CATACLYSM_SKULL_DEATHMARK_BLOOD_TEST(FCataclysmSkullSplitterTest,
	"Cataclysm.SkullDeathmarkBlood.SkullSplitterPaysThreeForEachStackAndPlacesNoneItCannotPayFor")
{
	using namespace CataclysmSkullDeathmarkBloodTest;
	// A REAL PLAYER holding Skull Splitter's figures at 1 point, its percent and its cost stated under Wrath as the rows
	// state them: 10% a stack, a cost of 3, a cap of 4, 2 seconds. THE CONTROLS: out of Wrath a critical strike places
	// nothing and spends nothing; in Wrath a blow that is not critical does neither. In Wrath a critical strike spends 3
	// and places 10%, shown under the creature's bar; five place four stacks (the cap) and spend 15. QUESTION 1: a pool
	// holding 2 pays nothing and places nothing, on a second creature.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FSkullDeathmarkBloodPlayer Player(World);
	ACataclysmEnemyCharacter* Enemy = SkullDeathmarkBloodEnemy(World, 300.0f);
	ACataclysmEnemyCharacter* Other = SkullDeathmarkBloodEnemy(World, -300.0f);
	UCataclysmAbilitySystemComponent* Its = SkullDeathmarkBloodSystemOf(Enemy);
	UCataclysmAbilitySystemComponent* OthersSystem = SkullDeathmarkBloodSystemOf(Other);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable())
		|| !TestNotNull(TEXT("set-up: a creature with an ability system"), Its)
		|| !TestNotNull(TEXT("set-up: a second creature with an ability system"), OthersSystem)
		|| !TestNotNull(TEXT("set-up: the announcer"), Events))
	{
		return false;
	}
	HoldSkullDeathmarkBloodStats(Player.System, {
		{FCataclysmWarFervour::CritArmorRemovedInWrathStat, 10.0f, ECataclysmStatCondition::InWrath},
		{FCataclysmWarFervour::CritArmorFervourCostStat, 3.0f, ECataclysmStatCondition::InWrath},
		{FCataclysmWarFervour::CritArmorStackCapStat, 4.0f},
		{FCataclysmWarFervour::CritArmorSecondsStat, 2.0f}});
	Player.Pool(100.0f, 50.0f);

	SkullDeathmarkBloodAnnounce(Events, Player, Enemy, /*bCritical=*/true);
	TestEqual(TEXT("control: out of Wrath a critical strike removes no armour"), Its->ArmourRemovedPercentNow(), 0.0f,
			  0.001f);
	TestEqual(TEXT("control: and spends nothing"), Player.Fervour(), 50.0f, 0.001f);

	Player.System->SetBerserkerState(ECataclysmBerserkerState::Wrath);
	SkullDeathmarkBloodAnnounce(Events, Player, Enemy, /*bCritical=*/false);
	TestEqual(TEXT("control: in Wrath a blow that is not critical removes no armour"), Its->ArmourRemovedPercentNow(),
			  0.0f, 0.001f);
	TestEqual(TEXT("control: and spends nothing"), Player.Fervour(), 50.0f, 0.001f);

	SkullDeathmarkBloodAnnounce(Events, Player, Enemy, /*bCritical=*/true);
	TestEqual(TEXT("in Wrath a critical strike removes 10%"), Its->ArmourRemovedPercentNow(), 10.0f, 0.001f);
	TestEqual(TEXT("and spends 3"), Player.Fervour(), 47.0f, 0.001f);
	TestEqual(TEXT("shown under the creature's bar"), UCataclysmCombatOverlay::ArmourRemovedTextFor(Enemy),
			  FString(TEXT("Armor -10%")));
	for (int32 More = 0; More < 4; ++More)
	{
		SkullDeathmarkBloodAnnounce(Events, Player, Enemy, /*bCritical=*/true);
	}
	TestEqual(TEXT("five critical strikes place four stacks, the cap"), Its->ArmourRemovedPercentNow(), 40.0f, 0.001f);
	TestEqual(TEXT("and spend 15"), Player.Fervour(), 35.0f, 0.001f);
	TestEqual(TEXT("shown as 40%"), UCataclysmCombatOverlay::ArmourRemovedTextFor(Enemy), FString(TEXT("Armor -40%")));

	// QUESTION 1: "in exchange for", so a pool that cannot pay 3 gets neither.
	Player.Pool(100.0f, 2.0f);
	SkullDeathmarkBloodAnnounce(Events, Player, Other, /*bCritical=*/true);
	TestEqual(TEXT("a pool holding 2 places nothing"), OthersSystem->ArmourRemovedPercentNow(), 0.0f, 0.001f);
	TestEqual(TEXT("and spends nothing"), Player.Fervour(), 2.0f, 0.001f);

	// AND THE STACKS LAPSE TOGETHER after their 2 seconds.
	World->TimeSeconds += 2.5f;
	TestEqual(TEXT("2.5 seconds later the stacks have lapsed"), Its->ArmourRemovedPercentNow(), 0.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// Deathmark: a critical strike marks; the marked take more from every source
// ---------------------------------------------------------------------------

CATACLYSM_SKULL_DEATHMARK_BLOOD_TEST(FCataclysmDeathmarkTest,
	"Cataclysm.SkullDeathmarkBlood.DeathmarkMarksOnACriticalStrikeAndTheMarkedTakeMoreFromEverySource")
{
	using namespace CataclysmSkullDeathmarkBloodTest;
	// A REAL PLAYER holding Deathmark at 6 points: 18%, a cost of 4, 5 seconds. THE CONTROL: a blow that is not critical
	// marks nothing. A critical strike spends 4 and marks, shown under the bar; the next critical strike refreshes the 5
	// seconds. QUESTION 2: a pool holding less than the cost, and an empty one, mark with no bonus and spend nothing.
	// QUESTION 3, THE DAMAGE STEP: a blow of 100 with no attacker at all (so from no particular source) deals 118 to a
	// creature marked with 18%, and 100 to one marked with nothing (the control).
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	FSkullDeathmarkBloodPlayer Player(World);
	ACataclysmEnemyCharacter* Enemy = SkullDeathmarkBloodEnemy(World, 300.0f);
	ACataclysmEnemyCharacter* Other = SkullDeathmarkBloodEnemy(World, -300.0f);
	UCataclysmAbilitySystemComponent* Its = SkullDeathmarkBloodSystemOf(Enemy);
	UCataclysmAbilitySystemComponent* OthersSystem = SkullDeathmarkBloodSystemOf(Other);
	UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable())
		|| !TestNotNull(TEXT("set-up: a creature with an ability system"), Its)
		|| !TestNotNull(TEXT("set-up: a second creature with an ability system"), OthersSystem)
		|| !TestNotNull(TEXT("set-up: the announcer"), Events))
	{
		return false;
	}
	HoldSkullDeathmarkBloodStats(Player.System, {{FCataclysmWarFervour::DeathmarkPercentStat, 18.0f},
												 {FCataclysmWarFervour::DeathmarkFervourCostStat, 4.0f},
												 {FCataclysmWarFervour::DeathmarkSecondsStat, 5.0f}});
	Player.Pool(100.0f, 50.0f);

	SkullDeathmarkBloodAnnounce(Events, Player, Enemy, /*bCritical=*/false);
	TestFalse(TEXT("control: a blow that is not critical marks nothing"), Its->IsDeathmarked());
	TestEqual(TEXT("control: nothing is shown"), UCataclysmCombatOverlay::DeathmarkTextFor(Enemy), FString());
	TestEqual(TEXT("control: and nothing is spent"), Player.Fervour(), 50.0f, 0.001f);

	SkullDeathmarkBloodAnnounce(Events, Player, Enemy, /*bCritical=*/true);
	TestEqual(TEXT("a critical strike marks with 18%"), Its->DeathmarkPercentNow(), 18.0f, 0.001f);
	TestEqual(TEXT("shown under the creature's bar"), UCataclysmCombatOverlay::DeathmarkTextFor(Enemy),
			  FString(TEXT("Deathmarked +18%")));
	TestEqual(TEXT("and spends 4"), Player.Fervour(), 46.0f, 0.001f);

	// REFRESHED: a second critical strike 3 seconds on holds the mark to 8 seconds, past the first one's 5.
	World->TimeSeconds += 3.0f;
	SkullDeathmarkBloodAnnounce(Events, Player, Enemy, /*bCritical=*/true);
	TestEqual(TEXT("the second spends 4 more"), Player.Fervour(), 42.0f, 0.001f);
	World->TimeSeconds += 3.0f;
	TestTrue(TEXT("6 seconds after the first, the refreshed mark lasts"), Its->IsDeathmarked());
	World->TimeSeconds += 3.0f;
	TestFalse(TEXT("and 9 seconds after, it has lapsed"), Its->IsDeathmarked());
	TestEqual(TEXT("and is no longer shown"), UCataclysmCombatOverlay::DeathmarkTextFor(Enemy), FString());

	// QUESTION 2: less than the cost is treated as none.
	Player.Pool(100.0f, 3.0f);
	SkullDeathmarkBloodAnnounce(Events, Player, Other, /*bCritical=*/true);
	TestTrue(TEXT("a pool holding 3 of a cost of 4 still marks"), OthersSystem->IsDeathmarked());
	TestEqual(TEXT("with no bonus"), OthersSystem->DeathmarkPercentNow(), 0.0f, 0.001f);
	TestEqual(TEXT("shown with no figure"), UCataclysmCombatOverlay::DeathmarkTextFor(Other), FString(TEXT("Deathmarked")));
	TestEqual(TEXT("and spends nothing"), Player.Fervour(), 3.0f, 0.001f);
	Player.Pool(100.0f, 0.0f);
	SkullDeathmarkBloodAnnounce(Events, Player, Enemy, /*bCritical=*/true);
	TestTrue(TEXT("an empty pool still marks"), Its->IsDeathmarked());
	TestEqual(TEXT("with no bonus"), Its->DeathmarkPercentNow(), 0.0f, 0.001f);

	// QUESTION 3: the damage step reads the mark on the creature, whoever strikes it.
	const FSkullDeathmarkBloodFighter Marked(World, 0.0f);
	const FSkullDeathmarkBloodFighter Unbonused(World, 0.0f);
	Marked.AbilitySystem->MarkWithDeathmark(18.0f, 5.0f);
	Unbonused.AbilitySystem->MarkWithDeathmark(0.0f, 5.0f);
	FCataclysmIncomingHit Hit;
	Hit.Damage = 100.0f;
	const float Never = 100.0f;
	const FCataclysmDamageResult OnMarked =
		UCataclysmDamageCalculation::Resolve(Hit, Marked.AbilitySystem, /*Tier=*/1, Never, Never, Never);
	const FCataclysmDamageResult OnUnbonused =
		UCataclysmDamageCalculation::Resolve(Hit, Unbonused.AbilitySystem, /*Tier=*/1, Never, Never, Never);
	TestEqual(TEXT("control: a mark carrying nothing leaves a blow of 100 at 100"), OnUnbonused.DealtToHealth, 100.0f,
			  0.001f);
	TestEqual(TEXT("a mark of 18% takes it to 118"), OnMarked.DealtToHealth, 118.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// Blood Price: the pool empties; the next melee blow that lands carries it
// ---------------------------------------------------------------------------

CATACLYSM_SKULL_DEATHMARK_BLOOD_TEST(FCataclysmBloodPriceTest,
	"Cataclysm.SkullDeathmarkBlood.BloodPriceArmsAsThePoolEmptiesAndTheFirstMeleeBlowThatLandsSpendsIt")
{
	using namespace CataclysmSkullDeathmarkBloodTest;
	// THE ARMING, ON A REAL PLAYER whose pool the out-of-combat decay empties (QUESTION 4: "for any reason" includes the
	// decay). THE CONTROL: without the percent nothing is held. With 40% (8 points) of a maximum of 100, 40 is held for
	// 3 seconds and shown on the effects line. THE BLOW, between two bare characters: a ranged blow neither carries nor
	// spends it (the control); the first melee blow (QUESTION 5) carries 40 more and spends it, so the next carries
	// nothing; and one held past its 3 seconds is gone.
	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkullDeathmarkBloodPinned NeverCritical(TEXT("Cataclysm.CritRoll"), 100.0f);
	const FSkullDeathmarkBloodPinned NeverBlocks(TEXT("Cataclysm.BlockRoll"), 100.0f);

	FSkullDeathmarkBloodPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const auto ShowsBloodPrice = [&Player](const TCHAR* Name, float& OutSecondsLeft)
	{
		for (const FCataclysmPlayerEffect& Entry : UCataclysmPlayerEffects::EntriesFor(Player.Character))
		{
			if (Entry.Name == Name)
			{
				OutSecondsLeft = Entry.SecondsLeft;
				return true;
			}
		}
		return false;
	};

	HoldSkullDeathmarkBloodStats(Player.System, {{FCataclysmWarFervour::BloodPriceSecondsStat, 3.0f},
												 {FCataclysmWarFervour::OutOfCombatDecayPerSecondStat, 50.0f}});
	Player.Pool(100.0f, 30.0f);
	World->TimeSeconds += 10.0f;
	FCataclysmWarFervour::OutOfCombatDecayStep(Player.Character, 1.0f);
	TestEqual(TEXT("set-up: the decay emptied the pool"), Player.Fervour(), 0.0f, 0.001f);
	TestEqual(TEXT("control: without the percent nothing is held"), Player.System->BloodPriceHeldNow(), 0.0f, 0.001f);

	HoldSkullDeathmarkBloodStats(Player.System, {{FCataclysmWarFervour::BloodPricePercentStat, 40.0f},
												 {FCataclysmWarFervour::BloodPriceSecondsStat, 3.0f},
												 {FCataclysmWarFervour::OutOfCombatDecayPerSecondStat, 50.0f}});
	Player.Pool(100.0f, 30.0f);
	FCataclysmWarFervour::OutOfCombatDecayStep(Player.Character, 1.0f);
	TestEqual(TEXT("set-up: the decay emptied the pool again"), Player.Fervour(), 0.0f, 0.001f);
	TestEqual(TEXT("with 40% of 100, 40 is held"), Player.System->BloodPriceHeldNow(), 40.0f, 0.001f);
	float SecondsLeft = -1.0f;
	if (TestTrue(TEXT("and shown on the effects line"), ShowsBloodPrice(TEXT("Blood Price +40 damage"), SecondsLeft)))
	{
		TestEqual(TEXT("with its 3 seconds"), SecondsLeft, 3.0f, 0.001f);
	}

	// THE BLOW.
	const FSkullDeathmarkBloodFighter Attacker(World, 100.0f);
	const FSkullDeathmarkBloodFighter Defender(World, 0.0f);
	const float Plain = SkullDeathmarkBloodBlow(Attacker, Defender, /*bMelee=*/true).DealtToHealth;
	if (!TestTrue(TEXT("set-up: a plain melee blow deals damage"), Plain > 0.0f))
	{
		return false;
	}
	Attacker.AbilitySystem->HoldBloodPrice(40.0f, 3.0f);
	TestEqual(TEXT("control: a ranged blow carries none of it"),
			  SkullDeathmarkBloodBlow(Attacker, Defender, /*bMelee=*/false).DealtToHealth, Plain, 0.001f);
	TestEqual(TEXT("control: and does not spend it"), Attacker.AbilitySystem->BloodPriceHeldNow(), 40.0f, 0.001f);
	TestEqual(TEXT("the first melee blow carries 40 more"),
			  SkullDeathmarkBloodBlow(Attacker, Defender, /*bMelee=*/true).DealtToHealth, Plain + 40.0f, 0.001f);
	TestEqual(TEXT("and spends it"), Attacker.AbilitySystem->BloodPriceHeldNow(), 0.0f, 0.001f);
	TestEqual(TEXT("so the next carries nothing"),
			  SkullDeathmarkBloodBlow(Attacker, Defender, /*bMelee=*/true).DealtToHealth, Plain, 0.001f);

	Attacker.AbilitySystem->HoldBloodPrice(40.0f, 3.0f);
	World->TimeSeconds += 3.5f;
	TestEqual(TEXT("held past its 3 seconds, a melee blow carries nothing"),
			  SkullDeathmarkBloodBlow(Attacker, Defender, /*bMelee=*/true).DealtToHealth, Plain, 0.001f);
	return true;
}

#undef CATACLYSM_SKULL_DEATHMARK_BLOOD_TEST

#endif // WITH_AUTOMATION_TESTS
