// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmSkillLevels.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

/**
 * A THRALL LOADED FROM A SAVE KEEPS THE SKILL THAT TOOK IT. Issue #2840, ruled by the coordinating session on
 * 2026-11-22. `UCataclysmSummonSkill::Possess` sets two fields on a creature it takes, read once at the take:
 * `TakenBySkillRow`, the row its blows credit and whose levelling lowers its Fervour reserve, and
 * `TakenLevelFactorOnCreatures`, the level factor its blows on creatures carry. Until #2840 neither was saved.
 *
 * `Cataclysm.ThrallSave.` holds three tests: the save and load in `CataclysmSaveLoadTests.cpp`, beside the thrall test
 * whose scene it uses, and these two. WRITTEN BEFORE THE CHANGE, WITHOUT A BUILD.
 */
namespace CataclysmThrallSaveTest
{
	const FName Subjugate(TEXT("Demonic_Staff_Ultimate"));

	/** Level 10's cost factor written out, 0.9642 to the ninth, as `Cataclysm.LevelCosts.` writes it. */
	constexpr float AtTen = 0.72028f;

	const UCataclysmRunSave* ReadRunFixture(FAutomationTestBase& Test, const TCHAR* Name,
											ECataclysmSaveLoadResult& OutResult)
	{
		FString Text;
		FString Reason;
		if (!CataclysmSaveFixtures::Read(Name, Text, Reason))
		{
			Test.AddError(Reason);
			return nullptr;
		}
		FString Message;
		OutResult = ECataclysmSaveLoadResult::NotValidJson;
		const UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
			Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), OutResult, Message));
		if (Read == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s would not load: %s -- %s"), Name,
										  FCataclysmSaveStorage::Describe(OutResult), *Message));
		}
		return Read;
	}

	/** The saved creature of `Archetype` on the run file's floor, or null. */
	const FCataclysmSavedCreature* CreatureOf(const UCataclysmRunSave* Run, const TCHAR* Archetype)
	{
		const FName Row(Archetype);
		return Run ? Run->Floor.Creatures.FindByPredicate(
						 [&Row](const FCataclysmSavedCreature& One) { return One.ArchetypeRow == Row; })
				   : nullptr;
	}

	FString Described(const FCataclysmSavedCreature* Creature)
	{
		return Creature ? FString::Printf(TEXT("%s commanded %d row %s factor %.2f"), *Creature->ArchetypeRow.ToString(),
										  Creature->bCommandedByThePlayer ? 1 : 0,
										  *Creature->TakenBySkillRow.ToString(), Creature->TakenLevelFactorOnCreatures)
						: FString(TEXT("(none)"));
	}

	/** The player `Cataclysm.LevelCosts.` makes: possessed, holding a Demonic Staff and its skills, a pool of 150, and
	 *  at character level 32 so its skills can reach 10. Copied into this file's namespace for the unity build. */
	struct FStaffPlayer
	{
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* Abilities = nullptr;
		bool IsUsable() const { return State && Character && Abilities; }
	};

	FStaffPlayer StaffPlayerIn(UWorld* World)
	{
		FStaffPlayer Out;
		Out.State = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Out.State || !Controller || !Character)
		{
			return Out;
		}
		Controller->SetPlayerState(Out.State);
		Controller->Possess(Character);
		UCataclysmAbilitySystemComponent* System = Out.State->GetCataclysmAbilitySystemComponent();
		UCataclysmWeaponSlotsComponent* Slots = Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
		if (!System || !Slots)
		{
			return Out;
		}
		Slots->SetDamageType(TEXT("Demonic"));
		Slots->EquipWeaponType(TEXT("Staff"));
		System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), 150.0f);
		System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 150.0f);
		Out.State->SetLevelAndExperience(32, 0);
		Out.Character = Character;
		Out.Abilities = System;
		return Out;
	}

	bool HoldsSubjugate(UAbilitySystemComponent* Abilities)
	{
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			const UCataclysmSummonSkill* Skill = Cast<UCataclysmSummonSkill>(Spec.GetPrimaryInstance());
			if (Skill && Skill->SkillRow == Subjugate)
			{
				return true;
			}
		}
		return false;
	}

	/** A Brute's saved record as a thrall of the player's, holding 30 Fervour back, taken by `TakenBy` at level 1. */
	FCataclysmSavedCreature ThrallRecord(const FVector& At, FName TakenBy)
	{
		FCataclysmSavedCreature Saved;
		Saved.ArchetypeRow = FName(TEXT("Brute"));
		Saved.Location = At;
		Saved.Health = 100.0f;
		Saved.MaxHealth = 100.0f;
		Saved.bCommandedByThePlayer = true;
		Saved.ReservedFervour = 30.0f;
		Saved.TakenBySkillRow = TakenBy;
		Saved.TakenLevelFactorOnCreatures = 1.0f;
		return Saved;
	}
}

