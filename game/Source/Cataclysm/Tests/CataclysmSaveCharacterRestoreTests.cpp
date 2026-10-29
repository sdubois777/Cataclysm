// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmPassivePoints.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmTestWorld.h"
#include "UObject/Package.h"

namespace CataclysmSaveCharacterRestoreTest
{
	/** A player state, a controller and a character, possessed, as `CataclysmHealthWrittenDirectlyTests.cpp` makes one. */
	struct FPlayer
	{
		ACataclysmPlayerState* State = nullptr;
		APlayerController* Controller = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;

		explicit FPlayer(UWorld* World)
		{
			State = World->SpawnActor<ACataclysmPlayerState>();
			Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (State && Controller && Character)
			{
				Controller->SetPlayerState(State);
				Controller->Possess(Character);
			}
		}

		bool IsUsable() const
		{
			return State && Character && Character->GetInventory() && Character->GetEquipment()
				&& Character->GetAbilitySystemComponent();
		}
	};

	/** The fake node and boss the allocation is handed with. A node no tree holds adds nothing to the stat line. */
	const TCHAR* TestNode = TEXT("Restore_test_node");
	const TCHAR* TestBoss = TEXT("Test_boss");

	/** An item of a named base carrying one top-tier flat health affix, so it moves the stat line. */
	FCataclysmItem WithHealth(const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(TEXT("Stat_Flat_maximum_health"));
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;
		Item.Affixes.Add(Rolled);
		return Item;
	}

	/**
	 * Makes `Player` the character the round trip saves, through the game's own calls. The allocation is handed over
	 * at level 10 and the character then raised to 40, so the figure it was handed with is 3 while computing it again at
	 * 40 would give 0. Returns false, with a reason, if a call refuses.
	 */
	bool Play(const FPlayer& Player, FString& OutWhy)
	{
		ACataclysmPlayerState& State = *Player.State;
		State.SetLevelAndExperience(10, 0);
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(TestNode), UCataclysmPassivePoints::Available(10, 1) + 3);
		State.SetPassiveAllocation(Allocation, {FName(TestBoss)});
		State.SetLevelAndExperience(40, 1234);
		State.SetLifetimeKills(321);
		State.SetCreationChoice(FName(TEXT("Greatsword")), FName(TEXT("War")));
		if (!State.SpendAttributePoints(TEXT("vitality"), 12, OutWhy) || !State.SpendAttributePoints(TEXT("agility"), 5, OutWhy))
		{
			return false;
		}

		UCataclysmEquipmentComponent& Equipment = *Player.Character->GetEquipment();
		FCataclysmItem Sword = WithHealth(TEXT("Weapon_Sword"));
		Sword.DamageTypes = {FName(TEXT("War"))};
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		Equipment.EquipInto(Sword, ECataclysmGearSlot::Weapon1, Removed, AlsoRemoved);
		Equipment.EquipInto(WithHealth(TEXT("Ring_Band")), ECataclysmGearSlot::Ring3, Removed, AlsoRemoved);

		UCataclysmInventoryComponent& Inventory = *Player.Character->GetInventory();
		FCataclysmItem Helm;
		Helm.Base = FName(TEXT("Head_Helm"));
		FCataclysmItem Boots;
		Boots.Base = FName(TEXT("Boots_Sabatons"));
		const int32 HelmAt = Inventory.AddItem(Helm);
		if (HelmAt != 3)
		{
			Inventory.SwapSlots(HelmAt, 3);
		}
		const int32 BootsAt = Inventory.AddItem(Boots);
		if (BootsAt != 7)
		{
			Inventory.SwapSlots(BootsAt, 7);
		}

