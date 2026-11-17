// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmConsumptionDoubleCharacter.h"
#include "Character/CataclysmCorruptedStalkerCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Dom/JsonObject.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interface/CataclysmFloorModifierPanelLayout.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWearing.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Player/CataclysmPowerScore.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveMigration.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Save/CataclysmSaveWriter.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the corrupted double's build, slice 2a of issue #2688.
 *
 * The double wears a player's build (`FCataclysmPlayerBuild`) through the same gatherer a player's refresh uses
 * (`UCataclysmEquipmentComponent::RefreshAttributesAs`): the player's own numbers, with no tier rescale, re-roll or
 * multiplier (R3, R4, ruled 2026-11-02); none of its gear drops (O2, the owner's decision of 2026-11-02); and its brain
 * skips a held skill with no shape (no War work). The first three tests spawn it themselves.
 *
 * SLICES 2B AND 2C, BELOW THEM: the capture at the crossing and its save, the arrival on the next floor entered, the
 * hunt across floors and across a save, and what the double's death does. See the comment above their namespace.
 *
 * NO TEST HERE SETS A TIER. A world built by a test has no game mode, so the tier is 1.
 */
namespace CataclysmConsumptionDoubleTest
{
	/** A world with a possessed player character and its state. */
	struct FDoubleTestScene
	{
		UWorld* World = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;

		bool IsUsable() const { return World && State && Character && Character->GetEquipment(); }
	};

	FDoubleTestScene MakeDoubleTestScene()
	{
		FDoubleTestScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.World->SetGameInstance(NewObject<UCataclysmGameInstance>());
		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Out.State && Controller && Out.Character)
		{
			Controller->SetPlayerState(Out.State);
			Controller->Possess(Out.Character);
		}
		return Out;
	}

	FCataclysmRolledAffix RolledAffix(const TCHAR* Affix, int32 Tier, float Roll)
	{
		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(Affix);
		Rolled.Tier = Tier;
		Rolled.Roll = Roll;
		return Rolled;
	}

	/** A Greataxe carrying `DamageTypes`, at gear level 3, with one rolled damage affix. */
	FCataclysmItem DoubleTestGreataxe(const TArray<FName>& DamageTypes)
	{
		FCataclysmItem Axe;
		Axe.Base = FName(TEXT("Weapon_Greataxe"));
		Axe.GearLevel = 3;
		Axe.DamageTypes = DamageTypes;
		Axe.Affixes = {RolledAffix(TEXT("Stat_Flat_damage"), 2, 0.55f)};
		return Axe;
	}

	ACataclysmConsumptionDoubleCharacter* SpawnDouble(UWorld* World, const FVector& Where)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmConsumptionDoubleCharacter* Double = World->SpawnActor<ACataclysmConsumptionDoubleCharacter>(
			ACataclysmConsumptionDoubleCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (Double)
		{
			Double->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		}
		return Double;
	}

	float DoubleTestRead(const AActor* Actor, const FGameplayAttribute& Attribute)
	{
		const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Actor);
		return Abilities && Abilities->HasAttributeSetForAttribute(Attribute)
			? Abilities->GetNumericAttribute(Attribute) : -1.0f;
	}

	/** The authored Demonic Stalker entry, or an empty one. */
	FCataclysmStalkerEntry DoubleTestDemonicEntry()
	{
		for (const FCataclysmStalkerEntry& Entry : ACataclysmCorruptedStalkerCharacter::Entries())
		{
			if (Entry.DamageType == FName(TEXT("Demonic")))
			{
				return Entry;
			}
		}
		return FCataclysmStalkerEntry();
	}

	/** A Ravager's build at level 20 with Demonic chosen at creation, wearing only `Worn`. Built by hand, not captured. */
	FCataclysmPlayerBuild HandMadeBuild(const TArray<FCataclysmWornItem>& Worn, const TArray<FCataclysmSkillChoice>& Choices)
	{
		FCataclysmPlayerBuild Build;
		Build.Worn = Worn;
		Build.Level = 20;
		Build.ClassName = TEXT("Ravager");
		Build.CreationDamageType = FName(TEXT("Demonic"));
		Build.SkillAssignment = Choices;
		return Build;
	}

	FCataclysmWornItem WornIn(ECataclysmGearSlot Slot, const FCataclysmItem& Item)
	{
		FCataclysmWornItem Worn;
		Worn.Slot = Slot;
		Worn.Item = Item;
		return Worn;
	}

	int32 DropsOnTheFloor(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}
}

// THE PLAYER'S OWN NUMBERS, NOT THE STALKER'S: a double built from a live player at tier 1 has the player's maximum
// health, mana, armour and attack damage, and its worn items carry the player's rolls exactly (R3, R4).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleOwnNumbersTest,
	"Cataclysm.ConsumptionDouble.TheDoubleHoldsThePlayersOwnNumbersNotTheStalkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleOwnNumbersTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmConsumptionDoubleTest;
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	FDoubleTestScene Scene = MakeDoubleTestScene();
	if (!TestTrue(TEXT("set-up: a possessed player with its state"), Scene.IsUsable()))
	{
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// THE PLAYER: level 37, attribute points spent, thirteen passive points in the Ravager's tree, and three items
	// with known rolls. The starting Greataxe comes off first.
	UCataclysmEquipmentComponent* Equipment = Scene.Character->GetEquipment();
	Equipment->UnequipEverything();
	if (!TestEqual(TEXT("set-up: nothing worn after taking everything off"), Equipment->NumEquipped(), 0))
	{
		return false;
	}
	Scene.State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("Demonic")));
	Scene.State->SetLevelAndExperience(37, 0);
	FCataclysmAttributePoints Points;
	Points.Constitution = 10;
	Points.Ferocity = 6;
	Scene.State->RestoreSpentAttributePoints(Points);
	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Ravager_basic_spine_000")), 1);
	Allocation.Add(FName(TEXT("Ravager_basic_spine_001")), 12);
	Scene.State->SetPassiveAllocation(Allocation, TArray<FName>());

	FCataclysmItem Helm;
	Helm.Base = FName(TEXT("Head_Helm"));
	Helm.GearLevel = 2;
	Helm.Affixes = {RolledAffix(TEXT("Stat_Flat_maximum_health"), 2, 0.37f),
					RolledAffix(TEXT("Stat_Increased_armor"), 3, 0.81f)};
	FCataclysmItem Ring;
	Ring.Base = FName(TEXT("Ring_Band"));
	Ring.Affixes = {RolledAffix(TEXT("Stat_Flat_maximum_health"), 1, 0.12f)};
	for (const FCataclysmItem& Item : {DoubleTestGreataxe({FName(TEXT("Demonic"))}), Helm, Ring})
	{
		FCataclysmItem First;
		FCataclysmItem Second;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		Equipment->Equip(Item, First, Second, Went);
	}
	UAbilitySystemComponent* PlayerAbilities = Scene.Character->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("set-up: the player has an ability system"), PlayerAbilities))
	{
		return false;
	}
	Equipment->RefreshAttributes(PlayerAbilities);

	const FCataclysmPlayerBuild Build = FCataclysmPlayerBuild::Of(*Scene.Character);
	TestEqual(TEXT("set-up: the build is at the player's level"), Build.Level, 37);
	TestEqual(TEXT("set-up: and wears the three items"), Build.Worn.Num(), 3);
	TestEqual(TEXT("set-up: and holds the thirteen passive points"), Build.PassiveAllocation.Total(), 13);
	TestEqual(TEXT("set-up: and the attribute points"), Build.SpentAttributePoints.Total(), 16);

	// CONTROLS: what the Stalker would have done differs from the player's own. Its level is the tier's reference
	// character's, and its health carries the tier's multiplier.
	TestNotEqual(TEXT("CONTROL: tier 1's reference level is not the player's"),
				 UCataclysmPowerScore::ReferenceCharacter(1).Level, 37);
	TestEqual(TEXT("CONTROL: a tier-1 Corrupted Stalker carries the 0.49 health multiplier"),
			  UCataclysmDungeonModifierEffects::CorruptedStalkerHealthMultiplier(1), 0.49f, 0.0001f);

	ACataclysmConsumptionDoubleCharacter* Double = SpawnDouble(Scene.World, FVector(1000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a double was spawned"), Double)
		|| !TestTrue(TEXT("set-up: it wears the player's build"), Double->WearThePlayersBuild(Build)))
	{
		return false;
	}
	TestTrue(TEXT("it holds a player's build"), Double->bHoldsAPlayerBuild);

	const float PlayerHealth = DoubleTestRead(Scene.Character, Vital::GetMaxHealthAttribute());
	TestTrue(FString::Printf(TEXT("set-up: the player has a maximum health: %.2f"), PlayerHealth), PlayerHealth > 0.0f);
	TestEqual(TEXT("the double's maximum health is the player's"),
			  DoubleTestRead(Double, Vital::GetMaxHealthAttribute()), PlayerHealth, 0.01f);
	TestEqual(TEXT("the double's maximum mana is the player's"),
			  DoubleTestRead(Double, Vital::GetMaxManaAttribute()),
			  DoubleTestRead(Scene.Character, Vital::GetMaxManaAttribute()), 0.01f);
	TestEqual(TEXT("the double's armour is the player's"), DoubleTestRead(Double, Combat::GetArmorAttribute()),
			  DoubleTestRead(Scene.Character, Combat::GetArmorAttribute()), 0.01f);
	const float PlayerDamage = DoubleTestRead(Scene.Character, Combat::GetAttackDamageAttribute());
	TestTrue(FString::Printf(TEXT("set-up: the player has an attack damage: %.2f"), PlayerDamage), PlayerDamage > 0.0f);
	TestEqual(TEXT("the double's attack damage is the player's"),
			  DoubleTestRead(Double, Combat::GetAttackDamageAttribute()), PlayerDamage, 0.01f);
	TestEqual(TEXT("it starts at full health"), DoubleTestRead(Double, Vital::GetHealthAttribute()),
			  DoubleTestRead(Double, Vital::GetMaxHealthAttribute()), 0.01f);
	TestEqual(TEXT("and with the Stalker's starting Fervour"),
			  DoubleTestRead(Double, UCataclysmClassResourceAttributeSet::GetClassResourceAttribute()),
			  UCataclysmDungeonModifierEffects::CorruptedStalkerStartingFervour, 0.01f);

	// NO RE-ROLL: every worn piece is the player's item, its rolls, tiers and gear level as they were.
	const TArray<FCataclysmItem>& Pieces = Double->WornPieces();
	if (!TestEqual(TEXT("it wears as many pieces as the player"), Pieces.Num(), Build.Worn.Num()))
	{
		return false;
	}
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		const FCataclysmItem* Players = Equipment->EquippedAt(Build.Worn[Index].Slot);
		if (!TestNotNull(FString::Printf(TEXT("piece %d: the player wears one in its slot"), Index), Players))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("piece %d: the same base"), Index), Pieces[Index].Base, Players->Base);
		TestEqual(FString::Printf(TEXT("piece %d: the same gear level"), Index), Pieces[Index].GearLevel,
				  Players->GearLevel);
		if (!TestEqual(FString::Printf(TEXT("piece %d: as many affixes"), Index), Pieces[Index].Affixes.Num(),
					   Players->Affixes.Num()))
		{
			continue;
		}
		for (int32 Each = 0; Each < Pieces[Index].Affixes.Num(); ++Each)
		{
			TestEqual(FString::Printf(TEXT("piece %d affix %d: the player's tier"), Index, Each),
					  Pieces[Index].Affixes[Each].Tier, Players->Affixes[Each].Tier);
			TestEqual(FString::Printf(TEXT("piece %d affix %d: the player's roll"), Index, Each),
					  Pieces[Index].Affixes[Each].Roll, Players->Affixes[Each].Roll);
		}
	}
	return true;
}

