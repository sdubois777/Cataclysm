// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmSiphon.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTaunt.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"

/**
 * The Death finish slice A (2026-11-26): the Soul Collector's Fervour spender limb and the Effigy.
 *
 *   - Soul Fuel (`SoulCollector_basic_spine_009`), issue #2937: a skill with a cooldown, not a movement skill and not an
 *     Ultimate, spends up to its figure of Fervour when used (`UCataclysmSkillTemplate::SpendSoulFuel`) and that use
 *     deals more for each Fervour spent (`SoulFuelIncreasePercent`, through `LastNextUseIncreasePercent`). It skips a
 *     skill whose own row holds or spends Fervour: Cold Blood and Requiem, read from their generated rows.
 *   - "Skills that spend Fervour": a use that spent any carries `Use.SpentFervour` on its tags
 *     (`MarkTheUseAsSpendingFervour`), which Soul Shatter's and Final Rites' real rows are scoped to, and Pyre widens it
 *     for every five it spent (`AreaOfEffectMultiplier`).
 *   - Spirit Volley, issue #2908: each Fervour a kill pays looses a spirit at the nearest enemy on its way to the
 *     character (`UCataclysmFervour::FlySpirits`), and a kill a spirit makes looses more, through the real kill handler.
 *   - The Effigy: its real row places a decoy that taunts, reserves its Fervour and widens its placer's Siphon round it
 *     (`FCataclysmSiphon::Step`).
 *
 * EXPECTED FIGURES ARE READ FROM THE DATA where the data states them (the owner, issue #2928): the rows' values from
 * `DT_PassiveEffects` and the Effigy's from `DT_WeaponSkills`. Both need the editor step after the CSVs change; a set-up
 * line says so when a row is missing.
 *
 * WRITTEN WITHOUT A BUILD. Every figure is reasoned from the code; the window that runs them is the coordinating
 * session's.
 */
namespace CataclysmDeathFinishATest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	/** Centimetres in a metre. */
	constexpr float FinishAMetre = 100.0f;

	/** How far a float figure may fall from one it was built to equal. */
	constexpr float FinishAAllowance = 0.01f;

	/** A bare fighter with health, mana, a Fervour pool of 150 and a weapon of 100, as `FDeathSkillFighter` is built. */
	struct FFinishAFighter
	{
		FFinishAFighter(UWorld* World, const FVector& Where)
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
			Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 0.0f);
			Set(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		}

		~FFinishAFighter()
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

		float Fervour() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** A real creature on a side, with a Fervour pool of 150, at full health of 100,000 unless told otherwise. */
	struct FFinishACreature
	{
		FFinishACreature(UWorld* World, const FVector& Where, ECataclysmTeam Team, int32 RarityStep = 0)
		{
			Actor = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
			check(Actor);
			Actor->SetGenericTeamId(UCataclysmTeams::IdFor(Team));
			if (RarityStep > 0)
			{
				Actor->SetRarityStep(RarityStep);
			}
			Actor->SetHealth(100000.0f);
			Actor->SetArmour(0.0f);
			AbilitySystem = Cast<UCataclysmAbilitySystemComponent>(Actor->GetAbilitySystemComponent());
			check(AbilitySystem);
			if (!AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
			{
				AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			}
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												   150.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(),
												   150.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 0.0f);
			AbilitySystem->SetNumericAttributeBase(UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(),
												   0.0f);
		}

		~FFinishACreature()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		float Health() const
		{
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		}

		ACataclysmEnemyCharacter* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	/** One flat row of a stat line. */
	FCataclysmStatModifier FinishAFlat(float Value)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = ECataclysmStatBucket::Flat;
		Row.Source = ECataclysmModifierSource::PassiveKeystone;
		Row.Value = Value;
		return Row;
	}

	/** Give `System` exactly these flat stats, and nothing else. */
	void FinishAWear(UCataclysmAbilitySystemComponent* System, const TMap<FName, float>& Flats)
	{
		TMap<FName, FCataclysmStatInputs> Lines;
		Lines.Add(FName(TEXT("attack_damage"))).Base = 100.0f;
		Lines.Add(FName(TEXT("spell_damage"))).Base = 100.0f;
		for (const TPair<FName, float>& Each : Flats)
		{
			Lines.FindOrAdd(Each.Key).Modifiers.Add(FinishAFlat(Each.Value));
		}
		System->SetStatInputs(MoveTemp(Lines));
	}

	/** The real rows of `Node` (option `Option`) on `Stat`, from the generated passive effects table. */
	TArray<const FCataclysmPassiveEffectRow*> FinishARows(const TCHAR* Node, const TCHAR* Stat, int32 Option = 0)
	{
		TArray<const FCataclysmPassiveEffectRow*> Found;
		for (const FCataclysmPassiveEffectRow* Row :
			 UCataclysmPassiveTree::EffectsFor(UCataclysmPassiveTree::LoadEffectTable(), FName(Node)))
		{
			if (Row && Row->Option == Option && Row->Stat == Stat)
			{
				Found.Add(Row);
			}
		}
		return Found;
	}

	/** What the real rows of `Node` on `Stat` grant at `Points`: a row per point, or once from its Min Points. */
	float FinishAGrantedAt(const TCHAR* Node, const TCHAR* Stat, int32 Points, int32 Option = 0)
	{
		float Sum = 0.0f;
		for (const FCataclysmPassiveEffectRow* Row : FinishARows(Node, Stat, Option))
		{
			Sum += Row->ValuePerPoint * static_cast<float>(UCataclysmPassiveTree::PointsPaid(*Row, Points));
		}
		return Sum;
	}

	/** Grant a template into a slot at level 100, its cell parsed as the game parses a row's. */
	template <typename T>
	T* FinishAGrant(UCataclysmAbilitySystemComponent* System, AActor* Owner, ECataclysmAbilitySlot Slot,
					const TCHAR* ParamText)
	{
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(T::StaticClass(), Slot, /*Level=*/100, Owner);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? System->FindAbilitySpecFromHandle(Handle) : nullptr;
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			Instance->SkillName = TEXT("A test skill");
			Instance->Params = UCataclysmSkillShapes::ParseParams(ParamText);
		}
		return Instance;
	}

	/** One generated Death row, by weapon and designed slot, or an empty one. */
	FCataclysmWeaponSkill FinishADeathRow(const TCHAR* Weapon, ECataclysmAbilitySlot Slot)
	{
		for (const FCataclysmWeaponSkill& Skill :
			 UCataclysmWeaponSkills::SkillsFor(UCataclysmWeaponSkills::LoadGeneratedTable(), Weapon, TEXT("Death")))
		{
			if (UCataclysmWeaponSkills::DesignedSlotOf(Skill) == Slot)
			{
				return Skill;
			}
		}
		return FCataclysmWeaponSkill();
	}

	/** A generated row granted in its designed slot at level 100, stamped as a weapon's grant stamps it. */
	template <typename T>
	T* FinishAGrantRow(UCataclysmAbilitySystemComponent* System, AActor* Owner, const FCataclysmWeaponSkill& Row)
	{
		const FGameplayAbilitySpecHandle Handle = System->GiveAbilityInSlot(
			T::StaticClass(), UCataclysmWeaponSkills::DesignedSlotOf(Row), /*Level=*/100, Owner);
		FGameplayAbilitySpec* Spec = Handle.IsValid() ? System->FindAbilitySpecFromHandle(Handle) : nullptr;
		T* Instance = Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
		if (Instance)
		{
			UCataclysmWeaponSkills::StampOnto(*Instance, Row);
		}
		return Instance;
	}

	/** Activate a granted ability and report whether it started. */
	bool FinishAPress(UAbilitySystemComponent* System, UGameplayAbility* Ability)
	{
		return System && Ability
			&& System->TryActivateAbility(Ability->GetCurrentAbilitySpecHandle(), /*bAllowRemoteActivation=*/false);
	}

	/** Whether a use of `Skill` carries the mark of a use that spent Fervour. */
	bool FinishAMarked(const UCataclysmSkillTemplate* Skill)
	{
		const FGameplayTag Mark = UCataclysmSkillTemplate::SpentFervourTag();
		return Skill && Mark.IsValid() && Skill->SkillTags.HasTagExact(Mark);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishASoulFuelSpendsTest,
	"Cataclysm.DeathFinishSoulCollector.SoulFuelSpendsOnACooldownSkillAndItsDamageRidesThatUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Soul Fuel at its 12 points, its figures read from its real rows (10 spent, and 1% for each 4 points, so 3% for each
 * Fervour): a strike on the Heavy key (a cooldown of 1.5) pressed holding 25 spends 10, leaves 15, and its use carries
 * 30% increased damage for its 10 and the mark of a use that spent Fervour. THE CONTROLS: a strike on the Special key
 * pressed holding 4 spends the 4 it holds and carries 12%; a strike on the movement key spends nothing and is not
 * marked; an Ultimate costing 50 is not reached by Soul Fuel (ruling 1), spends its 50 and is marked for it.
 */
