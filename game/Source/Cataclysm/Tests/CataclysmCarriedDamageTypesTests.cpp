// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmCharacterCreation.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Interface/CataclysmPassiveTreeWidget.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

/**
 * A character's class trees follow the damage types its worn weapons carry. Issue #857, layer 1, ruled 2026-10-25.
 *
 * WHAT THESE GUARD. `ACataclysmPlayerState::CarriedDamageTypes` and the static it asks,
 * `UCataclysmEquipmentComponent::CarriedDamageTypesOf`: the union, in order, of the worn weapons' `DamageTypes`; a
 * weapon with an empty list counting as the creation type; nothing worn, or no pawn, the creation type alone. And
 * every reader moved onto it: the reachable trees, the one-class rule, the refusal sentence, the equipment refresh
 * that decides whose effects apply, and the passive tree screen. And the starting weapon carrying the creation type.
 *
 * WHAT THEY DO NOT GUARD. The skills, which follow the same list since layer 2 (issue #837) and are guarded by the
 * group `Cataclysm.SkillPool.`.
 *
 * A REAL POSSESSED CHARACTER, wearing a two-handed Greataxe from `GiveStartingWeapon`, so every test that puts its own
 * weapons on takes everything off first and checks that nothing is worn.
 */
namespace CataclysmCarriedTypesTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	const FName Demonic(TEXT("Demonic"));
	const FName War(TEXT("War"));

	/** A possessed player character in this world, or null if any part failed. The helper the creation tests use. */
	ACataclysmPlayerCharacter* SpawnPossessedPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Character)
		{
			return nullptr;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Character);
		return Character;
	}

	/** The parts of one possessed player a test reads. */
	struct FCarrier
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		bool IsComplete() const { return Character && State && Equipment && AbilitySystem; }
	};

	/** A possessed player at level 100, so points never run out, still wearing what it began with. */
	FCarrier SpawnCarrier(FAutomationTestBase& Test, UWorld* World)
	{
		FCarrier Made;
		Made.Character = SpawnPossessedPlayer(World);
		Made.State = Made.Character ? Made.Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		Made.Equipment = Made.Character ? Made.Character->GetEquipment() : nullptr;
		Made.AbilitySystem = Made.State ? Made.State->GetCataclysmAbilitySystemComponent() : nullptr;
		if (!Test.TestTrue(TEXT("set-up: a possessed player with its state, equipment and ability system"),
						   Made.IsComplete()))
		{
			return FCarrier();
		}
		Made.State->SetLevelAndExperience(100, 0);
		return Made;
	}

	/** The same player wearing nothing, checked: the starting Greataxe fills both hands. */
	FCarrier SpawnBareCarrier(FAutomationTestBase& Test, UWorld* World)
	{
		FCarrier Made = SpawnCarrier(Test, World);
		if (!Made.IsComplete())
		{
			return Made;
		}
		Made.Equipment->UnequipEverything();
		if (!Test.TestEqual(TEXT("set-up: nothing is worn"), Made.Equipment->NumEquipped(), 0))
		{
			return FCarrier();
		}
		return Made;
	}

	/** A weapon of this base carrying these types and nothing else. */
	FCataclysmItem Weapon(const TCHAR* Base, const TArray<FName>& Types)
	{
		FCataclysmItem Made;
		Made.Base = FName(Base);
		Made.GearLevel = 0;
		Made.DamageTypes = Types;
		return Made;
	}

	/** Put a weapon on in one slot through the component's own equip, which announces and so refreshes. */
	bool Wear(FAutomationTestBase& Test, const FCarrier& Carrier, const FCataclysmItem& Item,
			  ECataclysmGearSlot Slot)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		const ECataclysmEquipResult Result = Carrier.Equipment->EquipInto(Item, Slot, Removed, AlsoRemoved);
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s goes on"), *Item.Base.ToString()),
							 Result == ECataclysmEquipResult::Equipped || Result == ECataclysmEquipResult::Swapped);
	}

	/** A list of names as one string, so two lists compare with one TestEqual of FString. */
	FString Joined(const TArray<FName>& Names)
	{
		return FString::JoinBy(Names, TEXT(", "), [](const FName& Name) { return Name.ToString(); });
	}

	/** The three trees a damage type unlocks, asked of the one list that says so. */
	TArray<FString> TreesOf(FName DamageType)
	{
		TArray<FString> Trees;
		for (const FName& Class : UCataclysmCharacterCreation::ClassesFor(DamageType))
		{
			Trees.Add(Class.ToString());
		}
		return Trees;
	}

	/** How many of these trees the list holds. */
	int32 HowManyOf(const TArray<FString>& Reachable, const TArray<FString>& Trees)
	{
		int32 Count = 0;
		for (const FString& Tree : Trees)
		{
			Count += Reachable.Contains(Tree) ? 1 : 0;
		}
		return Count;
	}

	/** The values of the modifiers the passive tree put on one stat, summed. Nought for a stat with no line. */
	float FromTheTreeOn(const FCarrier& Carrier, const TCHAR* Stat)
	{
		const FCataclysmStatInputs* Inputs = Carrier.AbilitySystem->GetStatInputs(FName(Stat));
		float Sum = 0.0f;
		if (Inputs)
		{
			for (const FCataclysmStatModifier& Modifier : Inputs->Modifiers)
			{
				if (Modifier.Source == ECataclysmModifierSource::PassiveKeystone)
				{
					Sum += Modifier.Value;
				}
			}
		}
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesWornWeaponTest,
	"Cataclysm.Passives.CarriedTypes.AWornWeaponsTypesAreTheTreesReached",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character wearing a weapon that carries War reaches the three War trees and not Masochist, and is refused a point
 * in Masochist with a sentence naming what the weapons carry. The control is the starting weapon, which carries the
 * creation type, Demonic, and reaches the three Demonic trees.
 */
bool FCataclysmCarriedTypesWornWeaponTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FCarrier Carrier = SpawnCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	// THE CONTROL: THE STARTING WEAPON.
	const TArray<FString> AtStart = Carrier.State->ReachableTrees();
	TestEqual(TEXT("control: the starting weapon reaches the three Demonic trees"),
			  HowManyOf(AtStart, TreesOf(Demonic)), 3);
	TestEqual(TEXT("control: and no War tree"), HowManyOf(AtStart, TreesOf(War)), 0);

	Carrier.Equipment->UnequipEverything();
	if (!TestEqual(TEXT("set-up: nothing is worn"), Carrier.Equipment->NumEquipped(), 0)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {War}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	const TArray<FString> WearingWar = Carrier.State->ReachableTrees();
	TestEqual(TEXT("a weapon carrying War reaches the three War trees"), HowManyOf(WearingWar, TreesOf(War)), 3);
	TestFalse(TEXT("and not Masochist"), WearingWar.Contains(TEXT("Masochist")));
	TestEqual(TEXT("nor any Demonic tree"), HowManyOf(WearingWar, TreesOf(Demonic)), 0);

	FString Reason;
	TestFalse(TEXT("a point in Masochist is refused"),
			  Carrier.State->SpendPassivePoint(FName(TEXT("Masochist_basic_spine_000")), Reason));
	TestTrue(*FString::Printf(TEXT("and the reason names the weapon and what it carries: %s"), *Reason),
			 Reason.Contains(TEXT("weapon")) && Reason.Contains(TEXT("War")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesTwoWeaponsTest,
	"Cataclysm.Passives.CarriedTypes.TwoWeaponsReachTheUnionOfTheirTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Sword carrying Demonic and an Axe carrying War, one in each hand, reach six trees, and the list is in order,
 * Weapon1's first. The control is the Sword alone, three; taking the Sword off leaves its partner's three.
 */
bool FCataclysmCarriedTypesTwoWeaponsTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Sword"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	// THE CONTROL: ONE WEAPON.
	TestEqual(TEXT("control: the Sword alone reaches three trees"), Carrier.State->ReachableTrees().Num(), 3);

	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Axe"), {War}), ECataclysmGearSlot::Weapon2)
		|| !TestEqual(TEXT("set-up: two weapons are worn"), Carrier.Equipment->WornWeapons().Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("the two carry Demonic and War, Weapon1's first"), Joined(Carrier.State->CarriedDamageTypes()),
			  FString(TEXT("Demonic, War")));
	const TArray<FString> Both = Carrier.State->ReachableTrees();
	TestEqual(TEXT("and reach six trees"), Both.Num(), 6);
	TestEqual(TEXT("the three Demonic ones"), HowManyOf(Both, TreesOf(Demonic)), 3);
	TestEqual(TEXT("and the three War ones"), HowManyOf(Both, TreesOf(War)), 3);

	FCataclysmItem Removed;
	if (!TestTrue(TEXT("set-up: the Sword comes off"),
				  Carrier.Equipment->Unequip(ECataclysmGearSlot::Weapon1, Removed)))
	{
		return false;
	}
	const TArray<FString> AxeAlone = Carrier.State->ReachableTrees();
	TestEqual(TEXT("taking the Sword off leaves three trees"), AxeAlone.Num(), 3);
	TestEqual(TEXT("the Axe's three War trees"), HowManyOf(AxeAlone, TreesOf(War)), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesOneWeaponTwoTypesTest,
	"Cataclysm.Passives.CarriedTypes.OneWeaponOfTwoTypesReachesBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * One Greataxe carrying Demonic and War reaches six trees: what a character could not be shown doing before, because
 * nothing handed a worn weapon's list to the trees. The control is a Greataxe carrying Demonic alone, three.
 */
bool FCataclysmCarriedTypesOneWeaponTwoTypesTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	// THE CONTROL: ONE TYPE.
	TestEqual(TEXT("control: a Greataxe carrying Demonic reaches three trees"),
			  Carrier.State->ReachableTrees().Num(), 3);

	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Demonic, War}), ECataclysmGearSlot::Weapon1)
		|| !TestEqual(TEXT("set-up: one weapon is worn"), Carrier.Equipment->WornWeapons().Num(), 1))
	{
		return false;
	}

	const TArray<FString> Both = Carrier.State->ReachableTrees();
	TestEqual(TEXT("a Greataxe carrying Demonic and War reaches six trees"), Both.Num(), 6);
	TestEqual(TEXT("the three Demonic ones"), HowManyOf(Both, TreesOf(Demonic)), 3);
	TestEqual(TEXT("and the three War ones"), HowManyOf(Both, TreesOf(War)), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesEffectsTest,
	"Cataclysm.Passives.CarriedTypes.ATreeOpenedByAWornTypeGrantsItsEffectsAndStopsWhenItComesOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Points in Hardened Skin, a Bulwark node worth increased armour per point, raise the stat line while a weapon
 * carrying War is worn. Swapping it for one carrying Demonic takes the effect away and the points stay spent; wearing
 * War again puts it back. The control is the Demonic weapon, holding the same points, giving nothing from the tree.
 *
 * THE ALLOCATION IS HANDED OVER, not spent, so that what is measured is the equipment refresh deciding whose effects
 * apply and nothing about whether a point could be spent. Every change after it is a change of weapon, which
 * announces and so refreshes; nothing here refreshes by hand.
 */
bool FCataclysmCarriedTypesEffectsTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;
	using Combat = UCataclysmCombatAttributeSet;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	const FName Root(TEXT("Bulwark_basic_trunk_000"));
	const FName HardenedSkin(TEXT("Bulwark_basic_trunk_001"));
	const int32 Points = 8;

	// WHAT THE SHEET SAYS THE NODE IS WORTH, read rather than assumed.
	float ArmourPerPoint = 0.0f;
	for (const FCataclysmPassiveEffectRow* Row :
		 UCataclysmPassiveTree::EffectsFor(UCataclysmPassiveTree::LoadEffectTable(), HardenedSkin))
	{
		if (Row && Row->Stat == TEXT("armor"))
		{
			ArmourPerPoint += Row->ValuePerPoint;
		}
	}
	if (!TestTrue(TEXT("set-up: Hardened Skin grants armour"), ArmourPerPoint > 0.0f))
	{
		return false;
	}

	const float TreeBefore = FromTheTreeOn(Carrier, TEXT("armor"));

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(Root, 1);
	Allocation.Add(HardenedSkin, Points);
	Carrier.State->SetPassiveAllocation(Allocation, TArray<FName>());

	// THE CONTROL: THE SAME POINTS, A DEMONIC WEAPON.
	const float TreeAsDemonic = FromTheTreeOn(Carrier, TEXT("armor"));
	const float ArmourAsDemonic = Carrier.AbilitySystem->GetNumericAttribute(Combat::GetArmorAttribute());
	TestEqual(TEXT("control: a Demonic weapon gives nothing from the Bulwark tree"), TreeAsDemonic, TreeBefore,
			  0.001f);

	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {War}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	const float TreeAsWar = FromTheTreeOn(Carrier, TEXT("armor"));
	const float ArmourAsWar = Carrier.AbilitySystem->GetNumericAttribute(Combat::GetArmorAttribute());
	TestEqual(TEXT("a War weapon puts the node's increase on armour"), TreeAsWar - TreeBefore,
			  ArmourPerPoint * Points, 0.01f);
	TestTrue(*FString::Printf(TEXT("and armour rose with it, from %.1f to %.1f"), ArmourAsDemonic, ArmourAsWar),
			 ArmourAsWar > ArmourAsDemonic);

	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("taking the War weapon off takes the increase away"), FromTheTreeOn(Carrier, TEXT("armor")),
			  TreeBefore, 0.001f);
	TestEqual(TEXT("and the nine points are still spent"), Carrier.State->GetPassiveAllocation().Total(),
			  Points + 1);
	TestEqual(TEXT("including the eight in Hardened Skin"),
			  Carrier.State->GetPassiveAllocation().PointsIn(HardenedSkin), Points);

	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {War}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("and wearing War again puts it back"), FromTheTreeOn(Carrier, TEXT("armor")) - TreeBefore,
			  ArmourPerPoint * Points, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesNothingWornTest,
	"Cataclysm.Passives.CarriedTypes.WithNothingWornOrNoPawnTheCreationTypesTreesAreReached",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * With no weapon worn, and with no pawn at all, the creation type's trees are reached, so the trees screen is never
 * empty. Ruled 2026-10-25. Each half's control is the default creation type, Demonic, read before it moves to War.
 */
bool FCataclysmCarriedTypesNothingWornTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// NO PAWN.
	ACataclysmPlayerState* Alone = World->SpawnActor<ACataclysmPlayerState>();
	if (!TestNotNull(TEXT("set-up: a player state with no pawn"), Alone))
	{
		return false;
	}
	TestEqual(TEXT("control: with no pawn the default creation type is carried"), Joined(Alone->CarriedDamageTypes()),
			  FString(TEXT("Demonic")));
	Alone->SetCreationChoice(FName(TEXT("Sword")), War);
	TestEqual(TEXT("with no pawn the chosen creation type is carried"), Joined(Alone->CarriedDamageTypes()),
			  FString(TEXT("War")));
	TestEqual(TEXT("and its three trees are reached"), HowManyOf(Alone->ReachableTrees(), TreesOf(War)), 3);

	// A PAWN WEARING NOTHING.
	const FCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	TestEqual(TEXT("control: wearing nothing the default creation type is carried"),
			  Joined(Carrier.State->CarriedDamageTypes()), FString(TEXT("Demonic")));
	TestEqual(TEXT("control: and its three trees are reached"),
			  HowManyOf(Carrier.State->ReachableTrees(), TreesOf(Demonic)), 3);
	Carrier.State->SetCreationChoice(FName(TEXT("Greataxe")), War);
	TestEqual(TEXT("wearing nothing the chosen creation type is carried"),
			  Joined(Carrier.State->CarriedDamageTypes()), FString(TEXT("War")));
	TestEqual(TEXT("and its three trees are reached"), HowManyOf(Carrier.State->ReachableTrees(), TreesOf(War)), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesOneClassEachTest,
	"Cataclysm.Passives.CarriedTypes.OneClassInEachCarriedType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Wearing a weapon carrying Demonic and War, the first point in Masochist and the first in Bulwark both succeed, and
 * Ravager and Berserker are then refused with the class sentence. The control is the same character wearing Demonic
 * alone, refused a point in Bulwark because no weapon carries War.
 */
bool FCataclysmCarriedTypesOneClassEachTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	const FName MasochistRoot(TEXT("Masochist_basic_spine_000"));
	const FName BulwarkRoot(TEXT("Bulwark_basic_trunk_000"));
	const FName RavagerRoot(TEXT("Ravager_basic_spine_000"));
	const FName BerserkerRoot(TEXT("Berserker_trunk_000"));

	// THE CONTROL: DEMONIC ALONE.
	FString Reason;
	TestFalse(TEXT("control: carrying Demonic alone, Bulwark refuses a point"),
			  Carrier.State->SpendPassivePoint(BulwarkRoot, Reason));
	TestTrue(*FString::Printf(TEXT("control: because no weapon carries War: %s"), *Reason),
			 Reason.Contains(TEXT("weapon")));

	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Demonic, War}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	Reason.Empty();
	TestTrue(TEXT("carrying both, the first point in Masochist succeeds"),
			 Carrier.State->SpendPassivePoint(MasochistRoot, Reason));
	Reason.Empty();
	TestTrue(TEXT("and the first in Bulwark"), Carrier.State->SpendPassivePoint(BulwarkRoot, Reason));

	Reason.Empty();
	TestFalse(TEXT("then Ravager is refused"), Carrier.State->SpendPassivePoint(RavagerRoot, Reason));
	TestEqual(TEXT("with the Demonic class sentence"), Reason,
			  FString(TEXT("Masochist is your Demonic class. A respec frees the choice.")));
	Reason.Empty();
	TestFalse(TEXT("and Berserker is refused"), Carrier.State->SpendPassivePoint(BerserkerRoot, Reason));
	TestEqual(TEXT("with the War class sentence"), Reason,
			  FString(TEXT("Bulwark is your War class. A respec frees the choice.")));
	TestEqual(TEXT("so two points are spent"), Carrier.State->GetPassiveAllocation().Total(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesStartingWeaponTest,
	"Cataclysm.Passives.CarriedTypes.TheStartingWeaponCarriesTheCreationTypeAndFollowsAChangeOfIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The starting weapon's list is the creation type. A creation choice that changes only the damage type changes what
 * the same worn weapon carries, without putting a new one on; one that changes the weapon type puts on a weapon
 * carrying the chosen type. The control is the starting weapon read before any choice.
 */
bool FCataclysmCarriedTypesStartingWeaponTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FCarrier Carrier = SpawnCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}

	const FCataclysmItem* Starting = Carrier.Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
	if (!TestNotNull(TEXT("set-up: the starting weapon is worn"), Starting))
	{
		return false;
	}

	// THE CONTROL: BEFORE ANY CHOICE.
	TestEqual(TEXT("control: the starting weapon carries the creation type"), Joined(Starting->DamageTypes),
			  FString(TEXT("Demonic")));
	TestEqual(TEXT("control: and that is what the character carries"), Joined(Carrier.State->CarriedDamageTypes()),
			  FString(TEXT("Demonic")));

	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("the weapon skill table loads"), Skills)
		|| !TestNotNull(TEXT("the item base table loads"), Bases))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		return false;
	}

	// ONLY THE DAMAGE TYPE MOVES.
	FString Reason;
	if (!TestTrue(TEXT("the same Greataxe carrying War is taken"),
				  Carrier.State->ChooseAtCreation(Skills, Bases, FName(TEXT("Greataxe")), War, Reason)))
	{
		AddError(Reason);
		return false;
	}
	const FCataclysmItem* Same = Carrier.Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
	TestTrue(TEXT("the same weapon is still worn"), Same && Same->Base == FName(TEXT("Weapon_Greataxe")));
	TestEqual(TEXT("one item, not a second axe"), Carrier.Equipment->NumEquipped(), 1);
	TestEqual(TEXT("and it now carries War"), Same ? Joined(Same->DamageTypes) : FString(), FString(TEXT("War")));
	TestEqual(TEXT("so the character carries War"), Joined(Carrier.State->CarriedDamageTypes()),
			  FString(TEXT("War")));
	TestFalse(TEXT("and Masochist is no longer reached"), Carrier.State->ReachableTrees().Contains(TEXT("Masochist")));

	// AND THE WEAPON TYPE MOVES, which puts a new weapon on.
	Reason.Empty();
	if (!TestTrue(TEXT("a Sword carrying Demonic is taken"),
				  Carrier.State->ChooseAtCreation(Skills, Bases, FName(TEXT("Sword")), Demonic, Reason))
		|| !TestTrue(TEXT("and applied to the character"), Carrier.Character->ApplyCreationChoice()))
	{
		AddError(Reason);
		return false;
	}
	// READ AS THE ONE WORN WEAPON, NOT AT Weapon1 (ruled 2026-10-25 after window t88w): `Equip` puts it in the first
	// free candidate slot, which beside the two-handed Greataxe in Weapon1 is Weapon2, and the Greataxe comes off.
	const TArray<FCataclysmItem> Worn = Carrier.Equipment->WornWeapons();
	const FCataclysmItem* InWeapon1 = Carrier.Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
	const FCataclysmItem* InWeapon2 = Carrier.Equipment->EquippedAt(ECataclysmGearSlot::Weapon2);
	AddInfo(FString::Printf(TEXT("set-up: after the creation choice Weapon1 holds %s and Weapon2 holds %s"),
							InWeapon1 && !InWeapon1->Base.IsNone() ? *InWeapon1->Base.ToString() : TEXT("nothing"),
							InWeapon2 && !InWeapon2->Base.IsNone() ? *InWeapon2->Base.ToString() : TEXT("nothing")));
	const FCataclysmItem* Created = Worn.Num() == 1 ? &Worn[0] : nullptr;
	TestEqual(TEXT("exactly one weapon is worn"), Worn.Num(), 1);
	TestTrue(TEXT("a Sword is worn"), Created && Created->Base == FName(TEXT("Weapon_Sword")));
	TestEqual(TEXT("carrying the chosen type"), Created ? Joined(Created->DamageTypes) : FString(),
			  FString(TEXT("Demonic")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesPossessionTest,
	"Cataclysm.Passives.CarriedTypes.APossessedCharacterHoldingTheChosenWeaponCarriesTheChosenType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The choice is on the player state before the pawn exists, as for a character coming back from the capital. The
 * starting weapon is put on at `BeginPlay`, before the player state arrives, with the default type; possession's
 * `ApplyCreationChoice` returns early because a Greataxe is already held, and must still leave it carrying War. The
 * control is a second character in another world, with nothing chosen, holding a Greataxe carrying Demonic.
 */
bool FCataclysmCarriedTypesPossessionTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	// THE CONTROL: NOTHING CHOSEN.
	{
		UWorld* ControlWorld = MakeWorldThatHasBegunPlay();
		ON_SCOPE_EXIT { ControlWorld->DestroyWorld(false); };
		const FCarrier Control = SpawnCarrier(*this, ControlWorld);
		if (!Control.IsComplete())
		{
			return false;
		}
		TestEqual(TEXT("control: with nothing chosen the character carries Demonic"),
				  Joined(Control.State->CarriedDamageTypes()), FString(TEXT("Demonic")));
	}

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("set-up: a player state"), State) || !TestNotNull(TEXT("set-up: a controller"), Controller))
	{
		return false;
	}
	State->SetCreationChoice(FName(TEXT("Greataxe")), War);
	Controller->SetPlayerState(State);

	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("set-up: a player character"), Character))
	{
		return false;
	}

	// NOTHING IS CALLED BY HAND AFTER THIS LINE, as in the creation test of the same path.
	Controller->Possess(Character);

	const FCataclysmItem* Held =
		Character->GetEquipment() ? Character->GetEquipment()->EquippedAt(ECataclysmGearSlot::Weapon1) : nullptr;
	TestTrue(TEXT("the character holds a Greataxe"), Held && Held->Base == FName(TEXT("Weapon_Greataxe")));
	TestEqual(TEXT("carrying War"), Held ? Joined(Held->DamageTypes) : FString(), FString(TEXT("War")));
	TestEqual(TEXT("so the character carries War"), Joined(State->CarriedDamageTypes()), FString(TEXT("War")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesScreenTest,
	"Cataclysm.Passives.CarriedTypes.TheTreeScreenFollowsTheWornList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The passive tree screen's reachable trees and its class refusal follow the worn list. With Bulwark holding a point,
 * a weapon carrying Demonic and War dims Berserker with the War class sentence and leaves Bulwark undimmed. The
 * control is the starting weapon: no War tree on the screen, and Berserker not dimmed.
 *
 * ON BUTTONS THIS TEST MADE, as `Cataclysm.Passives.OneClass.TheScreenDimsTheClassesNotChosen` does and for its
 * reason. THE POINT IS HANDED OVER, not spent, so the screen's own question is the only one asked.
 */
bool FCataclysmCarriedTypesScreenTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FCarrier Carrier = SpawnCarrier(*this, World);
	APlayerController* Controller =
		Carrier.IsComplete() ? Cast<APlayerController>(Carrier.Character->GetController()) : nullptr;
	if (!TestNotNull(TEXT("set-up: a player controller"), Controller))
	{
		return false;
	}

	UCataclysmPassiveTreeWidget* Screen = NewObject<UCataclysmPassiveTreeWidget>(Controller);
	Screen->SetPlayerStateForTests(Carrier.State);

	FCataclysmPassiveAllocation Allocation;
	Allocation.Add(FName(TEXT("Bulwark_basic_trunk_000")), 1);
	Carrier.State->SetPassiveAllocation(Allocation, TArray<FName>());

	// THE CONTROL: THE STARTING WEAPON, DEMONIC.
	TestEqual(TEXT("control: the screen reaches no War tree"), HowManyOf(Screen->ReachableTrees(), TreesOf(War)), 0);
	UCataclysmChoiceButton* BerserkerBefore = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*BerserkerBefore, FName(TEXT("Berserker")), true);
	TestFalse(TEXT("control: and does not dim Berserker"), BerserkerBefore->IsDimmed());

	Carrier.Equipment->UnequipEverything();
	if (!TestEqual(TEXT("set-up: nothing is worn"), Carrier.Equipment->NumEquipped(), 0)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Demonic, War}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	const TArray<FString> Shown = Screen->ReachableTrees();
	TestEqual(TEXT("wearing both, the screen reaches the three War trees"), HowManyOf(Shown, TreesOf(War)), 3);
	TestEqual(TEXT("and the three Demonic ones"), HowManyOf(Shown, TreesOf(Demonic)), 3);

	UCataclysmChoiceButton* Berserker = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*Berserker, FName(TEXT("Berserker")), true);
	TestTrue(TEXT("Berserker is dimmed"), Berserker->IsDimmed());
	TestEqual(TEXT("with the War class sentence as its tool tip"), Berserker->GetToolTipText().ToString(),
			  FString(TEXT("Bulwark is your War class. A respec frees the choice.")));

	UCataclysmChoiceButton* Bulwark = NewObject<UCataclysmChoiceButton>();
	Screen->DescribeButtonForTests(*Bulwark, FName(TEXT("Bulwark")), true);
	TestFalse(TEXT("and Bulwark is not dimmed"), Bulwark->IsDimmed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesEmptyListTest,
	"Cataclysm.Passives.CarriedTypes.AWeaponWithAnEmptyListCountsAsTheCreationType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A worn weapon with no list (an item saved before the list existed, or built by hand) counts as carrying the
 * creation type, beside a weapon carrying War. And the union's own rules, on the static, with no character: the first
 * weapon's types first, each type once, and the creation type alone for no weapon. The control is the bare Sword
 * alone, which carries the creation type and nothing else.
 */
bool FCataclysmCarriedTypesEmptyListTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;
	using FEquipped = UCataclysmEquipmentComponent;

	const FName Void(TEXT("Void"));
	TestEqual(TEXT("the union keeps the first weapon's order and lists each type once"),
			  Joined(FEquipped::CarriedDamageTypesOf(
				  {Weapon(TEXT("Weapon_Sword"), {War, Void}), Weapon(TEXT("Weapon_Axe"), {Void, Demonic})}, Demonic)),
			  FString(TEXT("War, Void, Demonic")));
	TestEqual(TEXT("an empty list is the creation type, in its weapon's place"),
			  Joined(FEquipped::CarriedDamageTypesOf(
				  {Weapon(TEXT("Weapon_Sword"), {}), Weapon(TEXT("Weapon_Axe"), {War})}, Void)),
			  FString(TEXT("Void, War")));
	TestEqual(TEXT("no weapon is the creation type alone"),
			  Joined(FEquipped::CarriedDamageTypesOf(TArray<FCataclysmItem>(), War)), FString(TEXT("War")));

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Sword"), {}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	// THE CONTROL: THE BARE SWORD ALONE.
	TestEqual(TEXT("control: a weapon with no list carries the creation type"),
			  Joined(Carrier.State->CarriedDamageTypes()), FString(TEXT("Demonic")));

	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Axe"), {War}), ECataclysmGearSlot::Weapon2))
	{
		return false;
	}
	TestEqual(TEXT("beside a weapon carrying War, the two carry Demonic and War"),
			  Joined(Carrier.State->CarriedDamageTypes()), FString(TEXT("Demonic, War")));
	TestEqual(TEXT("and six trees are reached"), Carrier.State->ReachableTrees().Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCarriedTypesPlainDropKeepsItsTypeTest,
	"Cataclysm.Passives.CarriedTypes.APlainDropOfTheCreationWeaponTypeKeepsItsTypeThroughACreationChangeAndAPossession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A dropped Greataxe carrying Void, with no affix and no enchantment, is the same weapon type as the creation weapon
 * and holds one type, so recognising the creation weapon by its contents would rewrite it. It keeps Void through a
 * creation choice that changes only the damage type, and through a possession that applies a chosen Greataxe. Ruled
 * 2026-10-25 (Q-L1): only a weapon marked `bMadeAtCreation` follows. The control is the marked starting weapon, which
 * does follow the same kind of change.
 *
 * THE DROP IS BUILT BY HAND, unmarked as every drop is, because a rolled one would carry whatever affixes its seed gave.
 */
bool FCataclysmCarriedTypesPlainDropKeepsItsTypeTest::RunTest(const FString&)
{
	using namespace CataclysmCarriedTypesTest;

	const FName Void(TEXT("Void"));
	const UDataTable* Skills = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("the weapon skill table loads"), Skills)
		|| !TestNotNull(TEXT("the item base table loads"), Bases))
	{
		AddError(TEXT("Run  python tools/run_editor_python.py tools/generate_datatable_assets.py"));
		return false;
	}

	// A CREATION CHANGE.
	{
		UWorld* World = MakeWorldThatHasBegunPlay();
		ON_SCOPE_EXIT { World->DestroyWorld(false); };
		const FCarrier Carrier = SpawnCarrier(*this, World);
		if (!Carrier.IsComplete())
		{
			return false;
		}

		// THE CONTROL: THE MARKED STARTING WEAPON FOLLOWS.
		FString Reason;
		if (!TestTrue(TEXT("control: a Greataxe carrying War is taken"),
					  Carrier.State->ChooseAtCreation(Skills, Bases, FName(TEXT("Greataxe")), War, Reason)))
		{
			AddError(Reason);
			return false;
		}
		const FCataclysmItem* Marked = Carrier.Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
		TestEqual(TEXT("control: the marked starting weapon now carries War"),
				  Marked ? Joined(Marked->DamageTypes) : FString(), FString(TEXT("War")));

		Carrier.Equipment->UnequipEverything();
		if (!TestEqual(TEXT("set-up: nothing is worn"), Carrier.Equipment->NumEquipped(), 0)
			|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Void}), ECataclysmGearSlot::Weapon1))
		{
			return false;
		}

		Reason.Empty();
		if (!TestTrue(TEXT("a Greataxe carrying Demonic is taken"),
					  Carrier.State->ChooseAtCreation(Skills, Bases, FName(TEXT("Greataxe")), Demonic, Reason)))
		{
			AddError(Reason);
			return false;
		}
		const FCataclysmItem* Dropped = Carrier.Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
		TestEqual(TEXT("the plain drop still carries Void"), Dropped ? Joined(Dropped->DamageTypes) : FString(),
				  FString(TEXT("Void")));
		TestEqual(TEXT("so the character carries Void"), Joined(Carrier.State->CarriedDamageTypes()),
				  FString(TEXT("Void")));
	}

	// A POSSESSION, with the drop already worn and a Greataxe chosen.
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	UCataclysmEquipmentComponent* Equipment = Character ? Character->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("set-up: a player state"), State) || !TestNotNull(TEXT("set-up: a controller"), Controller)
		|| !TestNotNull(TEXT("set-up: a player character with equipment"), Equipment))
	{
		return false;
	}
	State->SetCreationChoice(FName(TEXT("Greataxe")), War);
	Controller->SetPlayerState(State);

	Equipment->UnequipEverything();
	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	const ECataclysmEquipResult Result =
		Equipment->EquipInto(Weapon(TEXT("Weapon_Greataxe"), {Void}), ECataclysmGearSlot::Weapon1, Removed,
							 AlsoRemoved);
	if (!TestTrue(TEXT("set-up: the plain drop is the only thing worn"),
				  Result == ECataclysmEquipResult::Equipped && Equipment->NumEquipped() == 1))
	{
		return false;
	}

	Controller->Possess(Character);

	const FCataclysmItem* Held = Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
	TestTrue(TEXT("after possession the plain drop is still held"),
			 Held && Held->Base == FName(TEXT("Weapon_Greataxe")) && !Held->bMadeAtCreation);
	TestEqual(TEXT("and still carries Void"), Held ? Joined(Held->DamageTypes) : FString(), FString(TEXT("Void")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
