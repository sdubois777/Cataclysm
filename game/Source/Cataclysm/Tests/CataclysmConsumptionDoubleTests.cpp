// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
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
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerState.h"
#include "Player/CataclysmPowerScore.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the corrupted double's build, slice 2a of issue #2688.
 *
 * The double wears a player's build (`FCataclysmPlayerBuild`) through the same gatherer a player's refresh uses
 * (`UCataclysmEquipmentComponent::RefreshAttributesAs`): the player's own numbers, with no tier rescale, re-roll or
 * multiplier (R3, R4, ruled 2026-11-02); none of its gear drops (O2, the owner's decision of 2026-11-02); and its brain
 * skips a held skill with no shape (no War work). NOTHING RAISES IT YET: these tests spawn it themselves.
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

#endif // WITH_AUTOMATION_TESTS