// O2: NONE OF ITS GEAR DROPS, AND IT PAYS AS A BOSS. With one stream, the Stalker wearing eighteen pieces drops at
// least one; the double wearing the same eighteen drops none.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleDropsTest,
	"Cataclysm.ConsumptionDouble.ItDropsNoGearAndPaysABossRung",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleDropsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmConsumptionDoubleTest;
	constexpr int32 Seed = 11;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const FCataclysmStalkerEntry Entry = DoubleTestDemonicEntry();
	if (!TestTrue(TEXT("set-up: the Demonic entry is authored"), Entry.IsAuthored()))
	{
		return false;
	}
	FRandomStream Rolls(Seed);
	const TArray<FCataclysmItem> Items = ACataclysmCorruptedStalkerCharacter::PiecesAt(Entry, 1, Rolls);
	TArray<FCataclysmWornItem> Worn;
	for (int32 Index = 0; Index < Items.Num() && Index < Entry.Pieces.Num(); ++Index)
	{
		Worn.Add(WornIn(Entry.Pieces[Index].Slot, Items[Index]));
	}
	FRandomStream Count(Seed);
	if (!TestTrue(TEXT("set-up: the stream drops at least one of eighteen pieces"),
				  ACataclysmCorruptedStalkerCharacter::WornPiecesThatDrop(Worn.Num(), Count).Num() >= 1))
	{
		return false;
	}

	ACataclysmConsumptionDoubleCharacter* Double = SpawnDouble(World, FVector(0.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a double was spawned"), Double)
		|| !TestTrue(TEXT("set-up: it wears the build"), Double->WearThePlayersBuild(HandMadeBuild(Worn, {}))))
	{
		return false;
	}
	TestEqual(TEXT("set-up: it wears eighteen pieces"), Double->WornPieces().Num(), 18);

	// CONTROL: THE STALKER, ON THE SAME STREAM, DROPS WHAT IT WORE.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACataclysmCorruptedStalkerCharacter* Stalker = World->SpawnActor<ACataclysmCorruptedStalkerCharacter>(
		ACataclysmCorruptedStalkerCharacter::StaticClass(), FVector(3000.0f, 0.0f, 0.0f), FRotator::ZeroRotator, Params);
	FRandomStream StalkerRolls(Seed);
	if (!TestNotNull(TEXT("set-up: a Stalker was spawned"), Stalker)
		|| !TestTrue(TEXT("set-up: it wears the Demonic entry"), Stalker->WearTheBuild(Entry, 1, StalkerRolls)))
	{
		return false;
	}
	const int32 BeforeStalker = DropsOnTheFloor(World);
	FRandomStream StalkerDrops(Seed);
	Stalker->DropWhatItWore(World, Stalker->GetActorLocation(), StalkerDrops);
	TestTrue(TEXT("CONTROL: the Stalker drops at least one piece it wore"), DropsOnTheFloor(World) - BeforeStalker >= 1);

	const int32 BeforeDouble = DropsOnTheFloor(World);
	FRandomStream DoubleDrops(Seed);
	Double->DropWhatItWore(World, Double->GetActorLocation(), DoubleDrops);
	TestEqual(TEXT("the double drops none of the gear it wore"), DropsOnTheFloor(World) - BeforeDouble, 0);

	// AND ITS DEATH PAYS THE BOSS RUNG'S EXPERIENCE AND MATERIALS, WITH NO RANDOM GEAR, as the Stalker's does.
	TestEqual(TEXT("it pays as a Boss"), Double->RarityStepItPaysAs(), ACataclysmEnemyCharacter::FirstBossRarityStep);
	TestEqual(TEXT("with none of the drop row's random gear"), Double->RandomGearDropsGiven(), 0);
	return true;
}