		if (!State.AssignSkill(ECataclysmAbilitySlot::Heavy, FName(TEXT("War_Sword_Heavy")), OutWhy))
		{
			return false;
		}
		return true;
	}

	/** Every value the character record holds, as the player now holds it. */
	FString Held(const FPlayer& Player)
	{
		const ACataclysmPlayerState& State = *Player.State;
		const FCataclysmAttributePoints& Points = State.GetSpentAttributePoints();
		FString Out = FString::Printf(TEXT("level %d exp %lld kills %d choice %s %s | points %d %d %d %d %d %d %d %d"
										   " | passive %d node %d figure %d bosses"),
									  State.GetCharacterLevel(), State.GetExperienceIntoLevel(), State.GetLifetimeKills(),
									  *State.GetChosenWeaponType().ToString(), *State.GetChosenDamageType().ToString(),
									  Points.Agility, Points.Ferocity, Points.Constitution, Points.Vitality, Points.Mind,
									  Points.Spirit, Points.Efficacy, Points.Luck, State.GetPassiveAllocation().Total(),
									  State.GetPassiveAllocation().PointsIn(FName(TestNode)),
									  State.PassivePointsAnAllocationWasHandedWith());
		for (const FName& Boss : State.GetDefeatedCataclysmBosses())
		{
			Out += TEXT(" ") + Boss.ToString();
		}
		Out += TEXT(" | worn");
		for (int32 Slot = 0; Slot < static_cast<int32>(ECataclysmGearSlot::Count); ++Slot)
		{
			const FCataclysmItem* Item = Player.Character->GetEquipment()->EquippedAt(static_cast<ECataclysmGearSlot>(Slot));
			if (Item && !Item->Base.IsNone())
			{
				Out += FString::Printf(TEXT(" %d:%s/%d"), Slot, *Item->Base.ToString(), Item->Affixes.Num());
			}
		}
		Out += TEXT(" | bag");
		const TArray<FCataclysmCarriedSlot>& Slots = Player.Character->GetInventory()->GetSlots();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (!Slots[Index].Item.Base.IsNone() || !Slots[Index].Material.IsNone())
			{
				Out += FString::Printf(TEXT(" %d:%s%s"), Index, *Slots[Index].Item.Base.ToString(),
									   *Slots[Index].Material.ToString());
			}
		}
		Out += TEXT(" | skills");
		for (const FCataclysmSkillChoice& Choice : State.GetSkillAssignment())
		{
			Out += FString::Printf(TEXT(" %d=%s"), static_cast<int32>(Choice.Slot), *Choice.SkillRow.ToString());
		}
		return Out;
	}

	/** The whole derived stat line: every attribute but the four pools' current values, which the record does not hold. */
	FString StatLine(const FPlayer& Player)
	{
		const UAbilitySystemComponent* System = Player.Character->GetAbilitySystemComponent();
		TArray<FGameplayAttribute> Attributes;
		System->GetAllAttributes(Attributes);
		TArray<FString> Lines;
		for (const FGameplayAttribute& Attribute : Attributes)
		{
			const FString Name = Attribute.GetName();
			if (Name == TEXT("Health") || Name == TEXT("Mana") || Name == TEXT("EnergyShield") || Name == TEXT("ClassResource"))
			{
				continue;
			}
			Lines.Add(FString::Printf(TEXT("%s=%.3f"), *Name, System->GetNumericAttribute(Attribute)));
		}
		Lines.Sort();
		return FString::Join(Lines, TEXT(" "));
	}

	/** The record the writer would make of `Player`, written to JSON and read back. */
	UCataclysmCharacterSave* SavedAndReadBack(const FPlayer& Player, FString& OutWhy)
	{
		UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
		Record->SchemaVersion = UCataclysmCharacterSave::SchemaVersionNow;
		if (!FCataclysmSaveGather::CharacterFrom(*Player.Character, *Record))
		{
			OutWhy = TEXT("the gather found nothing to read");
			return nullptr;
		}
		FString Json;
		if (!FCataclysmSaveStorage::ToJson(Record, Json, OutWhy))
		{
			return nullptr;
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
		return Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
			Json, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, OutWhy));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveCharacterRestoreTest,
	"Cataclysm.SaveApply.ARestoredCharacterHoldsWhatItWasSavedWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #753, its character step. A character played through the game's own calls is saved, written to JSON, read
 * back and put into a second possessed player with `FCataclysmSaveApply::CharacterInto`. The second holds every value
 * the first holds, the figure its allocation was handed with included, and has the same derived stat line. CONTROL: a
 * third player played by the same calls as the first has the same stat line as the first, so the stat line depends
 * only on those values and a mismatch after a restore is the restore's fault.
 */
bool FCataclysmSaveCharacterRestoreTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSaveCharacterRestoreTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FPlayer Original(World);
	const FPlayer Restored(World);
	const FPlayer Twin(World);
	FString Why;
	if (!TestTrue(TEXT("set-up: three possessed players"), Original.IsUsable() && Restored.IsUsable() && Twin.IsUsable())
		|| !TestTrue(TEXT("set-up: the first is played"), Play(Original, Why))
		|| !TestTrue(TEXT("set-up: the twin is played the same way"), Play(Twin, Why)))
	{
		AddError(Why);
		return false;
	}

	// SET-UP: THE FIGURE CANNOT BE WORKED OUT AGAIN. It is 3, and computing it at level 40 would give 0.
	const int32 Figure = Original.State->PassivePointsAnAllocationWasHandedWith();
	const int32 Recomputed = FMath::Max(0, Original.State->GetPassiveAllocation().Total()
											   - UCataclysmPassivePoints::Available(40, 1));
	if (!TestEqual(TEXT("set-up: the allocation was handed with 3 points the level did not pay for"), Figure, 3)
		|| !TestTrue(TEXT("set-up: and computing it again at level 40 would give a different figure"), Recomputed != Figure))
	{
		return false;
	}

	UCataclysmCharacterSave* Back = SavedAndReadBack(Original, Why);
	if (!TestNotNull(TEXT("set-up: the character is saved and read back"), Back))
	{
		AddError(Why);
		return false;
	}
	int32 Refused = 0;
	if (!TestTrue(TEXT("set-up: the record is put back into the second player"),
				  FCataclysmSaveApply::CharacterInto(*Back, *Restored.Character, Refused, Why)))
	{
		AddError(Why);
		return false;
	}

	const FString OriginalHeld = Held(Original);
	const FString RestoredHeld = Held(Restored);
	const FString OriginalStats = StatLine(Original);
	AddInfo(FString::Printf(TEXT("CHARACTER RESTORE: figure %d saved, %d restored; vitality %d saved, %d restored; %d skill "
								 "choices refused; held values %s; stat line %s; twin's stat line %s"),
							Figure, Restored.State->PassivePointsAnAllocationWasHandedWith(),
							Original.State->GetSpentAttributePoints().Vitality,
							Restored.State->GetSpentAttributePoints().Vitality, Refused,
							RestoredHeld == OriginalHeld ? TEXT("match") : TEXT("DIFFER"),
							StatLine(Restored) == OriginalStats ? TEXT("matches") : TEXT("DIFFERS"),
							StatLine(Twin) == OriginalStats ? TEXT("matches") : TEXT("DIFFERS")));
	if (RestoredHeld != OriginalHeld)
	{
		AddInfo(FString::Printf(TEXT("CHARACTER RESTORE HELD: saved [%s] restored [%s]"), *OriginalHeld, *RestoredHeld));
	}
	TestEqual(TEXT("the figure the allocation was handed with is put back as saved"),
			  Restored.State->PassivePointsAnAllocationWasHandedWith(), Figure);
	TestEqual(TEXT("the spent attribute points are put back"), Restored.State->GetSpentAttributePoints().Vitality,
			  Original.State->GetSpentAttributePoints().Vitality);
	TestEqual(TEXT("every value the record holds is put back"), RestoredHeld, OriginalHeld);
	TestEqual(TEXT("and the derived stat line is the same"), StatLine(Restored), OriginalStats);
	TestEqual(TEXT("no saved skill choice is refused"), Refused, 0);
	TestEqual(TEXT("CONTROL: a twin played by the same calls has the same stat line"), StatLine(Twin), OriginalStats);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveCharacterRefusedTest,
	"Cataclysm.SaveApply.ACharacterRecordThatCannotBeRestoredChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Issue #753. A record put into a character with no player state is refused with a reason, and so is a record whose
 * carried slots are not the inventory's 48; the character handed over is unchanged both times. CONTROL: the same
 * record whole, into a possessed player, is accepted.
 */
bool FCataclysmSaveCharacterRefusedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmSaveCharacterRestoreTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FPlayer Source(World);
	FString Why;
	if (!TestTrue(TEXT("set-up: a possessed player"), Source.IsUsable()) || !TestTrue(TEXT("set-up: played"), Play(Source, Why)))
	{
		AddError(Why);
		return false;
	}
	UCataclysmCharacterSave* Back = SavedAndReadBack(Source, Why);
	if (!TestNotNull(TEXT("set-up: saved and read back"), Back))
	{
		AddError(Why);
		return false;
	}

	// NO PLAYER STATE.
	ACataclysmPlayerCharacter* Stateless =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector(500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a character with no player state"), Stateless))
	{
		return false;
	}
	const auto BagOf = [](const ACataclysmPlayerCharacter& Character)
	{
		FString Out;
		for (const FCataclysmCarriedSlot& Slot : Character.GetInventory()->GetSlots())
		{
			Out += Slot.Item.Base.ToString() + Slot.Material.ToString() + TEXT("|");
		}
		return Out;
	};
	const FString StatelessBag = BagOf(*Stateless);
	int32 Refused = 0;
	FString NoState;
	const bool bNoStateAccepted = FCataclysmSaveApply::CharacterInto(*Back, *Stateless, Refused, NoState);
	const bool bStatelessBagUnchanged = BagOf(*Stateless) == StatelessBag;

	// THE WRONG NUMBER OF CARRIED SLOTS.
	const FPlayer Target(World);
	if (!TestTrue(TEXT("set-up: a second possessed player"), Target.IsUsable()))
	{
		return false;
	}
	UCataclysmCharacterSave* Short = DuplicateObject<UCataclysmCharacterSave>(Back, GetTransientPackage());
	Short->CarriedSlots.Pop();
	const FString Before = Held(Target) + StatLine(Target);
	FString WrongCount;
	const bool bShortAccepted = FCataclysmSaveApply::CharacterInto(*Short, *Target.Character, Refused, WrongCount);
	const FString After = Held(Target) + StatLine(Target);

	// CONTROL: THE SAME RECORD WHOLE.
	FString Whole;
	const bool bWholeAccepted = FCataclysmSaveApply::CharacterInto(*Back, *Target.Character, Refused, Whole);

	AddInfo(FString::Printf(TEXT("CHARACTER REFUSAL: no player state %s (\"%s\"); %d carried slots %s (\"%s\"), the player "
								 "%s; the whole record %s"),
							bNoStateAccepted ? TEXT("accepted") : TEXT("refused"), *NoState, Short->CarriedSlots.Num(),
							bShortAccepted ? TEXT("accepted") : TEXT("refused"), *WrongCount,
							Before == After ? TEXT("unchanged") : TEXT("CHANGED"),
							bWholeAccepted ? TEXT("accepted") : TEXT("refused")));
	TestFalse(TEXT("a character with no player state is refused"), bNoStateAccepted);
	TestFalse(TEXT("with a reason"), NoState.IsEmpty());
	TestTrue(TEXT("and its bag is unchanged"), bStatelessBagUnchanged);
	TestFalse(TEXT("a record without the inventory's 48 slots is refused"), bShortAccepted);
	TestFalse(TEXT("with a reason"), WrongCount.IsEmpty());
	TestEqual(TEXT("and the player handed over is unchanged"), After, Before);
	TestTrue(TEXT("CONTROL: the same record whole is accepted"), bWholeAccepted);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