// --------------------------------------------------------------------------------------------------------------------
// TS2. A thrall in an earlier run file is given Subjugate at level 1; the newest file is read as it states.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmThrallSaveMigrationTest,
	"Cataclysm.ThrallSave.AThrallInAnEarlierRunFileIsGivenSubjugateAtLevelOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Run_v16.json`, written before the two fields, migrates: its thrall, the Abyssal Warden the player commands, is given
 * Subjugate's row and a factor of 1 (the ruling's Q1), and the Brute beside it, which no one commands, None and 1.
 * CONTROL: `Run_v18.json` is read, not migrated, and states its thrall's row and a factor of 1.5, not the defaults, so
 * the fields are read from the file. Prints "THRALL FILES".
 */
bool FCataclysmThrallSaveMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmThrallSaveTest;
	ECataclysmSaveLoadResult OldResult = ECataclysmSaveLoadResult::NotValidJson;
	ECataclysmSaveLoadResult NewResult = ECataclysmSaveLoadResult::NotValidJson;
	const UCataclysmRunSave* Old = ReadRunFixture(*this, TEXT("Run_v16.json"), OldResult);
	const UCataclysmRunSave* Committed = ReadRunFixture(*this, TEXT("Run_v18.json"), NewResult);
	const FCataclysmSavedCreature* OldThrall = CreatureOf(Old, TEXT("AbyssalWarden"));
	const FCataclysmSavedCreature* OldBrute = CreatureOf(Old, TEXT("Brute"));
	const FCataclysmSavedCreature* NewThrall = CreatureOf(Committed, TEXT("AbyssalWarden"));
	const FCataclysmSavedCreature* NewBrute = CreatureOf(Committed, TEXT("Brute"));
	AddInfo(FString::Printf(TEXT("THRALL FILES: Run_v16.json %s: %s | %s; Run_v18.json %s: %s | %s"),
							FCataclysmSaveStorage::Describe(OldResult), *Described(OldThrall), *Described(OldBrute),
							FCataclysmSaveStorage::Describe(NewResult), *Described(NewThrall), *Described(NewBrute)));

	if (!TestNotNull(TEXT("Run_v16.json loads"), Old) || !TestNotNull(TEXT("Run_v18.json loads"), Committed)
		|| !TestNotNull(TEXT("set-up: Run_v16.json holds its thrall"), OldThrall)
		|| !TestNotNull(TEXT("set-up: and its Brute"), OldBrute) || !TestNotNull(TEXT("set-up: Run_v18.json its thrall"), NewThrall)
		|| !TestNotNull(TEXT("set-up: and its Brute"), NewBrute))
	{
		return false;
	}
	TestEqual(TEXT("Run_v16.json is migrated"), static_cast<int32>(OldResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestTrue(TEXT("set-up: its thrall is the player's"), OldThrall->bCommandedByThePlayer);
	TestEqual(TEXT("**its thrall is given Subjugate's row**"), OldThrall->TakenBySkillRow, Subjugate);
	TestEqual(TEXT("**and Subjugate's factor at level 1**"), OldThrall->TakenLevelFactorOnCreatures, 1.0f);
	TestTrue(TEXT("the Brute no one commands is given no skill"), OldBrute->TakenBySkillRow.IsNone());
	TestEqual(TEXT("and a factor of 1"), OldBrute->TakenLevelFactorOnCreatures, 1.0f);
	TestEqual(TEXT("CONTROL: Run_v18.json is read, not migrated"), static_cast<int32>(NewResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestEqual(TEXT("CONTROL: its thrall's row as it states"), NewThrall->TakenBySkillRow, Subjugate);
	TestEqual(TEXT("CONTROL: and its factor as it states, 1.5, not the default"), NewThrall->TakenLevelFactorOnCreatures,
			  1.5f);
	TestTrue(TEXT("CONTROL: its Brute names no skill"), NewBrute->TakenBySkillRow.IsNone());
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// TS3. A loaded thrall's reserve falls when Subjugate levels; one loaded with no skill does not.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmThrallSaveReserveTest,
	"Cataclysm.ThrallSave.ALoadedThrallsReserveFallsWhenSubjugateLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two thralls restored from saved records as a load restores them (`FCataclysmSaveApply::CreatureInto`, then
 * `ThrallInto`), each holding 30 Fervour back: one saved with Subjugate as the skill that took it, one with none -- a
 * thrall loaded before #2840. When Subjugate reaches level 10, the first is re-priced to 30 x 0.72028, as a thrall taken
 * in play is (`Cataclysm.LevelCosts.AHeldThrallIsRepricedWhenItsSkillLevels`), because the row is what
 * `UCataclysmSkillLevels::RepriceReservesOf` matches; CONTROL: the second keeps its 30. Prints "THRALL RESERVE".
 */
bool FCataclysmThrallSaveReserveTest::RunTest(const FString&)
{
	using namespace CataclysmThrallSaveTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	FStaffPlayer Player = StaffPlayerIn(World);
	if (!TestTrue(TEXT("set-up: a possessed player holding the Staff's Subjugate"),
				  Player.IsUsable() && HoldsSubjugate(Player.Abilities)))
	{
		return false;
	}

	const FCataclysmSavedCreature WithTheRow = ThrallRecord(FVector(300.0f, 0.0f, 100.0f), Subjugate);
	const FCataclysmSavedCreature WithNone = ThrallRecord(FVector(-300.0f, 0.0f, 100.0f), NAME_None);
	ACataclysmEnemyCharacter* Taken = FCataclysmSaveApply::CreatureInto(*World, WithTheRow);
	ACataclysmEnemyCharacter* Unnamed = FCataclysmSaveApply::CreatureInto(*World, WithNone);
	if (!TestNotNull(TEXT("set-up: the first thrall stands"), Taken)
		|| !TestNotNull(TEXT("set-up: the second thrall stands"), Unnamed)
		|| !TestTrue(TEXT("set-up: the first is re-linked as the player's"),
					 FCataclysmSaveApply::ThrallInto(*Taken, WithTheRow, *Player.Character))
		|| !TestTrue(TEXT("set-up: and the second"), FCataclysmSaveApply::ThrallInto(*Unnamed, WithNone, *Player.Character)))
	{
		return false;
	}
	const FName RowAfterTheLoad = Taken->TakenBySkillRow;
	const float HeldAtOne = Taken->ReservedFervour;
	Player.State->SetSkillExperience(Subjugate, UCataclysmSkillLevels::ExperienceToReach(10));
	AddInfo(FString::Printf(TEXT("THRALL RESERVE: row after the load %s; Subjugate at level %d; the named thrall %.2f to "
								 "%.2f, the unnamed %.2f"),
							*RowAfterTheLoad.ToString(), Player.State->SkillLevelOf(Subjugate), HeldAtOne,
							Taken->ReservedFervour, Unnamed->ReservedFervour));

	TestEqual(TEXT("set-up: the restored thrall names Subjugate"), RowAfterTheLoad, Subjugate);
	TestEqual(TEXT("set-up: and holds the 30 it was saved with"), HeldAtOne, 30.0f, 0.001f);
	TestEqual(TEXT("set-up: Subjugate is at level 10"), Player.State->SkillLevelOf(Subjugate), 10);
	TestEqual(TEXT("**its reserve falls to 30 x 0.72028, 21.6**"), Taken->ReservedFervour, 30.0f * AtTen, 0.01f);
	TestEqual(TEXT("CONTROL: a thrall loaded with no skill keeps its 30"), Unnamed->ReservedFervour, 30.0f, 0.001f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