// NO WAR WORK: a held skill whose row has no shape is never offered by the double's brain. A Greataxe carrying Demonic
// and War, with War's Savage Leap chosen for the movement key: its row has no shape.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleNoShapeTest,
	"Cataclysm.ConsumptionDouble.ItsBrainSkipsAHeldSkillWithNoShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleNoShapeTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmConsumptionDoubleTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a test world was created"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const int32 MovementIndex =
		ACataclysmCorruptedStalkerCharacter::ChoiceOrder().IndexOfByKey(ECataclysmAbilitySlot::Movement);
	if (!TestTrue(TEXT("set-up: the movement key is in the choice order"), MovementIndex != INDEX_NONE))
	{
		return false;
	}
	const TArray<FCataclysmWornItem> Worn = {
		WornIn(ECataclysmGearSlot::Weapon1, DoubleTestGreataxe({FName(TEXT("Demonic")), FName(TEXT("War"))}))};

	// CONTROL: WITH NO CHOICE, THE MOVEMENT KEY HOLDS DEMONIC'S INFERNAL PLUNGE, WHICH HAS A SHAPE, AND IS OFFERED.
	ACataclysmConsumptionDoubleCharacter* Plain = SpawnDouble(World, FVector(0.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a double was spawned"), Plain)
		|| !TestTrue(TEXT("set-up: it wears the build"), Plain->WearThePlayersBuild(HandMadeBuild(Worn, {}))))
	{
		return false;
	}
	const FCataclysmWeaponSkill* PlainMovement = Plain->SkillIn(ECataclysmAbilitySlot::Movement);
	if (!TestNotNull(TEXT("CONTROL: it holds a movement skill"), PlainMovement))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: Demonic's"), PlainMovement->RowName, FName(TEXT("Demonic_Greataxe_Movement")));
	const FCataclysmEnemyAbility PlainChoice = Plain->EnemyAbilities()[MovementIndex];
	TestTrue(FString::Printf(TEXT("CONTROL: it is offered: %.0f to %.0f cm"), PlainChoice.MinRangeCm,
							 PlainChoice.MaxRangeCm),
			 PlainChoice.MaxRangeCm >= PlainChoice.MinRangeCm);

	// WAR'S SAVAGE LEAP ON THE MOVEMENT KEY: held, with no shape, and never offered.
	FCataclysmSkillChoice Choice;
	Choice.Slot = ECataclysmAbilitySlot::Movement;
	Choice.SkillRow = FName(TEXT("War_Greataxe_Movement"));
	ACataclysmConsumptionDoubleCharacter* Double = SpawnDouble(World, FVector(2000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a second double was spawned"), Double)
		|| !TestTrue(TEXT("set-up: it wears the build"), Double->WearThePlayersBuild(HandMadeBuild(Worn, {Choice}))))
	{
		return false;
	}
	const FCataclysmWeaponSkill* Held = Double->SkillIn(ECataclysmAbilitySlot::Movement);
	if (!TestNotNull(TEXT("set-up: it holds a movement skill"), Held))
	{
		return false;
	}
	TestEqual(TEXT("set-up: the chosen War row"), Held->RowName, FName(TEXT("War_Greataxe_Movement")));
	TestTrue(TEXT("set-up: whose row has no shape"), Held->Shape == ECataclysmSkillShape::None);
	const FCataclysmEnemyAbility Skipped = Double->EnemyAbilities()[MovementIndex];
	TestTrue(FString::Printf(TEXT("the skill with no shape is offered nowhere: %.0f to %.0f cm"), Skipped.MinRangeCm,
							 Skipped.MaxRangeCm),
			 Skipped.MaxRangeCm < Skipped.MinRangeCm);
	return true;
}

// ===========================================================================
// Slices 2b and 2c: the capture, the save, the arrival, the hunt and the kill.
// ===========================================================================

/**
 * WHAT THESE COVER: the build captured at the crossing and saved (O4); the character and run records' migrations; the
 * double arriving on the next floor entered and never on the floor of the crossing (R2), following thirty seconds into
 * the floor after with its health (J1), and coming on the next dungeon's first floor after leaving one (R2); a double
 * and a Corrupted Stalker each with its own hunt; killing the double clearing the worn residue, the mark and the
 * build (O1), and the player killed by it keeping the mark (O3); the double wearing the captured build and not the gear
 * worn when it arrives; and the hunt across a save and load.
 *
 * THE DUNGEON IS A GAME MODE BEGUN IN A TEST WORLD, as the Corrupted Stalker's tests make it, with a possessed player
 * and a game instance. Its `StartPlay` begins the save writer, so each test deletes the two slots it wrote.
 */
namespace CataclysmDoubleHuntTest
{
	using namespace CataclysmConsumptionDoubleTest;

	static constexpr int32 UserIndex = UCataclysmSaveWriter::UserIndex;

	/** A test world with a possessed player, its state, and (when asked) a dungeon game mode begun on floor 1. */
	struct FHuntScene
	{
		UWorld* World = nullptr;
		ACataclysmPlayerState* State = nullptr;
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmDungeonGameMode* Mode = nullptr;
		UCataclysmSaveWriter* Writer = nullptr;

		bool IsUsable() const
		{
			return World && State && Character && Character->GetInventory() && Character->GetEquipment()
				&& Character->GetAbilitySystemComponent();
		}
	};

	FHuntScene MakeHuntScene(bool bWithADungeon)
	{
		FHuntScene Out;
		Out.World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
		if (!Out.World)
		{
			return Out;
		}
		Out.World->SetGameInstance(NewObject<UCataclysmGameInstance>());
		Out.State = Out.World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = Out.World->SpawnActor<APlayerController>();
		Out.Character = Out.World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Out.State || !Controller || !Out.Character)
		{
			return Out;
		}
		Controller->SetPlayerState(Out.State);
		Controller->Possess(Out.Character);
		Out.Writer = UCataclysmSaveWriter::In(Out.World);
		if (bWithADungeon)
		{
			Out.Mode = Out.World->SpawnActor<ACataclysmDungeonGameMode>();
			if (Out.Mode)
			{
				Out.Mode->StartPlay();
				Out.Mode->ImpRarityStep = 0;
				Out.Mode->DungeonModifiers = {};
			}
		}
		else if (Out.Writer)
		{
			Out.Writer->BeginRun(FGuid::NewGuid(), FGuid::NewGuid(), FName(TEXT("Sandbox")), 1);
		}
		return Out;
	}

	/** Waits for every write to a slot to finish, then deletes it; a test leaves no save behind. */
	void DeleteSlot(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
		{
			UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
		}
	}

	/** The character record in a slot, once every write to it has finished; null when there is none. */
	UCataclysmCharacterSave* ReadCharacterRecord(const FString& SlotName)
	{
		const double GiveUpAt = FPlatformTime::Seconds() + 2.0;
		while (FCataclysmSaveStorage::HasAWriteInFlight(SlotName) && FPlatformTime::Seconds() < GiveUpAt)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::SlotIsEmpty;
		FString Message;
		return Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::ReadFromSlot(
			SlotName, UserIndex, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	}

	FCataclysmItem WithResidue(const TCHAR* Base, float Residue)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.Residue = Residue;
		return Item;
	}

	/**
	 * The crossing, as the player makes it: a helm of 1000 worn and boots of 1000 carried, the boots put on with a yes.
	 * Past tier 1's 1950 once both are on (the starting Greataxe carries under 950). True when the boots went on and
	 * the character is marked.
	 */
	bool CrossTheThreshold(FHuntScene& Scene)
	{
		FCataclysmItem First;
		FCataclysmItem Second;
		ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
		Scene.Character->GetEquipment()->Equip(WithResidue(TEXT("Head_Helm"), 1000.0f), First, Second, Went);
		const FCataclysmItem Boots = WithResidue(TEXT("Boots_Sabatons"), 1000.0f);
		const int32 Slot = Scene.Character->GetInventory()->AddItem(Boots);
		const ECataclysmWearResult Yes = Scene.Character->WearPastTheThreshold(Slot, &Boots, Went);
		return Yes == ECataclysmWearResult::Worn && Scene.State->IsMarkedByConsumption();
	}

	void Beat(ACataclysmDungeonGameMode* Mode, int32 Times)
	{
		for (int32 Index = 0; Index < Times; ++Index)
		{
			Mode->Tick(ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
		}
	}

	int32 BeatsFor(float Seconds)
	{
		return FMath::CeilToInt(Seconds / ACataclysmDungeonGameMode::SecondsBetweenWaveChecks);
	}

	/** Go to a floor and clear its own creatures, so nothing but the hunters stands. */
	bool EnterFloor(FAutomationTestBase& Test, ACataclysmDungeonGameMode* Mode, int32 Floor)
	{
		if (!Test.TestTrue(FString::Printf(TEXT("set-up: floor %d was reached"), Floor), Mode->GoToFloor(Floor)))
		{
			return false;
		}
		Mode->ClearFloorEnemies();
		return true;
	}

	int32 DoublesStanding(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACataclysmConsumptionDoubleCharacter> It(World); It; ++It)
		{
			Count += IsValid(*It) && !UCataclysmSkillEffects::IsDead(*It) ? 1 : 0;
		}
		return Count;
	}

	float HealthShareOf(const AActor* Actor)
	{
		return DoubleTestRead(Actor, UCataclysmVitalAttributeSet::GetHealthAttribute())
			/ FMath::Max(DoubleTestRead(Actor, UCataclysmVitalAttributeSet::GetMaxHealthAttribute()), 1.0f);
	}

	void SetHealthShare(AActor* Actor, float Share)
	{
		if (UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Actor))
		{
			Abilities->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(),
				Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) * Share);
		}
	}

	/** The panel's line for the double, as the panel would draw it, or empty when the panel lists none. */
	FString DoublePanelLine(const ACataclysmDungeonGameMode& Mode)
	{
		using Layout = UCataclysmFloorModifierPanelLayout;
		const TArray<FCataclysmFloorModifierLine> Lines = Layout::LinesFor(
			Mode.RowsTheFloorPanelLists(), UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
			Mode.LiveCountsForTheFloor());
		for (const FCataclysmFloorModifierLine& Line : Lines)
		{
			if (Line.bIsTheConsumptionDouble)
			{
				return Layout::NameLineFor(Line);
			}
		}
		return FString();
	}
}

// ---------------------------------------------------------------------------
// The capture, and the character record
// ---------------------------------------------------------------------------