bool FCataclysmDeathFinishASoulFuelSpendsTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const TCHAR* const SoulFuel = TEXT("SoulCollector_basic_spine_009");
	const float Spend = FinishAGrantedAt(SoulFuel, UCataclysmSkillTemplate::SkillFervourSpendStat, 12);
	const float PerFervour = FinishAGrantedAt(SoulFuel, UCataclysmSkillTemplate::DamagePerSoulFuelFervourStat, 12);
	if (!TestEqual(TEXT("set-up: Soul Fuel's real row spends 10. If 0, DT_PassiveEffects is older than the CSV"), Spend,
				   10.0f, FinishAAllowance)
		|| !TestEqual(TEXT("set-up: and its real rows give 3% for each Fervour at 12 points"), PerFervour, 3.0f,
					  FinishAAllowance)
		|| !TestTrue(TEXT("set-up: the tag table knows Use.SpentFervour"), UCataclysmSkillTemplate::SpentFervourTag().IsValid()))
	{
		return false;
	}

	// WHAT ONE PRESS LEFT BEHIND, read while its fighter still stands.
	struct FPressed
	{
		bool bPressed = false;
		float SoulFuel = -1.0f;
		float Left = -1.0f;
		float Increase = -1.0f;
		float SpentInAll = -1.0f;
		bool bMarked = false;
	};
	const auto Press = [World, Spend, PerFervour](ECataclysmAbilitySlot Slot, float Held, float FervourCost,
												  float Across) -> FPressed
	{
		FPressed Out;
		FFinishAFighter Fighter(World, FVector(0.0f, Across * FinishAMetre, 0.0f));
		FinishAWear(Fighter.AbilitySystem, {{FName(UCataclysmSkillTemplate::SkillFervourSpendStat), Spend},
											{FName(UCataclysmSkillTemplate::DamagePerSoulFuelFervourStat), PerFervour}});
		Fighter.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Held);
		UCataclysmStrikeSkill* Skill =
			FinishAGrant<UCataclysmStrikeSkill>(Fighter.AbilitySystem, Fighter.Actor, Slot, TEXT("Radius=2; Angle=360"));
		if (!Skill)
		{
			return Out;
		}
		if (FervourCost >= 0.0f)
		{
			Skill->FervourCostOverride = FervourCost;
		}
		Out.bPressed = FinishAPress(Fighter.AbilitySystem, Skill);
		Out.SoulFuel = Skill->SoulFuelSpentThisUse;
		Out.Left = Fighter.Fervour();
		Out.Increase = Skill->LastNextUseIncreasePercent;
		Out.SpentInAll = Skill->FervourSpentByThisUse;
		Out.bMarked = FinishAMarked(Skill);
		return Out;
	};

	const FPressed Heavy = Press(ECataclysmAbilitySlot::Heavy, 25.0f, -1.0f, 0.0f);
	if (!TestTrue(TEXT("set-up: a heavy strike holding 25 was pressed"), Heavy.bPressed))
	{
		return false;
	}
	TestEqual(TEXT("Soul Fuel spent its 10"), Heavy.SoulFuel, Spend, FinishAAllowance);
	TestEqual(TEXT("and 15 are left"), Heavy.Left, 25.0f - Spend, FinishAAllowance);
	TestEqual(TEXT("the use carries 3% increased damage for each of the 10, 30%"), Heavy.Increase, PerFervour * Spend,
			  FinishAAllowance);
	TestEqual(TEXT("the use spent 10 in all"), Heavy.SpentInAll, Spend, FinishAAllowance);
	TestTrue(TEXT("and is marked as a use that spent Fervour"), Heavy.bMarked);

	const FPressed Short = Press(ECataclysmAbilitySlot::Special, 4.0f, -1.0f, 50.0f);
	if (TestTrue(TEXT("set-up: a special strike holding 4 was pressed"), Short.bPressed))
	{
		TestEqual(TEXT("control: holding 4, it spends the 4 it holds"), Short.SoulFuel, 4.0f, FinishAAllowance);
		TestEqual(TEXT("and none is left"), Short.Left, 0.0f, FinishAAllowance);
		TestEqual(TEXT("and carries 12%"), Short.Increase, PerFervour * 4.0f, FinishAAllowance);
	}

	const FPressed Step = Press(ECataclysmAbilitySlot::Movement, 25.0f, -1.0f, 100.0f);
	if (TestTrue(TEXT("set-up: a strike on the movement key holding 25 was pressed"), Step.bPressed))
	{
		TestEqual(TEXT("control: a movement skill spends nothing"), Step.SoulFuel, 0.0f, FinishAAllowance);
		TestEqual(TEXT("and keeps its 25"), Step.Left, 25.0f, FinishAAllowance);
		TestEqual(TEXT("and carries no increase"), Step.Increase, 0.0f, FinishAAllowance);
		TestFalse(TEXT("and is not marked"), Step.bMarked);
	}

	const FPressed Ultimate = Press(ECataclysmAbilitySlot::Ultimate, 60.0f, 50.0f, 150.0f);
	if (TestTrue(TEXT("set-up: an Ultimate costing 50 holding 60 was pressed"), Ultimate.bPressed))
	{
		TestEqual(TEXT("control: Soul Fuel adds nothing to an Ultimate"), Ultimate.SoulFuel, 0.0f, FinishAAllowance);
		TestEqual(TEXT("which spends its 50 and keeps 10"), Ultimate.Left, 10.0f, FinishAAllowance);
		TestEqual(TEXT("so the use spent 50 in all"), Ultimate.SpentInAll, 50.0f, FinishAAllowance);
		TestTrue(TEXT("and is marked for its cost"), Ultimate.bMarked);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishASoulFuelSkipsTest,
	"Cataclysm.DeathFinishSoulCollector.SoulFuelSkipsColdBloodAndRequiemReadFromTheirGeneratedRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #2937, the coordinating session's ruling 2 of 2026-11-25: Soul Fuel skips any skill whose own row already holds
 * or spends Fervour. Read from the generated rows: the Sword's Cold Blood (`HoldsFervour=1`) and the Wand's Requiem
 * (`SpendsAllFervour=1`) are not reached, and with Soul Fuel worn and 50 held neither spends any. THE CONTROLS: the
 * Crossbow's Carrion Crows, a buff with a cooldown stating neither key, is reached and spends 10; and stand-ins of one
 * shape each way, a support buff and a heavy strike, are reached without the key and not with it.
 */
bool FCataclysmDeathFinishASoulFuelSkipsTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World)
		|| !TestNotNull(TEXT("set-up: the weapon skill table loads"), UCataclysmWeaponSkills::LoadGeneratedTable()))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FFinishAFighter Fighter(World, FVector::ZeroVector);
	FinishAWear(Fighter.AbilitySystem, {{FName(UCataclysmSkillTemplate::SkillFervourSpendStat), 10.0f}});
	Fighter.Set(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 50.0f);

	const FCataclysmWeaponSkill ColdBloodRow = FinishADeathRow(TEXT("Sword"), ECataclysmAbilitySlot::Support);
	const FCataclysmWeaponSkill RequiemRow = FinishADeathRow(TEXT("Wand"), ECataclysmAbilitySlot::Ultimate);
	const FCataclysmWeaponSkill CrowsRow = FinishADeathRow(TEXT("Crossbow"), ECataclysmAbilitySlot::Support);
	if (!TestEqual(TEXT("set-up: the Sword's Support is Cold Blood"), ColdBloodRow.Name, FString(TEXT("Cold Blood")))
		|| !TestEqual(TEXT("set-up: the Wand's Ultimate is Requiem"), RequiemRow.Name, FString(TEXT("Requiem")))
		|| !TestEqual(TEXT("set-up: the Crossbow's Support is Carrion Crows"), CrowsRow.Name,
					  FString(TEXT("Carrion Crows"))))
	{
		return false;
	}
	UCataclysmSelfBuffSkill* ColdBlood =
		FinishAGrantRow<UCataclysmSelfBuffSkill>(Fighter.AbilitySystem, Fighter.Actor, ColdBloodRow);
	UCataclysmStrikeSkill* Requiem = FinishAGrantRow<UCataclysmStrikeSkill>(Fighter.AbilitySystem, Fighter.Actor,
																			 RequiemRow);
	UCataclysmSelfBuffSkill* Crows =
		FinishAGrantRow<UCataclysmSelfBuffSkill>(Fighter.AbilitySystem, Fighter.Actor, CrowsRow);
	if (!TestNotNull(TEXT("set-up: Cold Blood granted"), ColdBlood) || !TestNotNull(TEXT("set-up: Requiem granted"), Requiem)
		|| !TestNotNull(TEXT("set-up: Carrion Crows granted"), Crows)
		|| !TestTrue(TEXT("set-up: Cold Blood's row holds Fervour"), ColdBlood->Params.bHoldsFervour)
		|| !TestTrue(TEXT("set-up: Requiem's row spends all of it"), Requiem->Params.bSpendsAllFervour))
	{
		return false;
	}

	TestFalse(TEXT("Cold Blood, holding Fervour by its own row, is not reached"), ColdBlood->SoulFuelReaches());
	TestEqual(TEXT("and spends nothing"), ColdBlood->SpendSoulFuel(Fighter.AbilitySystem), 0.0f);
	TestFalse(TEXT("Requiem, spending every Fervour by its own row, is not reached"), Requiem->SoulFuelReaches());
	TestEqual(TEXT("and spends nothing"), Requiem->SpendSoulFuel(Fighter.AbilitySystem), 0.0f);
	TestEqual(TEXT("so the 50 held are all still held"), Fighter.Fervour(), 50.0f, FinishAAllowance);
	TestTrue(TEXT("control: Carrion Crows, a buff with a cooldown stating neither, is reached"), Crows->SoulFuelReaches());
	TestEqual(TEXT("and spends 10"), Crows->SpendSoulFuel(Fighter.AbilitySystem), 10.0f, FinishAAllowance);

	// THE RULE IS THE KEYS AND NOT THE NAMES: a stand-in of each shape is reached without its key and not with it.
	UCataclysmSelfBuffSkill* PlainBuff = FinishAGrant<UCataclysmSelfBuffSkill>(
		Fighter.AbilitySystem, Fighter.Actor, ECataclysmAbilitySlot::Support, TEXT("Duration=6"));
	UCataclysmSelfBuffSkill* HoldingBuff = FinishAGrant<UCataclysmSelfBuffSkill>(
		Fighter.AbilitySystem, Fighter.Actor, ECataclysmAbilitySlot::Support, TEXT("Duration=6; HoldsFervour=1"));
	UCataclysmStrikeSkill* PlainStrike = FinishAGrant<UCataclysmStrikeSkill>(
		Fighter.AbilitySystem, Fighter.Actor, ECataclysmAbilitySlot::Heavy, TEXT("Radius=2; Angle=360"));
	UCataclysmStrikeSkill* SpendingStrike = FinishAGrant<UCataclysmStrikeSkill>(
		Fighter.AbilitySystem, Fighter.Actor, ECataclysmAbilitySlot::Heavy,
		TEXT("Radius=2; Angle=360; SpendsAllFervour=1"));
	if (!PlainBuff || !HoldingBuff || !PlainStrike || !SpendingStrike)
	{
		AddError(TEXT("set-up: four stand-ins"));
		return false;
	}
	TestTrue(TEXT("control: a support buff stating no key is reached"), PlainBuff->SoulFuelReaches());
	TestFalse(TEXT("the same buff stating HoldsFervour=1 is not"), HoldingBuff->SoulFuelReaches());
	TestTrue(TEXT("control: a heavy strike stating no key is reached"), PlainStrike->SoulFuelReaches());
	TestFalse(TEXT("the same strike stating SpendsAllFervour=1 is not"), SpendingStrike->SoulFuelReaches());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishARealRowsTest,
	"Cataclysm.DeathFinishSoulCollector.TheSixNodesRowsAreTheirSentences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The real rows, read from the generated passive effects table: Soul Fuel spends 10 from its first point and gives 1%
 * at each of 4, 8 and 12 points; Deeper Draw 1 more a point, so its tenth point makes the 20 its sentence names; Pyre 1
 * a point; Soul Shatter 4% increased critical strike chance a point and Final Rites 3% increased damage a point against
 * rung 1 and up, both scoped to `Use.SpentFervour`; Spirit Volley 30, option 2 of the vow at 50 points.
 */
bool FCataclysmDeathFinishARealRowsTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	if (!TestNotNull(TEXT("set-up: the passive effects table"), UCataclysmPassiveTree::LoadEffectTable()))
	{
		return false;
	}
	const TCHAR* const SoulFuel = TEXT("SoulCollector_basic_spine_009");
	TestEqual(TEXT("Soul Fuel at 1 point spends 10"),
			  FinishAGrantedAt(SoulFuel, UCataclysmSkillTemplate::SkillFervourSpendStat, 1), 10.0f, FinishAAllowance);
	TestEqual(TEXT("and at 12, still 10"),
			  FinishAGrantedAt(SoulFuel, UCataclysmSkillTemplate::SkillFervourSpendStat, 12), 10.0f, FinishAAllowance);
	TestEqual(TEXT("its damage at 3 points: none"),
			  FinishAGrantedAt(SoulFuel, UCataclysmSkillTemplate::DamagePerSoulFuelFervourStat, 3), 0.0f);
	TestEqual(TEXT("at 4: 1%"),
			  FinishAGrantedAt(SoulFuel, UCataclysmSkillTemplate::DamagePerSoulFuelFervourStat, 4), 1.0f, FinishAAllowance);
	TestEqual(TEXT("at 12: 3%"),
			  FinishAGrantedAt(SoulFuel, UCataclysmSkillTemplate::DamagePerSoulFuelFervourStat, 12), 3.0f,
			  FinishAAllowance);
	TestEqual(TEXT("Deeper Draw at 10 points: 10 more, so 20 a skill with Soul Fuel's"),
			  FinishAGrantedAt(TEXT("SoulCollector_basic_d_stem1"), UCataclysmSkillTemplate::SkillFervourSpendStat, 10)
			  + FinishAGrantedAt(SoulFuel, UCataclysmSkillTemplate::SkillFervourSpendStat, 12),
			  20.0f, FinishAAllowance);
	TestEqual(TEXT("Pyre: 1 a point"),
			  FinishAGrantedAt(TEXT("SoulCollector_basic_d_a0"), UCataclysmSkillTemplate::AreaPerFiveFervourSpentStat, 1),
			  1.0f, FinishAAllowance);

	const TArray<const FCataclysmPassiveEffectRow*> Shatter = FinishARows(TEXT("SoulCollector_basic_d_a1"), TEXT("crit_chance"));
	if (TestEqual(TEXT("Soul Shatter: one row"), Shatter.Num(), 1))
	{
		TestEqual(TEXT("increased"), Shatter[0]->ValueKind, FString(TEXT("increased")));
		TestEqual(TEXT("4 a point"), Shatter[0]->ValuePerPoint, 4.0f, FinishAAllowance);
		TestEqual(TEXT("for a use that spent Fervour"), Shatter[0]->RequiredTags, FString(TEXT("Use.SpentFervour")));
	}
	for (const TCHAR* Stat : {TEXT("attack_damage"), TEXT("spell_damage")})
	{
		const TArray<const FCataclysmPassiveEffectRow*> Rites = FinishARows(TEXT("SoulCollector_basic_d_a2"), Stat);
		if (TestEqual(*FString::Printf(TEXT("Final Rites: one %s row"), Stat), Rites.Num(), 1))
		{
			TestEqual(TEXT("3 a point"), Rites[0]->ValuePerPoint, 3.0f, FinishAAllowance);
			TestEqual(TEXT("for a use that spent Fervour"), Rites[0]->RequiredTags, FString(TEXT("Use.SpentFervour")));
			TestEqual(TEXT("against rung 1 and up"), Rites[0]->Condition, FString(TEXT("target_rarity_at_least")));
			TestEqual(TEXT("which is Elite"), Rites[0]->ConditionValue, 1.0f, FinishAAllowance);
		}
	}
	TestEqual(TEXT("Spirit Volley: 30, option 2 of the vow at 50"),
			  FinishAGrantedAt(TEXT("SoulCollector_capstone_50"), UCataclysmFervour::SpiritVolleyWeaponPercentStat, 1,
							   /*Option=*/2),
			  30.0f, FinishAAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishAMarkedUseTest,
	"Cataclysm.DeathFinishSoulCollector.SoulShatterAndFinalRitesReachOnlyAUseThatSpentFervourThroughARealCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A real level 30 Soul Collector holding Soul Shatter at 8 points and Final Rites at 6, through the real passive rows
 * and builder. With a skill's tags carrying `Use.SpentFervour`, the critical strike chance's increases are 32 points more
 * (8 x 4, read from the row) than without; and against an Elite its attack damage stands to the unmarked as one plus the
 * other increases plus 18% (6 x 3) stands to one plus the other increases. THE CONTROLS: against a Common creature
 * marked and unmarked are the same; and the same character with only the starting node gives a marked use nothing.
 */
bool FCataclysmDeathFinishAMarkedUseTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* SpawnedState = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!SpawnedState || !Controller || !Character)
	{
		AddError(TEXT("A possessed player could not be built."));
		return false;
	}
	// DEATH CHOSEN BEFORE POSSESSION, as `Cataclysm.CarriedDamageTypes.` builds its character: the starting weapon is
	// then given carrying Death, so the Death trees are reached AND a weapon is worn. `attack_damage` comes wholly from
	// what is worn, and window dfb-w1 failed this test's set-up with the starting weapon taken off ("No weapon is worn").
	SpawnedState->SetCreationChoice(FName(TEXT("Dagger")), FName(TEXT("Death")));
	Controller->SetPlayerState(SpawnedState);
	Controller->Possess(Character);
	ACataclysmPlayerState* State = Character->GetPlayerState<ACataclysmPlayerState>();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	UCataclysmAbilitySystemComponent* System = State ? State->GetCataclysmAbilitySystemComponent() : nullptr;
	const FGameplayTag Mark = UCataclysmSkillTemplate::SpentFervourTag();
	if (!TestNotNull(TEXT("set-up: the player state"), State) || !TestNotNull(TEXT("set-up: equipment"), Equipment)
		|| !TestNotNull(TEXT("set-up: the ability system"), System)
		|| !TestTrue(TEXT("set-up: the tag table knows Use.SpentFervour"), Mark.IsValid()))
	{
		return false;
	}
	State->SetLevelAndExperience(30, 0);

	FGameplayTagContainer Unmarked;
	Unmarked.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Melee")), /*ErrorIfNotFound=*/false));
	FGameplayTagContainer Marked = Unmarked;
	Marked.AddTag(Mark);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("SoulCollector_basic_spine_000")), 1);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(System);
	const float CritAlone = System->IncreasesForStat(FName(TEXT("crit_chance")), Marked)
		- System->IncreasesForStat(FName(TEXT("crit_chance")), Unmarked);
	TestEqual(TEXT("control: the starting node alone gives a marked use no critical strike chance"), CritAlone, 0.0f,
			  0.0001f);

	Allocation.Add(FName(TEXT("SoulCollector_basic_d_a1")), 8);
	Allocation.Add(FName(TEXT("SoulCollector_basic_d_a2")), 6);
	State->SetPassiveAllocation(Allocation, TArray<FName>());
	Equipment->RefreshAttributes(System);

	const float ShatterAt8 = FinishAGrantedAt(TEXT("SoulCollector_basic_d_a1"), TEXT("crit_chance"), 8);
	const float RitesAt6 = FinishAGrantedAt(TEXT("SoulCollector_basic_d_a2"), TEXT("attack_damage"), 6);
	if (!TestEqual(TEXT("set-up: Soul Shatter's row at 8 points is 32"), ShatterAt8, 32.0f, FinishAAllowance)
		|| !TestEqual(TEXT("set-up: Final Rites' row at 6 points is 18"), RitesAt6, 18.0f, FinishAAllowance))
	{
		return false;
	}
	TestEqual(TEXT("Soul Shatter: a marked use's critical strike chance has 32 points more increases"),
			  System->IncreasesForStat(FName(TEXT("crit_chance")), Marked)
			  - System->IncreasesForStat(FName(TEXT("crit_chance")), Unmarked),
			  ShatterAt8 / 100.0f, 0.0001f);

	FFinishACreature Common(World, FVector(5.0f * FinishAMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters, 0);
	FFinishACreature Elite(World, FVector(-5.0f * FinishAMetre, 0.0f, 0.0f), ECataclysmTeam::Monsters, 1);
	const auto DamageAgainst = [System](const FGameplayTagContainer& Tags, const AActor* Target)
	{
		return System->StatForSkill(FName(TEXT("attack_damage")), Tags, 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f,
									-1.0f, false, Target);
	};
	const float CommonMarked = DamageAgainst(Marked, Common.Actor);
	const float CommonUnmarked = DamageAgainst(Unmarked, Common.Actor);
	const float EliteMarked = DamageAgainst(Marked, Elite.Actor);
	const float EliteUnmarked = DamageAgainst(Unmarked, Elite.Actor);
	const float Others = System->IncreasesForStat(FName(TEXT("attack_damage")), Unmarked);
	if (!TestTrue(TEXT("set-up: the character's attack damage is above nought"), EliteUnmarked > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("control: against a Common creature a marked use deals what an unmarked one does"), CommonMarked,
			  CommonUnmarked, FinishAAllowance);
	TestEqual(TEXT("Final Rites: against an Elite a marked use has 18% more increases"), EliteMarked / EliteUnmarked,
			  (1.0f + Others + RitesAt6 / 100.0f) / (1.0f + Others), 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishAPyreTest,
	"Cataclysm.DeathFinishSoulCollector.PyreWidensAUseForEveryFiveFervourItSpent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Pyre at 10 points, its figure read from its real row (1 a point, 10): a strike whose use spent 50 (an Ultimate's cost)
 * has ten fives, so its area is 1 + 10 x 10% = 2 times; one whose use spent 14 has two fives, 1.2 times. THE CONTROLS:
 * a use that spent 4 has no five and is unchanged; and with no Pyre a use that spent 50 is unchanged.
 */
bool FCataclysmDeathFinishAPyreTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const float PerFive = FinishAGrantedAt(TEXT("SoulCollector_basic_d_a0"),
										   UCataclysmSkillTemplate::AreaPerFiveFervourSpentStat, 10);
	if (!TestEqual(TEXT("set-up: Pyre's real row at 10 points is 10. If 0, DT_PassiveEffects is older than the CSV"),
				   PerFive, 10.0f, FinishAAllowance))
	{
		return false;
	}
	FFinishAFighter Fighter(World, FVector::ZeroVector);
	UCataclysmStrikeSkill* Wave = FinishAGrant<UCataclysmStrikeSkill>(
		Fighter.AbilitySystem, Fighter.Actor, ECataclysmAbilitySlot::Ultimate, TEXT("Radius=4; Angle=360"));
	if (!TestNotNull(TEXT("set-up: a strike"), Wave))
	{
		return false;
	}

	FinishAWear(Fighter.AbilitySystem, {});
	Wave->FervourSpentByThisUse = 50.0f;
	TestEqual(TEXT("control: with no Pyre a use that spent 50 is unchanged"), Wave->AreaOfEffectMultiplier(), 1.0f,
			  FinishAAllowance);

	FinishAWear(Fighter.AbilitySystem, {{FName(UCataclysmSkillTemplate::AreaPerFiveFervourSpentStat), PerFive}});
	TestEqual(TEXT("with Pyre, 50 spent is ten fives: twice the area"), Wave->AreaOfEffectMultiplier(),
			  1.0f + PerFive * 10.0f / 100.0f, FinishAAllowance);
	Wave->FervourSpentByThisUse = 14.0f;
	TestEqual(TEXT("14 spent is two whole fives: 1.2 times"), Wave->AreaOfEffectMultiplier(),
			  1.0f + PerFive * 2.0f / 100.0f, FinishAAllowance);
	Wave->FervourSpentByThisUse = 4.0f;
	TestEqual(TEXT("control: 4 spent is no five, unchanged"), Wave->AreaOfEffectMultiplier(), 1.0f, FinishAAllowance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishASpiritsOnTheWayTest,
	"Cataclysm.DeathFinishSoulCollector.ASpiritStrikesTheNearestEnemyOnItsWayAndNoOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmFervour::FlySpirits`: a fighter at the origin, a body 10 m along +X. An enemy 4 m along +X and half a metre
 * off the line is on the spirits' way, and each spirit strikes it: one spirit takes some health, two take twice that
 * more. THE CONTROLS: an enemy beside the body but 5 m off the line (nearer the body than the one struck) and one 4 m
 * beyond the body are not on the way and are not struck; and with nothing on the way, a spirit strikes nothing.
 */
bool FCataclysmDeathFinishASpiritsOnTheWayTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);

	const float M = FinishAMetre;
	FFinishAFighter Fighter(World, FVector::ZeroVector);
	FinishAWear(Fighter.AbilitySystem, {});
	const FVector Body(10.0f * M, 0.0f, 0.0f);
	FFinishACreature OnTheWay(World, FVector(4.0f * M, 0.5f * M, 0.0f), ECataclysmTeam::Monsters);
	FFinishACreature Beside(World, FVector(10.0f * M, 5.0f * M, 0.0f), ECataclysmTeam::Monsters);
	FFinishACreature Beyond(World, FVector(14.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);

	const float Full = OnTheWay.Health();
	TestEqual(TEXT("one spirit struck an enemy"), UCataclysmFervour::FlySpirits(Fighter.Actor, nullptr, Body, 1, 30.0f),
			  1);
	const float One = Full - OnTheWay.Health();
	if (!TestTrue(TEXT("the enemy on the way lost health"), One > 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("two spirits struck it twice"), UCataclysmFervour::FlySpirits(Fighter.Actor, nullptr, Body, 2, 30.0f),
			  2);
	TestEqual(TEXT("for twice as much again"), Full - OnTheWay.Health(), 3.0f * One, FinishAAllowance);
	TestEqual(TEXT("control: the enemy 5 m off the line was not struck"), Beside.Health(), Full, FinishAAllowance);
	TestEqual(TEXT("control: nor the one beyond the body"), Beyond.Health(), Full, FinishAAllowance);

	// NOTHING ON THE WAY: the same flight along +Y.
	TestEqual(TEXT("control: with nothing on its way a spirit strikes nothing"),
			  UCataclysmFervour::FlySpirits(Fighter.Actor, nullptr, FVector(0.0f, 20.0f * M, 0.0f), 3, 30.0f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishASpiritChainTest,
	"Cataclysm.DeathFinishSoulCollector.SpiritVolleysKillsLooseSpiritsThroughTheRealKillHandler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Kills make spirits, spirits make kills." A real player paying 1 Fervour a kill and wearing Spirit Volley's 30 (read
 * from its row) kills a creature 6 m along +X. Its one spirit flies home and strikes the creature 3 m along +X, which it
 * kills; that kill's spirit strikes the creature 1.5 m along +X, half a metre off the line, which it kills too. THE
 * CONTROLS: a creature 8 m off to the side, on nobody's way, is untouched; and the same scene without Spirit Volley's
 * row leaves the second and third standing.
 */
bool FCataclysmDeathFinishASpiritChainTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	const float Percent =
		FinishAGrantedAt(TEXT("SoulCollector_capstone_50"), UCataclysmFervour::SpiritVolleyWeaponPercentStat, 1, 2);
	if (!TestEqual(TEXT("set-up: Spirit Volley's real row is 30. If 0, DT_PassiveEffects is older than the CSV"),
				   Percent, 30.0f, FinishAAllowance))
	{
		return false;
	}

	const auto Run = [this, Percent](bool bWithVolley, bool& OutSecond, bool& OutThird, bool& OutAside) -> bool
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		const CataclysmTestWorld::FScopedCritRoll NeverCrits(100.0f);
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Player =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!State || !Player)
		{
			return false;
		}
		Player->SetPlayerState(State);
		Player->OnRep_PlayerState();
		UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
		if (!System)
		{
			return false;
		}
		System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 1000.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 100.0f);
		TMap<FName, float> Flats;
		Flats.Add(FName(UCataclysmFervour::PerKillStat), 1.0f);
		if (bWithVolley)
		{
			Flats.Add(FName(UCataclysmFervour::SpiritVolleyWeaponPercentStat), Percent);
		}
		FinishAWear(System, Flats);

		const float M = FinishAMetre;
		const auto Creature = [World](const FVector& At, float Left) -> ACataclysmEnemyCharacter*
		{
			ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(At, FRotator::ZeroRotator);
			if (Made)
			{
				Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
				Made->SetHealth(1000.0f);
				Made->SetArmour(0.0f);
				if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Made))
				{
					Theirs->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Left);
				}
			}
			return Made;
		};
		ACataclysmEnemyCharacter* First = Creature(FVector(6.0f * M, 0.0f, 0.0f), 1.0f);
		ACataclysmEnemyCharacter* Second = Creature(FVector(3.0f * M, 0.0f, 0.0f), 5.0f);
		ACataclysmEnemyCharacter* Third = Creature(FVector(1.5f * M, 0.5f * M, 0.0f), 5.0f);
		ACataclysmEnemyCharacter* Aside = Creature(FVector(3.0f * M, 8.0f * M, 0.0f), 5.0f);
		if (!First || !Second || !Third || !Aside)
		{
			return false;
		}

		// THE PLAYER'S OWN BLOW KILLS THE FIRST, as `Cataclysm.DeathBursts.` strikes it.
		FCataclysmHitDelivery Delivery;
		Delivery.CritChancePercent = 0.0f;
		UCataclysmSkillEffects::ApplyHit(Player, First, /*DamagePercent=*/100.0f, FGameplayTagContainer(), Delivery);
		const auto Gone = [](const AActor* Who) { return !IsValid(Who) || UCataclysmSkillEffects::IsDead(Who); };
		if (!TestTrue(TEXT("set-up: the player's blow killed the first"), Gone(First)))
		{
			return false;
		}
		OutSecond = Gone(Second);
		OutThird = Gone(Third);
		OutAside = Gone(Aside);
		return true;
	};

	bool bSecond = false;
	bool bThird = false;
	bool bAside = true;
	if (!TestTrue(TEXT("set-up: the scene with Spirit Volley ran"), Run(true, bSecond, bThird, bAside)))
	{
		return false;
	}
	TestTrue(TEXT("the first kill's spirit killed the second, on its way home"), bSecond);
	TestTrue(TEXT("and the second's spirit killed the third"), bThird);
	TestFalse(TEXT("control: the creature 8 m to the side is untouched"), bAside);

	bool bSecondWithout = true;
	bool bThirdWithout = true;
	bool bAsideWithout = true;
	if (TestTrue(TEXT("set-up: the scene without Spirit Volley ran"),
				 Run(false, bSecondWithout, bThirdWithout, bAsideWithout)))
	{
		TestFalse(TEXT("control: without Spirit Volley the second stands"), bSecondWithout);
		TestFalse(TEXT("and the third"), bThirdWithout);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishASiphonAroundTest,
	"Cataclysm.DeathFinishSoulCollector.AMachineThatSiphonsAroundWidensItsPlacersSiphonWhileItStands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Crossbow draft's C4 through `FCataclysmSiphon::Step`: a placer with a Siphon of 6 m stands 12 m from where its
 * machine goes down. With a row stating `SiphonsAround=1` the enemy 3 m from the machine (15 m from the placer) is in the
 * placer's Siphon, beside the enemy 3 m from the placer. THE CONTROLS: an enemy 9 m from the machine is not; the same
 * scene with a row stating no `SiphonsAround` takes in only the enemy near the placer; and once the machine falls, the
 * one near it leaves.
 */
bool FCataclysmDeathFinishASiphonAroundTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	const auto Run = [this](bool bSiphonsAround, int32& OutMembers, bool& OutNearMachine, bool& OutFar,
							bool& OutNearPlacer, bool& OutAfterFall) -> bool
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		if (!World)
		{
			return false;
		}
		ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
		const float M = FinishAMetre;
		FFinishACreature Placer(World, FVector(-12.0f * M, 0.0f, 0.0f), ECataclysmTeam::Players);
		FFinishACreature NearMachine(World, FVector(3.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
		FFinishACreature Far(World, FVector(0.0f, 9.0f * M, 0.0f), ECataclysmTeam::Monsters);
		FFinishACreature NearPlacer(World, FVector(-12.0f * M, 3.0f * M, 0.0f), ECataclysmTeam::Monsters);
		FinishAWear(Placer.AbilitySystem, {{FName(FCataclysmSiphon::RadiusStat), 6.0f}});
		UCataclysmDeployableSkill* Deploy = FinishAGrant<UCataclysmDeployableSkill>(
			Placer.AbilitySystem, Placer.Actor, ECataclysmAbilitySlot::Special,
			bSiphonsAround ? TEXT("Count=1; Duration=5; Minions=BoltTurret:1; FervourReserve=5; SiphonsAround=1")
						   : TEXT("Count=1; Duration=5; Minions=BoltTurret:1; FervourReserve=5"));
		// PLACED, AND HELD AT THE ORIGIN, wherever the spawn put it.
		ACataclysmMinion* Machine = Deploy ? Deploy->DeployOne(TEXT("BoltTurret")) : nullptr;
		ON_SCOPE_EXIT
		{
			if (IsValid(Machine))
			{
				Machine->Destroy();
			}
		};
		if (!Machine)
		{
			return false;
		}
		Machine->SetActorLocation(FVector::ZeroVector, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		OutMembers = FCataclysmSiphon::Step(Placer.Actor, 0.25f);
		const auto& Members = Placer.AbilitySystem->SiphonMembers();
		OutNearMachine = Members.Contains(NearMachine.Actor);
		OutFar = Members.Contains(Far.Actor);
		OutNearPlacer = Members.Contains(NearPlacer.Actor);

		// THE MACHINE FALLS: its health to nought, as anything else dies.
		if (UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Machine))
		{
			Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		}
		FCataclysmSiphon::Step(Placer.Actor, 0.25f);
		OutAfterFall = Placer.AbilitySystem->SiphonMembers().Contains(NearMachine.Actor);
		return true;
	};

	int32 Members = 0;
	bool bNearMachine = false;
	bool bFar = true;
	bool bNearPlacer = false;
	bool bAfterFall = true;
	if (!TestTrue(TEXT("set-up: a machine stating SiphonsAround=1 was placed at the origin"),
				  Run(true, Members, bNearMachine, bFar, bNearPlacer, bAfterFall)))
	{
		return false;
	}
	TestTrue(TEXT("the enemy 3 m from the machine, 15 m from the placer, is in the placer's Siphon"), bNearMachine);
	TestTrue(TEXT("beside the enemy 3 m from the placer"), bNearPlacer);
	TestEqual(TEXT("two members"), Members, 2);
	TestFalse(TEXT("control: the enemy 9 m from the machine is not"), bFar);
	TestFalse(TEXT("control: once the machine falls, the enemy near it has left"), bAfterFall);

	int32 PlainMembers = 0;
	bool bPlainNearMachine = true;
	bool bPlainFar = true;
	bool bPlainNearPlacer = false;
	bool bPlainAfterFall = true;
	if (TestTrue(TEXT("set-up: a machine stating no SiphonsAround was placed at the origin"),
				 Run(false, PlainMembers, bPlainNearMachine, bPlainFar, bPlainNearPlacer, bPlainAfterFall)))
	{
		TestFalse(TEXT("control: without the key the enemy near the machine is not in the Siphon"), bPlainNearMachine);
		TestTrue(TEXT("and the one near the placer still is"), bPlainNearPlacer);
		TestEqual(TEXT("one member"), PlainMembers, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmDeathFinishAEffigyRowTest,
	"Cataclysm.DeathFinishSoulCollector.TheEffigysRowPlacesADecoyThatTauntsReservesItsFervourAndSiphonsAround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Death Crossbow's Effigy, its generated row granted as a weapon grants it: a Deployable placing one Effigy, which
 * taunts within its row's 8 m, reserves its row's 5 Fervour while it stands, is a gadget, and widens its placer's Siphon.
 * Placed, the enemy 3 m from it is taunted by it and, for a placer with a Siphon of 6 m standing 12 m away, is in that
 * Siphon. THE CONTROL: a Boss 3 m from it is not taunted ("Bosses are not fooled").
 */
bool FCataclysmDeathFinishAEffigyRowTest::RunTest(const FString&)
{
	using namespace CataclysmDeathFinishATest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World)
		|| !TestNotNull(TEXT("set-up: the weapon skill table loads"), UCataclysmWeaponSkills::LoadGeneratedTable()))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmWeaponSkill Row = FinishADeathRow(TEXT("Crossbow"), ECataclysmAbilitySlot::Special);
	if (!TestEqual(TEXT("set-up: the Crossbow's Special is the Effigy"), Row.Name, FString(TEXT("Effigy")))
		|| !TestTrue(TEXT("set-up: a Deployable. If not, DT_WeaponSkills is older than the CSV"),
					 Row.Shape == ECataclysmSkillShape::Deployable))
	{
		return false;
	}

	const float M = FinishAMetre;
	FFinishACreature Placer(World, FVector(-12.0f * M, 0.0f, 0.0f), ECataclysmTeam::Players);
	FFinishACreature Enemy(World, FVector(3.0f * M, 0.0f, 0.0f), ECataclysmTeam::Monsters);
	FFinishACreature Boss(World, FVector(0.0f, 3.0f * M, 0.0f), ECataclysmTeam::Monsters, /*RarityStep=*/4);
	FinishAWear(Placer.AbilitySystem, {{FName(FCataclysmSiphon::RadiusStat), 6.0f}});
	UCataclysmDeployableSkill* Effigy = FinishAGrantRow<UCataclysmDeployableSkill>(Placer.AbilitySystem, Placer.Actor, Row);
	if (!TestNotNull(TEXT("set-up: the Effigy granted"), Effigy)
		|| !TestEqual(TEXT("it places one kind"), Effigy->Params.Minions.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("an Effigy"), Effigy->Params.Minions[0].Type, FString(TEXT("Effigy")));
	TestTrue(TEXT("it taunts"), Effigy->Params.bTaunts);
	TestTrue(TEXT("and siphons around"), Effigy->Params.bSiphonsAround);
	TestEqual(TEXT("within 8 m"), Effigy->Params.RadiusCm, 8.0f * M, FinishAAllowance);
	TestEqual(TEXT("reserving 5"), Effigy->Params.FervourReserve, 5.0f, FinishAAllowance);

	const float ReservedBefore = UCataclysmCommand::ReservedFervourOf(Placer.Actor);
	ACataclysmMinion* Placed = Effigy->DeployOne(Effigy->Params.Minions[0].Type);
	ON_SCOPE_EXIT
	{
		if (IsValid(Placed))
		{
			Placed->Destroy();
		}
	};
	if (!TestNotNull(TEXT("set-up: an Effigy was placed. If not, DT_MinionTypes is older than the CSV"), Placed))
	{
		return false;
	}
	TestEqual(TEXT("it is the Effigy kind"), Placed->TypeName, FString(TEXT("Effigy")));
	TestTrue(TEXT("a gadget"), Placed->IsGadget());
	TestTrue(TEXT("marked to widen its placer's Siphon"), Placed->bWidensSummonersSiphon);
	TestEqual(TEXT("it reserves its row's 5 while it stands"),
			  UCataclysmCommand::ReservedFervourOf(Placer.Actor) - ReservedBefore, Effigy->Params.FervourReserve,
			  FinishAAllowance);
	TestTrue(TEXT("the enemy 3 m off is taunted by it"), UCataclysmTaunt::TaunterOf(Enemy.Actor) == Placed);
	TestTrue(TEXT("control: the Boss 3 m off is not"), UCataclysmTaunt::TaunterOf(Boss.Actor) != Placed);

	Placed->SetActorLocation(FVector::ZeroVector, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	FCataclysmSiphon::Step(Placer.Actor, 0.25f);
	TestTrue(TEXT("and the enemy 3 m from it, 15 m from its placer, is in the placer's Siphon"),
			 Placer.AbilitySystem->SiphonMembers().Contains(Enemy.Actor));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