/**
 * The confirmed crossing captures the character's build after the equip, so it holds the item that crossed; the
 * character record the crossing wrote holds it; and a fresh character loaded from that record holds it. The owner's
 * decision of 2026-11-02 (O4). Prints "DOUBLE BUILD CAPTURED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleCaptureTest,
	"Cataclysm.ConsumptionDouble.TheCrossingCapturesTheBuildAndTheCharacterRecordKeepsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleCaptureTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/false);
	if (!TestTrue(TEXT("set-up: a possessed player with its state and a writer"), Scene.IsUsable() && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };

	TestFalse(TEXT("CONTROL: before the crossing no build is held"), Scene.State->HoldsConsumptionBuild());
	if (!TestTrue(TEXT("set-up: the crossing puts the boots on and marks"), CrossTheThreshold(Scene)))
	{
		return false;
	}
	const FCataclysmPlayerBuild& Held = Scene.State->ConsumptionBuild();
	int32 BootsHeld = 0;
	for (const FCataclysmWornItem& Entry : Held.Worn)
	{
		BootsHeld += Entry.Item.Base == FName(TEXT("Boots_Sabatons")) && Entry.Item.Residue == 1000.0f ? 1 : 0;
	}
	const int32 WornNow = Scene.Character->GetEquipment()->NumEquipped();

	const UCataclysmCharacterSave* Record = ReadCharacterRecord(CharacterSlot);
	const int32 RecordWorn = Record ? Record->ConsumptionBuild.Worn.Num() : -1;

	FHuntScene Fresh = MakeHuntScene(/*bWithADungeon=*/false);
	ON_SCOPE_EXIT
	{
		if (Fresh.World)
		{
			if (Fresh.Writer)
			{
				DeleteSlot(Fresh.Writer->RunSlotName());
				DeleteSlot(Fresh.Writer->CharacterSlotName());
			}
			Fresh.World->DestroyWorld(false);
		}
	};
	int32 Refused = 0;
	FString Why;
	const bool bApplied = Record && Fresh.IsUsable()
		&& FCataclysmSaveApply::CharacterInto(*Record, *Fresh.Character, Refused, Why);

	AddInfo(FString::Printf(TEXT("DOUBLE BUILD CAPTURED: held %d items (worn now %d, the boots among them %d), level %d; "
								 "the record %s at version %d with %d items; loaded into a fresh character %s, holding %d"),
							Held.Worn.Num(), WornNow, BootsHeld, Held.Level, Record ? TEXT("read") : TEXT("NOT READ"),
							Record ? Record->SchemaVersion : -1, RecordWorn, bApplied ? TEXT("yes") : TEXT("NO"),
							Fresh.State ? Fresh.State->ConsumptionBuild().Worn.Num() : -1));

	TestTrue(TEXT("the crossing captured a build"), Scene.State->HoldsConsumptionBuild());
	TestEqual(TEXT("holding every item worn after the equip"), Held.Worn.Num(), WornNow);
	TestEqual(TEXT("the boots that crossed among them"), BootsHeld, 1);
	if (!TestNotNull(TEXT("set-up: the crossing wrote the character record"), Record))
	{
		return false;
	}
	TestTrue(TEXT("the record holds the mark"), Record->bMarkedByConsumption);
	TestEqual(TEXT("the record holds the build"), RecordWorn, WornNow);
	TestTrue(TEXT("set-up: a fresh character took the record"), bApplied);
	TestEqual(TEXT("and a fresh character loaded from it holds the build"),
			  Fresh.State ? Fresh.State->ConsumptionBuild().Worn.Num() : -1, WornNow);
	return true;
}

/**
 * A character file written at version 5, before the build was saved, migrates to version 6 holding an empty build,
 * and every other field is kept. Prints "BUILD MIGRATED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleCharacterMigrationTest,
	"Cataclysm.ConsumptionDouble.ACharacterFileFromBeforeTheBuildMigratesWithNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleCharacterMigrationTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Character_v5.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Character_v5.json is a JSON object"), Stepped.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("set-up: the version 5 file has no build key"), Stepped->HasField(TEXT("ConsumptionBuild")));
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmCharacterSave>()->MigrationSteps(), UCataclysmCharacterSave::SchemaVersionNow,
		Stepped.ToSharedRef(), StepMessage);
	const TSharedPtr<FJsonObject>* SteppedBuild = nullptr;
	const bool bWritten = Stepped->TryGetObjectField(TEXT("ConsumptionBuild"), SteppedBuild);
	FString BuildWhere;
	const bool bWrittenEmpty = bWritten && SteppedBuild
		&& CataclysmSaveFixtures::SameObject(CataclysmSaveFixtures::EmptyPlayerBuild(), *SteppedBuild, BuildWhere);

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Character_v5.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmCharacterSave::SchemaVersionNow);
	Expected->SetObjectField(TEXT("ConsumptionBuild"), CataclysmSaveFixtures::EmptyPlayerBuild());
	Expected->SetNumberField(TEXT("CataclysmOrderSeed"), CataclysmSaveFixtures::FixtureIdentifiersOrderSeed); // #1324: 7.
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("BUILD MIGRATED: the step %d (%s), wrote the build %s, empty %s; the record at version %d, "
								 "marked %s, holding %d items; the rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWritten ? TEXT("yes") : TEXT("NO"),
							bWrittenEmpty ? TEXT("yes") : *BuildWhere, Read->SchemaVersion,
							Read->bMarkedByConsumption ? TEXT("yes") : TEXT("no"), Read->ConsumptionBuild.Worn.Num(),
							bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 5 file"), static_cast<int32>(Stepping),
		static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes an empty build into the file"), bWrittenEmpty);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmCharacterSave::SchemaVersionNow);
	TestEqual(TEXT("a character from before the build holds none"), Read->ConsumptionBuild.Worn.Num(), 0);
	TestTrue(TEXT("CONTROL: and is still marked, as the version 5 file says"), Read->bMarkedByConsumption);
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

/**
 * A run file written at version 1, before the double's hunt was saved, migrates to version 2 with no hunt, and every
 * other field is kept. Prints "HUNT MIGRATED". Since 2026-11-02 the chain runs on to version 3, which adds an empty
 * death (issue #41), and the expected object carries it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleRunMigrationTest,
	"Cataclysm.ConsumptionDouble.ARunFileFromBeforeTheHuntMigratesWithNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleRunMigrationTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Reason;
	if (!CataclysmSaveFixtures::Read(TEXT("Run_v1.json"), Text, Reason))
	{
		AddError(Reason);
		return false;
	}
	const TSharedPtr<FJsonObject> Stepped = CataclysmSaveFixtures::Parse(Text);
	if (!TestTrue(TEXT("set-up: Run_v1.json is a JSON object"), Stepped.IsValid()))
	{
		return false;
	}
	FString StepMessage;
	const ECataclysmSaveMigrationResult Stepping = FCataclysmSaveMigration::Migrate(
		GetDefault<UCataclysmRunSave>()->MigrationSteps(), UCataclysmRunSave::SchemaVersionNow, Stepped.ToSharedRef(),
		StepMessage);
	const TSharedPtr<FJsonObject> Walk = Stepped->GetObjectField(TEXT("Walk"));
	const TSharedPtr<FJsonObject> RuleState = Stepped->GetObjectField(TEXT("Floor"))->GetObjectField(TEXT("RuleState"));
	bool bHunted = true;
	double Share = -1.0;
	const TArray<TSharedPtr<FJsonValue>>* Charges = nullptr;
	bool bHunts = true;
	bool bStanding = true;
	const TSharedPtr<FJsonObject>* Location = nullptr;
	const bool bWroteHunted = Walk->TryGetBoolField(TEXT("bConsumptionDoubleHasHunted"), bHunted) && !bHunted;
	const bool bWroteShare = Walk->TryGetNumberField(TEXT("ConsumptionDoubleHealthShare"), Share) && Share == 1.0;
	const bool bWroteCharges = Walk->TryGetArrayField(TEXT("ConsumptionDoublePotionCharges"), Charges) && Charges
		&& Charges->IsEmpty();
	const bool bWroteHunts = RuleState->TryGetBoolField(TEXT("bConsumptionDoubleHuntsThisFloor"), bHunts) && !bHunts;
	const bool bWroteStanding = RuleState->TryGetBoolField(TEXT("bConsumptionDoubleStanding"), bStanding) && !bStanding;
	const bool bWroteLocation = RuleState->TryGetObjectField(TEXT("ConsumptionDoubleLocation"), Location) && Location;

	ECataclysmSaveLoadResult Result = ECataclysmSaveLoadResult::NotValidJson;
	FString Message;
	UCataclysmRunSave* Read = Cast<UCataclysmRunSave>(FCataclysmSaveStorage::FromJson(
		Text, UCataclysmRunSave::StaticClass(), GetTransientPackage(), Result, Message));
	if (Read == nullptr)
	{
		AddError(FString::Printf(TEXT("Run_v1.json would not load: %s -- %s"),
			FCataclysmSaveStorage::Describe(Result), *Message));
		return false;
	}

	// AND NOTHING ELSE MOVED: the version 1 file with the version and the six keys, compared with what the record
	// writes back.
	const TSharedPtr<FJsonObject> Expected = CataclysmSaveFixtures::Parse(Text);
	Expected->SetNumberField(TEXT("SchemaVersion"), UCataclysmRunSave::SchemaVersionNow);
	const TSharedPtr<FJsonObject> ExpectedWalk = Expected->GetObjectField(TEXT("Walk"));
	ExpectedWalk->SetBoolField(TEXT("bConsumptionDoubleHasHunted"), false);
	ExpectedWalk->SetNumberField(TEXT("ConsumptionDoubleHealthShare"), 1.0);
	ExpectedWalk->SetArrayField(TEXT("ConsumptionDoublePotionCharges"), TArray<TSharedPtr<FJsonValue>>());
	const TSharedPtr<FJsonObject> ExpectedRules =
		Expected->GetObjectField(TEXT("Floor"))->GetObjectField(TEXT("RuleState"));
	ExpectedRules->SetBoolField(TEXT("bConsumptionDoubleHuntsThisFloor"), false);
	ExpectedRules->SetBoolField(TEXT("bConsumptionDoubleStanding"), false);
	const TSharedRef<FJsonObject> Origin = MakeShared<FJsonObject>();
	Origin->SetNumberField(TEXT("X"), 0.0);
	Origin->SetNumberField(TEXT("Y"), 0.0);
	Origin->SetNumberField(TEXT("Z"), 0.0);
	ExpectedRules->SetObjectField(TEXT("ConsumptionDoubleLocation"), Origin);
	// AND THE EMPTY DEATH `Migrate_2_to_3` WRITES, because the chain runs on to version 3 since 2026-11-02 (issue #41).
	Expected->SetNumberField(TEXT("LastDeathDay"), 0);
	Expected->SetStringField(TEXT("LastDeathDungeonName"), FString());
	Expected->SetNumberField(TEXT("LastDeathWalkDays"), 0);
	Expected->SetNumberField(TEXT("LastDeathDaysCost"), 0);
	Expected->SetArrayField(TEXT("LastDeathFallenCityIds"), TArray<TSharedPtr<FJsonValue>>());
	// AND THE UNOPENED CATACLYSM BOSS DUNGEON `Migrate_3_to_4` WRITES, because the chain runs on to version 4 since
	// 2026-11-02 (issue #1324 slice 6b).
	Expected->SetBoolField(TEXT("bCataclysmDungeonOpened"), false);
	Expected->SetNumberField(TEXT("CataclysmDungeonOpenedDay"), 0);
	Expected->SetNumberField(TEXT("CataclysmDungeonBaseFloors"), 0);
	// AND THE "NOT WON" `Migrate_4_to_5` WRITES, because the chain runs on to version 5 since 2026-11-03 (issue #1324
	// slice 6e).
	Expected->SetNumberField(TEXT("LastRunWonDay"), 0);
	Expected->SetNumberField(TEXT("LastRunWonTier"), 0);
	Expected->SetStringField(TEXT("LastRunWonCataclysm"), TEXT("None"));
	Expected->SetNumberField(TEXT("LastRunWonPassivePoints"), 0);
	FString Rewritten;
	FString Error;
	const bool bWroteBack = FCataclysmSaveStorage::ToJson(Read, Rewritten, Error);
	FString Where;
	const bool bSame = bWroteBack
		&& CataclysmSaveFixtures::SameObject(Expected, CataclysmSaveFixtures::Parse(Rewritten), Where);

	AddInfo(FString::Printf(TEXT("HUNT MIGRATED: the step %d (%s); wrote hunted %s, share %s, charges %s, hunts %s, "
								 "standing %s, location %s; the record at version %d, hunted %s, share %.2f, hunts %s; the "
								 "rest %s"),
							static_cast<int32>(Stepping), *StepMessage, bWroteHunted ? TEXT("yes") : TEXT("NO"),
							bWroteShare ? TEXT("yes") : TEXT("NO"), bWroteCharges ? TEXT("yes") : TEXT("NO"),
							bWroteHunts ? TEXT("yes") : TEXT("NO"), bWroteStanding ? TEXT("yes") : TEXT("NO"),
							bWroteLocation ? TEXT("yes") : TEXT("NO"), Read->SchemaVersion,
							Read->Walk.bConsumptionDoubleHasHunted ? TEXT("yes") : TEXT("no"),
							Read->Walk.ConsumptionDoubleHealthShare,
							Read->Floor.RuleState.bConsumptionDoubleHuntsThisFloor ? TEXT("yes") : TEXT("no"),
							bSame ? TEXT("kept") : *Where));

	TestEqual(TEXT("the chain migrates the version 1 file"), static_cast<int32>(Stepping),
		static_cast<int32>(ECataclysmSaveMigrationResult::Migrated));
	TestTrue(TEXT("the step writes 'has not hunted' into the walk"), bWroteHunted);
	TestTrue(TEXT("the step writes a whole health share into the walk"), bWroteShare);
	TestTrue(TEXT("the step writes no potion charges into the walk"), bWroteCharges);
	TestTrue(TEXT("the step writes 'does not hunt this floor' into the floor's rule state"), bWroteHunts);
	TestTrue(TEXT("the step writes 'not standing' into the floor's rule state"), bWroteStanding);
	TestTrue(TEXT("the step writes a location into the floor's rule state"), bWroteLocation);
	TestEqual(TEXT("it arrives at the current version"), Read->SchemaVersion, UCataclysmRunSave::SchemaVersionNow);
	TestTrue(TEXT("and every other field is kept"), bSame);
	return true;
}

// ---------------------------------------------------------------------------
// The arrival
// ---------------------------------------------------------------------------

/**
 * A crossing made on a floor brings the double on the next floor entered, a minute in, at the cell farthest from the
 * player, wearing the build captured at the crossing; never on the floor the crossing was made on (R2, ruled
 * 2026-11-02). The panel says when it comes and that it is here. Prints "DOUBLE ARRIVES".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleArrivesTest,
	"Cataclysm.ConsumptionDouble.TheDoubleComesOnTheNextFloorEnteredAndNeverOnTheFloorOfTheCrossing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleArrivesTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;

	// THE CROSSING, ON FLOOR 2, ENTERED UNMARKED.
	if (!EnterFloor(*this, Mode, 2) || !TestTrue(TEXT("set-up: the crossing marks"), CrossTheThreshold(Scene)))
	{
		return false;
	}
	const int32 CapturedItems = Scene.State->ConsumptionBuild().Worn.Num();
	TestFalse(TEXT("CONTROL: the floor of the crossing was entered unmarked"), Mode->ConsumptionDoubleHuntsThisFloor());
	Beat(Mode, BeatsFor(70.0f));
	if (!TestNull(TEXT("no double on the floor the crossing was made on, seventy seconds in"),
				  Mode->ConsumptionDoubleOnTheFloor()))
	{
		return false;
	}

	// THE NEXT FLOOR ENTERED.
	if (!EnterFloor(*this, Mode, 3))
	{
		return false;
	}
	const FString PanelBefore = DoublePanelLine(*Mode);
	const FIntPoint Farthest = ACataclysmDungeonGameMode::FarthestWalkableCellFrom(
		Mode->CurrentFloor->GetPlan(), Mode->CurrentFloor->CellOfWorld(Scene.Character->GetActorLocation()));
	TestTrue(TEXT("the next floor entered is hunted"), Mode->ConsumptionDoubleHuntsThisFloor());
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds) - 1);
	if (!TestNull(TEXT("no double a beat before a minute"), Mode->ConsumptionDoubleOnTheFloor()))
	{
		return false;
	}
	Beat(Mode, 1);
	ACataclysmConsumptionDoubleCharacter* Double = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("the double came a minute into the next floor"), Double))
	{
		return false;
	}
	const FString PanelAfter = DoublePanelLine(*Mode);
	AddInfo(FString::Printf(TEXT("DOUBLE ARRIVES: captured %d items; the double wears %d at level %d; %.0f cm from the "
								 "farthest cell, %.0f from the entrance; panel before '%s', after '%s'"),
							CapturedItems, Double->WornBuild().Worn.Num(), Double->WornBuild().Level,
							FVector::Dist2D(Double->GetActorLocation(), Mode->CurrentFloor->WorldOfCell(Farthest)),
							FVector::Dist2D(Double->GetActorLocation(), Mode->CurrentFloor->EntranceWorld()),
							*PanelBefore, *PanelAfter));

	TestEqual(TEXT("the panel said when it comes"), PanelBefore,
			  FString(TEXT("Hunted by your corrupted double (it comes 60 seconds into the floor)")));
	TestTrue(TEXT("wearing the build captured at the crossing"), Double->WornBuild().Worn.Num() == CapturedItems
		&& CapturedItems > 0);
	TestTrue(TEXT("holding a player's build"), Double->bHoldsAPlayerBuild);
	TestTrue(TEXT("raised by a rule"), Double->bRaisedByARule);
	TestTrue(TEXT("on the monsters' side"),
			 Double->GetGenericTeamId() == UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	TestEqual(TEXT("noticing the player from across the floor"), Double->SightRadiusMultiplier,
			  Effects::CorruptedStalkerSightMultiplier);
	TestTrue(TEXT("at the walkable cell farthest from the player, within one cell of it"),
			 FVector::Dist2D(Double->GetActorLocation(), Mode->CurrentFloor->WorldOfCell(Farthest))
				 < FCataclysmFloorGenerator::CellSizeCm);
	TestTrue(TEXT("and the panel says it is here"), PanelAfter.StartsWith(TEXT("Hunted by your corrupted double (it is "
																			   "on this floor")));

	// ONCE: another minute raises no second one.
	Beat(Mode, BeatsFor(60.0f));
	TestTrue(TEXT("a minute later it is the same double"), Mode->ConsumptionDoubleOnTheFloor() == Double);
	TestEqual(TEXT("and only one stands"), DoublesStanding(Scene.World), 1);
	return true;
}

/**
 * On the floor after the first it hunts, the double follows thirty seconds in, at the entrance, with the share of
 * health it had (J1). Prints "DOUBLE FOLLOWS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleFollowsTest,
	"Cataclysm.ConsumptionDouble.TheDoubleFollowsThirtySecondsIntoTheNextFloorWithTheHealthItHad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleFollowsTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;

	if (!TestTrue(TEXT("set-up: the crossing marks"), CrossTheThreshold(Scene)) || !EnterFloor(*this, Mode, 2))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds));
	ACataclysmConsumptionDoubleCharacter* First = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("set-up: the double came on floor 2"), First))
	{
		return false;
	}
	SetHealthShare(First, 0.4f);
	Beat(Mode, 1);

	if (!TestTrue(TEXT("set-up: floor 3 was reached"), Mode->GoToFloor(3)))
	{
		return false;
	}
	TestNull(TEXT("no double as floor 3 begins"), Mode->ConsumptionDoubleOnTheFloor());
	TestFalse(TEXT("the first went with floor 2"), IsValid(First));
	Mode->ClearFloorEnemies();
	const FString Panel = DoublePanelLine(*Mode);
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFollowSeconds) - 1);
	if (!TestNull(TEXT("no double a beat before thirty seconds"), Mode->ConsumptionDoubleOnTheFloor()))
	{
		return false;
	}
	Beat(Mode, 1);
	ACataclysmConsumptionDoubleCharacter* Second = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("it followed at thirty seconds"), Second))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DOUBLE FOLLOWS: panel '%s'; %.0f cm from the entrance; health share %.3f"), *Panel,
							FVector::Dist2D(Second->GetActorLocation(), Mode->CurrentFloor->EntranceWorld()),
							HealthShareOf(Second)));
	TestEqual(TEXT("the panel said when it follows"), Panel,
			  FString(TEXT("Hunted by your corrupted double (it comes 30 seconds into the floor)")));
	TestTrue(TEXT("at the entrance, within one cell of it"),
			 FVector::Dist2D(Second->GetActorLocation(), Mode->CurrentFloor->EntranceWorld())
				 < FCataclysmFloorGenerator::CellSizeCm);
	TestEqual(TEXT("with the share of its health it had"), HealthShareOf(Second), 0.4f, 0.01f);
	return true;
}

/**
 * Leaving a dungeon alive resets the hunt and not the mark; the next dungeon entered marked is hunted from its first
 * floor, a minute in, at the farthest cell, as a first floor hunted (R2: "the doubles return on the next dungeon").
 * The mark here is set between the two dungeons. Prints "DOUBLE NEXT DUNGEON".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleNextDungeonTest,
	"Cataclysm.ConsumptionDouble.ACharacterMarkedBetweenDungeonsIsHuntedOnTheNextDungeonsFirstFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleNextDungeonTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;

	// A DUNGEON WALKED UNMARKED AND LEFT; THE CROSSING BETWEEN DUNGEONS.
	if (!EnterFloor(*this, Mode, 2))
	{
		return false;
	}
	Mode->LeaveEmpireDungeon();
	if (!TestTrue(TEXT("set-up: the crossing between dungeons marks"), CrossTheThreshold(Scene)))
	{
		return false;
	}
	TestFalse(TEXT("CONTROL: nothing hunts between dungeons"), Mode->ConsumptionDoubleHuntsThisFloor());

	// THE NEXT DUNGEON'S FIRST FLOOR, as `EnterEmpireDungeon` enters it.
	if (!EnterFloor(*this, Mode, 1))
	{
		return false;
	}
	TestTrue(TEXT("the next dungeon's first floor is hunted"), Mode->ConsumptionDoubleHuntsThisFloor());
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds));
	ACataclysmConsumptionDoubleCharacter* First = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("the double came a minute into the next dungeon's first floor"), First))
	{
		return false;
	}

	// LEAVING ALIVE: the hunt resets, the mark stays, and the dungeon after is hunted afresh.
	Mode->LeaveEmpireDungeon();
	const bool bMarkedAfterLeaving = Scene.State->IsMarkedByConsumption();
	const bool bHuntedAfterLeaving = Mode->ConsumptionDoubleHasHunted();
	if (!EnterFloor(*this, Mode, 1))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFollowSeconds) + 1);
	const bool bCameAtThirty = Mode->ConsumptionDoubleOnTheFloor() != nullptr;
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds - Effects::CorruptedStalkerFollowSeconds));
	const ACataclysmConsumptionDoubleCharacter* Again = Mode->ConsumptionDoubleOnTheFloor();
	AddInfo(FString::Printf(TEXT("DOUBLE NEXT DUNGEON: first floor double %s; after leaving marked %s, hunted %s; the "
								 "dungeon after: at thirty seconds %s, at a minute %s"),
							First ? TEXT("came") : TEXT("NONE"), bMarkedAfterLeaving ? TEXT("yes") : TEXT("no"),
							bHuntedAfterLeaving ? TEXT("yes") : TEXT("no"), bCameAtThirty ? TEXT("came") : TEXT("none"),
							Again ? TEXT("came") : TEXT("none")));
	TestTrue(TEXT("leaving alive keeps the mark"), bMarkedAfterLeaving);
	TestFalse(TEXT("and resets the hunt"), bHuntedAfterLeaving);
	TestFalse(TEXT("so the dungeon after is not followed into at thirty seconds"), bCameAtThirty);
	TestNotNull(TEXT("but hunted from a minute in"), Again);
	return true;
}

/**
 * A double and a Corrupted Stalker on one floor, each with its own hunt: each arrives, each carries its own share of
 * health to the next floor, and killing the double ends neither the Stalker nor its hunt. Prints "DOUBLE AND STALKER".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleAndStalkerTest,
	"Cataclysm.ConsumptionDouble.ADoubleAndACorruptedStalkerHuntOneFloorEachWithItsOwnState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleAndStalkerTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;

	if (!TestTrue(TEXT("set-up: the crossing marks"), CrossTheThreshold(Scene)))
	{
		return false;
	}
	Mode->DungeonModifiers = {FName(Effects::CorruptedStalkerKey)};
	if (!EnterFloor(*this, Mode, 2))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds));
	ACataclysmConsumptionDoubleCharacter* Double = Mode->ConsumptionDoubleOnTheFloor();
	ACataclysmCorruptedStalkerCharacter* Stalker = Mode->CorruptedStalkerOnTheFloor();
	if (!TestNotNull(TEXT("set-up: the double came"), Double) || !TestNotNull(TEXT("set-up: the Stalker came"), Stalker))
	{
		return false;
	}
	TestTrue(TEXT("they are two creatures"), static_cast<AActor*>(Double) != static_cast<AActor*>(Stalker));
	SetHealthShare(Double, 0.3f);
	SetHealthShare(Stalker, 0.7f);
	Beat(Mode, 1);

	if (!EnterFloor(*this, Mode, 3))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFollowSeconds));
	Double = Mode->ConsumptionDoubleOnTheFloor();
	Stalker = Mode->CorruptedStalkerOnTheFloor();
	if (!TestNotNull(TEXT("the double followed"), Double) || !TestNotNull(TEXT("the Stalker followed"), Stalker))
	{
		return false;
	}
	const float DoubleShare = HealthShareOf(Double);
	const float StalkerShare = HealthShareOf(Stalker);

	UCataclysmSkillEffects::ReduceHealthDirectly(Scene.Character, Double, 100000000.0f);
	const bool bDoubleDied = UCataclysmSkillEffects::IsDead(Double);
	AddInfo(FString::Printf(TEXT("DOUBLE AND STALKER: on floor 3 the double at %.3f, the Stalker at %.3f; the double "
								 "killed %s; the Stalker slain %s, standing %s; marked %s"),
							DoubleShare, StalkerShare, bDoubleDied ? TEXT("yes") : TEXT("NO"),
							Mode->CorruptedStalkerSlainInThisDungeon() ? TEXT("yes") : TEXT("no"),
							UCataclysmSkillEffects::IsDead(Stalker) ? TEXT("no") : TEXT("yes"),
							Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no")));
	TestEqual(TEXT("the double carried its own share"), DoubleShare, 0.3f, 0.01f);
	TestEqual(TEXT("and the Stalker its own"), StalkerShare, 0.7f, 0.01f);
	TestTrue(TEXT("set-up: the double died"), bDoubleDied);
	TestFalse(TEXT("killing the double does not slay the Stalker"), Mode->CorruptedStalkerSlainInThisDungeon());
	TestFalse(TEXT("which still stands"), UCataclysmSkillEffects::IsDead(Stalker));
	TestTrue(TEXT("and is still the Stalker the floor holds"), Mode->CorruptedStalkerOnTheFloor() == Stalker);
	TestFalse(TEXT("while the double's hunt is over"), Mode->ConsumptionDoubleHasHunted());
	return true;
}

// ---------------------------------------------------------------------------
// The kill, and the player's death
// ---------------------------------------------------------------------------

/**
 * Killing the double sets the residue on every worn item to zero and clears the mark and the captured build, and the
 * character record written then says so; an item carried keeps its residue (O1; the design: "residue is set to zero
 * on every equipped item"). Prints "DOUBLE KILLED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleKilledTest,
	"Cataclysm.ConsumptionDouble.KillingTheDoubleZeroesTheWornResidueAndClearsTheMarkAndTheRecordSaysSo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleKilledTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	UCataclysmEquipmentComponent* Equipment = Scene.Character->GetEquipment();
	UCataclysmInventoryComponent* Bag = Scene.Character->GetInventory();

	if (!TestTrue(TEXT("set-up: the crossing marks"), CrossTheThreshold(Scene)))
	{
		return false;
	}
	const int32 CarriedSlot = Bag->AddItem(WithResidue(TEXT("Ring_Band"), 300.0f));
	const float WornBefore = Equipment->WornResidue();
	if (!EnterFloor(*this, Mode, 2))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds));
	ACataclysmConsumptionDoubleCharacter* Double = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("set-up: the double came"), Double))
	{
		return false;
	}

	UCataclysmSkillEffects::ReduceHealthDirectly(Scene.Character, Double, 100000000.0f);
	if (!TestTrue(TEXT("set-up: the double died"), UCataclysmSkillEffects::IsDead(Double)))
	{
		return false;
	}
	const FCataclysmItem* Carried = Bag->ItemAt(CarriedSlot);
	int32 WornWithResidue = 0;
	for (const FCataclysmItem& Worn : Equipment->GetSlots())
	{
		WornWithResidue += !Worn.Base.IsNone() && Worn.Residue != 0.0f ? 1 : 0;
	}
	const UCataclysmCharacterSave* Record = ReadCharacterRecord(CharacterSlot);

	AddInfo(FString::Printf(TEXT("DOUBLE KILLED: worn residue %.0f before, %.0f after (%d of %d worn items still holding "
								 "some); the carried ring %.0f; marked %s, build %d items, hunts %s, has hunted %s; the "
								 "record %s, marked %s, build %d items"),
							WornBefore, Equipment->WornResidue(), WornWithResidue, Equipment->NumEquipped(),
							Carried ? Carried->Residue : -1.0f, Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no"),
							Scene.State->ConsumptionBuild().Worn.Num(),
							Mode->ConsumptionDoubleHuntsThisFloor() ? TEXT("yes") : TEXT("no"),
							Mode->ConsumptionDoubleHasHunted() ? TEXT("yes") : TEXT("no"),
							Record ? TEXT("read") : TEXT("NOT READ"),
							Record && Record->bMarkedByConsumption ? TEXT("yes") : TEXT("no"),
							Record ? Record->ConsumptionBuild.Worn.Num() : -1));

	TestTrue(TEXT("set-up: the worn residue was past the threshold"), WornBefore >= 1950.0f);
	TestEqual(TEXT("every worn item's residue is zero"), WornWithResidue, 0);
	TestEqual(TEXT("CONTROL: the carried ring keeps its residue"), Carried ? Carried->Residue : -1.0f, 300.0f);
	TestTrue(TEXT("and the character keeps its equipment"), Equipment->NumEquipped() > 0);
	TestFalse(TEXT("the mark is cleared"), Scene.State->IsMarkedByConsumption());
	TestFalse(TEXT("and the build with it"), Scene.State->HoldsConsumptionBuild());
	TestFalse(TEXT("the hunt is over on this floor"), Mode->ConsumptionDoubleHuntsThisFloor());
	TestFalse(TEXT("and reset, so a later crossing brings a fresh double"), Mode->ConsumptionDoubleHasHunted());
	if (!TestNotNull(TEXT("set-up: the character record was written"), Record))
	{
		return false;
	}
	TestFalse(TEXT("the record written is unmarked"), Record->bMarkedByConsumption);
	TestEqual(TEXT("and holds no build"), Record->ConsumptionBuild.Worn.Num(), 0);
	return true;
}

/**
 * A player killed by their double takes the ordinary death and keeps the mark and the build (O3: no consumed outcome
 * until a run can end, #2693). Prints "KILLED BY THE DOUBLE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleKillsThePlayerTest,
	"Cataclysm.ConsumptionDouble.APlayerKilledByTheDoubleStaysMarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleKillsThePlayerTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;

	if (!TestTrue(TEXT("set-up: the crossing marks"), CrossTheThreshold(Scene)) || !EnterFloor(*this, Mode, 2))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds));
	ACataclysmConsumptionDoubleCharacter* Double = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("set-up: the double came"), Double))
	{
		return false;
	}
	const float WornBefore = Scene.Character->GetEquipment()->WornResidue();
	UCataclysmSkillEffects::ReduceHealthDirectly(Double, Scene.Character, 100000000.0f);
	const bool bPlayerDied = UCataclysmSkillEffects::IsDead(Scene.Character);
	AddInfo(FString::Printf(TEXT("KILLED BY THE DOUBLE: the player died %s; marked %s, build %d items; worn residue %.0f "
								 "before and %.0f after; the double standing %s"),
							bPlayerDied ? TEXT("yes") : TEXT("NO"), Scene.State->IsMarkedByConsumption() ? TEXT("yes") : TEXT("no"),
							Scene.State->ConsumptionBuild().Worn.Num(), WornBefore,
							Scene.Character->GetEquipment()->WornResidue(),
							UCataclysmSkillEffects::IsDead(Double) ? TEXT("no") : TEXT("yes")));
	TestTrue(TEXT("set-up: the double killed the player"), bPlayerDied);
	TestTrue(TEXT("the player killed by the double stays marked"), Scene.State->IsMarkedByConsumption());
	TestTrue(TEXT("and keeps the build"), Scene.State->HoldsConsumptionBuild());
	TestEqual(TEXT("and the worn residue"), Scene.Character->GetEquipment()->WornResidue(), WornBefore);
	return true;
}

// ---------------------------------------------------------------------------
// The build it wears
// ---------------------------------------------------------------------------

/**
 * The double wears the build captured at the crossing, not the gear worn when it arrives: the player strips after
 * the crossing, and the double still wears what was worn then (O4). And a character marked with no build held -- a
 * character marked before version 6 -- gets a double wearing the gear worn when it arrives. Prints "DOUBLE WEARS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleWearsTheCaptureTest,
	"Cataclysm.ConsumptionDouble.TheDoubleWearsTheBuildCapturedAtTheCrossingNotTheGearWornWhenItArrives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleWearsTheCaptureTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;
	UCataclysmEquipmentComponent* Equipment = Scene.Character->GetEquipment();

	if (!TestTrue(TEXT("set-up: the crossing marks"), CrossTheThreshold(Scene)))
	{
		return false;
	}
	const int32 Captured = Scene.State->ConsumptionBuild().Worn.Num();
	Equipment->UnequipEverything();
	if (!TestEqual(TEXT("CONTROL: the player wears nothing when the double arrives"), Equipment->NumEquipped(), 0)
		|| !EnterFloor(*this, Mode, 2))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds));
	const ACataclysmConsumptionDoubleCharacter* Double = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("set-up: the double came"), Double))
	{
		return false;
	}
	float DoubleResidue = 0.0f;
	for (const FCataclysmWornItem& Entry : Double->WornBuild().Worn)
	{
		DoubleResidue += Entry.Item.Residue;
	}

	// AND A CHARACTER MARKED WITH NO BUILD HELD: the double wears what is worn when it arrives.
	FCataclysmItem First;
	FCataclysmItem Second;
	ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
	Equipment->Equip(WithResidue(TEXT("Ring_Band"), 5.0f), First, Second, Went);
	Scene.State->SetConsumptionBuild(FCataclysmPlayerBuild());
	Mode->LeaveEmpireDungeon();
	if (!EnterFloor(*this, Mode, 1))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds));
	const ACataclysmConsumptionDoubleCharacter* Fallback = Mode->ConsumptionDoubleOnTheFloor();
	AddInfo(FString::Printf(TEXT("DOUBLE WEARS: captured %d items; the stripped player's double wears %d with residue "
								 "%.0f; with no build held, the double wears %d (the player wears %d)"),
							Captured, Double->WornBuild().Worn.Num(), DoubleResidue,
							Fallback ? Fallback->WornBuild().Worn.Num() : -1, Equipment->NumEquipped()));
	TestTrue(TEXT("set-up: the crossing captured the helm, the boots and what was worn before"), Captured >= 2);
	TestEqual(TEXT("the double wears every item captured at the crossing"), Double->WornBuild().Worn.Num(), Captured);
	TestTrue(TEXT("their residue among them, the helm's and the boots' 2000"), DoubleResidue >= 2000.0f);
	if (!TestNotNull(TEXT("set-up: with no build held, a double came"), Fallback))
	{
		return false;
	}
	TestEqual(TEXT("with no build held, the double wears the gear worn when it arrives"),
			  Fallback->WornBuild().Worn.Num(), Equipment->NumEquipped());
	return true;
}

// ---------------------------------------------------------------------------
// The hunt across a save and load
// ---------------------------------------------------------------------------

/**
 * The hunt's state comes back from a save: a double standing at 40% health with a spent potion is raised again at
 * once where it stood, with that health and those charges, and the beat raises no second. Saved and put back in the
 * order `Cataclysm.LoadCharacter` uses: the floor entered, then the walk, then the floor's rule state. Prints "DOUBLE
 * LOADED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleLoadedTest,
	"Cataclysm.ConsumptionDouble.TheHuntComesBackFromASaveWithTheDoubleWhereItStood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleLoadedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;
	using Effects = UCataclysmDungeonModifierEffects;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;

	if (!TestTrue(TEXT("set-up: the crossing marks"), CrossTheThreshold(Scene)) || !EnterFloor(*this, Mode, 2))
	{
		return false;
	}
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds));
	ACataclysmConsumptionDoubleCharacter* Saved = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("set-up: the double came"), Saved))
	{
		return false;
	}
	SetHealthShare(Saved, 0.4f);
	UCataclysmAbilitySystemComponent* SavedPotions =
		Cast<UCataclysmAbilitySystemComponent>(Saved->GetAbilitySystemComponent());
	if (!TestNotNull(TEXT("set-up: the double holds potions"), SavedPotions))
	{
		return false;
	}
	SavedPotions->SetPotionCharges(0, 5.0f);
	const FVector StoodAt = Saved->GetActorLocation();

	// THE SAVE.
	const FCataclysmSavedWalk Walk = Mode->WalkStateNow();
	const FCataclysmSavedFloorRuleState Rules = Mode->FloorRuleStateNow();

	// THE LOAD, IN `Cataclysm.LoadCharacter`'S ORDER, on a mode whose hunt is reset: the floor entered again, the walk,
	// the floor's rule state.
	Mode->LeaveEmpireDungeon();
	if (!TestTrue(TEXT("set-up: floor 2 was entered again"), Mode->GoToFloor(2)))
	{
		return false;
	}
	Mode->ClearFloorEnemies();
	TestFalse(TEXT("CONTROL: the saved double went with the floor"), IsValid(Saved));
	Mode->RestoreWalkState(Walk);
	Mode->RestoreFloorRuleState(Rules);
	ACataclysmConsumptionDoubleCharacter* Loaded = Mode->ConsumptionDoubleOnTheFloor();
	if (!TestNotNull(TEXT("the double standing at the save stands again at once"), Loaded))
	{
		return false;
	}
	const UCataclysmAbilitySystemComponent* LoadedPotions =
		Cast<UCataclysmAbilitySystemComponent>(Loaded->GetAbilitySystemComponent());
	const float LoadedShare = HealthShareOf(Loaded);
	const float LoadedCharges = LoadedPotions ? LoadedPotions->GetPotionCharges(0) : -1.0f;
	const float FromWhereItStood = FVector::Dist2D(Loaded->GetActorLocation(), StoodAt);
	Beat(Mode, BeatsFor(Effects::CorruptedStalkerFirstFloorSeconds + 1.0f));
	AddInfo(FString::Printf(TEXT("DOUBLE LOADED: saved hunts %s, standing %s, hunted %s, share %.3f; loaded %.1f cm from "
								 "where it stood, share %.3f, first slot's charges %.1f; a minute later the same %s, %d "
								 "standing"),
							Rules.bConsumptionDoubleHuntsThisFloor ? TEXT("yes") : TEXT("no"),
							Rules.bConsumptionDoubleStanding ? TEXT("yes") : TEXT("no"),
							Walk.bConsumptionDoubleHasHunted ? TEXT("yes") : TEXT("no"), Walk.ConsumptionDoubleHealthShare,
							FromWhereItStood, LoadedShare, LoadedCharges,
							Mode->ConsumptionDoubleOnTheFloor() == Loaded ? TEXT("yes") : TEXT("NO"),
							DoublesStanding(Scene.World)));
	TestTrue(TEXT("the save holds that the floor is hunted"), Rules.bConsumptionDoubleHuntsThisFloor);
	TestTrue(TEXT("and that the double stood"), Rules.bConsumptionDoubleStanding);
	TestTrue(TEXT("and the walk that it has hunted"), Walk.bConsumptionDoubleHasHunted);
	TestTrue(TEXT("where it stood"), FromWhereItStood < 1.0f);
	TestEqual(TEXT("with the health it had"), LoadedShare, 0.4f, 0.01f);
	TestEqual(TEXT("and the potion charges it had"), LoadedCharges, 5.0f);
	TestTrue(TEXT("and the floor is hunted after the load"), Mode->ConsumptionDoubleHuntsThisFloor());
	TestTrue(TEXT("a minute later it is the same double"), Mode->ConsumptionDoubleOnTheFloor() == Loaded);
	TestEqual(TEXT("and only one stands"), DoublesStanding(Scene.World), 1);
	return true;
}

/**
 * A load keeps the flag the saved floor was entered with: a crossing made on the saved floor still brings the double
 * on the next floor, never on the loaded one, though the player is marked when the load enters it (R2). Prints
 * "DOUBLE FLAG LOADED".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmConsumptionDoubleFlagLoadedTest,
	"Cataclysm.ConsumptionDouble.ALoadKeepsWhetherTheSavedFloorWasHunted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmConsumptionDoubleFlagLoadedTest::RunTest(const FString& Parameters)
{
	using namespace CataclysmDoubleHuntTest;

	FHuntScene Scene = MakeHuntScene(/*bWithADungeon=*/true);
	if (!TestTrue(TEXT("set-up: a possessed player and a dungeon"), Scene.IsUsable() && Scene.Mode && Scene.Writer))
	{
		if (Scene.World)
		{
			Scene.World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { Scene.World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	const FString RunSlot = Scene.Writer->RunSlotName();
	const FString CharacterSlot = Scene.Writer->CharacterSlotName();
	ON_SCOPE_EXIT { DeleteSlot(RunSlot); DeleteSlot(CharacterSlot); };
	ACataclysmDungeonGameMode* Mode = Scene.Mode;

	if (!EnterFloor(*this, Mode, 2) || !TestTrue(TEXT("set-up: the crossing marks"), CrossTheThreshold(Scene)))
	{
		return false;
	}
	const FCataclysmSavedWalk Walk = Mode->WalkStateNow();
	const FCataclysmSavedFloorRuleState Rules = Mode->FloorRuleStateNow();

	Mode->LeaveEmpireDungeon();
	if (!EnterFloor(*this, Mode, 2))
	{
		return false;
	}
	const bool bLiveMarkWouldHunt = Mode->ConsumptionDoubleHuntsThisFloor();
	Mode->RestoreWalkState(Walk);
	Mode->RestoreFloorRuleState(Rules);
	const bool bHuntedAfterTheLoad = Mode->ConsumptionDoubleHuntsThisFloor();
	Beat(Mode, BeatsFor(70.0f));
	const bool bCame = Mode->ConsumptionDoubleOnTheFloor() != nullptr;
	AddInfo(FString::Printf(TEXT("DOUBLE FLAG LOADED: saved hunts %s; entered again marked, hunts %s; after the load "
								 "hunts %s; seventy seconds later a double %s"),
							Rules.bConsumptionDoubleHuntsThisFloor ? TEXT("yes") : TEXT("no"),
							bLiveMarkWouldHunt ? TEXT("yes") : TEXT("no"), bHuntedAfterTheLoad ? TEXT("yes") : TEXT("no"),
							bCame ? TEXT("CAME") : TEXT("did not come")));
	TestFalse(TEXT("the save holds that the floor of the crossing is not hunted"), Rules.bConsumptionDoubleHuntsThisFloor);
	TestTrue(TEXT("CONTROL: entered again marked, the floor would be hunted"), bLiveMarkWouldHunt);
	TestFalse(TEXT("the load puts back the saved floor's flag"), bHuntedAfterTheLoad);
	TestFalse(TEXT("so no double comes on the loaded floor"), bCame);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
