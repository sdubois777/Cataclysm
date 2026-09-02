// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
// For a weapon left standing in the ground, which draws no weapon in a
// hand while it stands there. Issue #1141.
#include "AbilitySystem/CataclysmPlantedWeapon.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponMeshes.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestSkip.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for the weapon drawn in the player character's hand. Issue #1125.
 *
 * WHAT THESE GUARD. No weapon was drawn anywhere in this game. A player equipped
 * a Greataxe, its stats and its six skills changed, and nothing on screen
 * changed at all.
 *
 * THE HALF THAT BREAKS SILENTLY IS CLEARING, NOT DRAWING. A
 * `RefreshWeaponMeshes` that only ever assigned a mesh would look completely
 * correct in play until somebody took a weapon off: the old one would stay in
 * the character's hand for ever, while the gear panel and every number said the
 * hand was empty. `ItStopsDrawingAWeaponThatWasTakenOff` below is the assertion
 * this file exists for.
 *
 * THESE NEED THE WEAPONS PACK AND SAY SO WHEN IT IS ABSENT.
 * `game/Content/Medieval_Weapons/` is third-party content and `.gitignore`
 * excludes it, so a fresh checkout has none of it and every mesh lookup answers
 * null. That is correct behaviour -- the character fights identically with an
 * empty hand -- and it means these report through
 * `CataclysmTestSkip::ReportSkippedHalf` rather than failing.
 *
 * WHAT THESE DELIBERATELY DO NOT COVER. Whether the weapon looks right, sits in
 * the hand at a sensible angle, or is a sensible size. The automation command
 * passes `-nullrhi`, so nothing reaches a screen under test. Somebody has to
 * look.
 */

namespace CataclysmWeaponMeshTest
{
	/** Real rows in `game/Data/ItemBases.csv`, chosen so the two shapes that
	 *  matter are both covered: a one-handed weapon that leaves the off-hand
	 *  free, and a two-handed one that takes both slots. */
	const TCHAR* OneHandedBase = TEXT("Weapon_Sword");
	const TCHAR* OffHandBase = TEXT("Weapon_Shield");
	const TCHAR* TwoHandedBase = TEXT("Weapon_Greatsword");

	/** The base the design says draws nothing on purpose. */
	const TCHAR* DrawsNothingBase = TEXT("Weapon_Fist");

	FCataclysmItem Plain(const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		return Item;
	}

	ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
		ACataclysmPlayerCharacter* Actor =
			World->SpawnActor<ACataclysmPlayerCharacter>(
				FVector::ZeroVector, FRotator::ZeroRotator);
		if (State && Actor)
		{
			Actor->SetPlayerState(State);
			Actor->OnRep_PlayerState();
			return Actor;
		}
		return nullptr;
	}

	/**
	 * Wears an item and redraws, which is what an equipment change does.
	 *
	 * @return what is worn in that slot afterwards, or None. **Checked by every
	 *         caller before it looks at a hand.** The first version of these
	 *         tests assumed the equip worked and asserted only on the mesh, so
	 *         when a starting weapon got in the way the failure read "no weapon
	 *         is drawn" and said nothing about why.
	 */
	FName Wear(ACataclysmPlayerCharacter* Player, const TCHAR* Base,
			   ECataclysmGearSlot& OutSlot)
	{
		// OutSlot IS AN OUTPUT AND NOT A REQUEST, which is easy to read the
		// wrong way round. `UCataclysmEquipmentComponent::Equip` chooses the
		// slot itself -- the first free candidate -- and writes back which one
		// it used. `EquipInto` is the one that takes a slot you name. These
		// tests empty both weapon slots first, so Weapon1 is always the first
		// free candidate and the answer is always Weapon1; the parameter is
		// still passed by reference so a caller reads the truth rather than
		// what it hoped for.
		UCataclysmEquipmentComponent* Equipment = Player->GetEquipment();
		if (!Equipment)
		{
			return NAME_None;
		}

		// EVERY WEAPON SLOT IS EMPTIED FIRST. The character puts on a starting
		// Greataxe at BeginPlay, which is two-handed and therefore occupies both
		// weapon slots, so equipping over it takes the swap path rather than the
		// ordinary one. These tests are about drawing a weapon, not about how
		// swapping resolves -- CataclysmEquipmentTests.cpp owns that -- so they
		// start from an empty pair of hands and say so.
		FCataclysmItem Discarded;
		Equipment->Unequip(ECataclysmGearSlot::Weapon1, Discarded);
		Equipment->Unequip(ECataclysmGearSlot::Weapon2, Discarded);

		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		Equipment->Equip(Plain(Base), Removed, AlsoRemoved, OutSlot);

		// DRIVEN DIRECTLY RATHER THAN LEFT TO THE BROADCAST. Equip does
		// broadcast, and in the running game that reaches OnEquipmentChanged
		// and then RefreshWeaponMeshes. Calling it here as well makes the test
		// independent of whether the handler happens to be bound in a world
		// that was never possessed.
		Player->RefreshWeaponMeshes();

		const FCataclysmItem* Worn = Equipment->EquippedAt(OutSlot);
		return Worn ? Worn->Base : NAME_None;
	}

	/** The named weapon component, or null. Found by name rather than by class
	 *  because the character's camera brings a CameraProxyMeshComponent of its
	 *  own, which also derives from UStaticMeshComponent. */
	UStaticMeshComponent* HandNamed(ACataclysmPlayerCharacter* Player,
									const TCHAR* Name)
	{
		TArray<UStaticMeshComponent*> All;
		Player->GetComponents<UStaticMeshComponent>(All);
		for (UStaticMeshComponent* Component : All)
		{
			if (Component && Component->GetName() == Name)
			{
				return Component;
			}
		}
		return nullptr;
	}

	/** Whether the weapons pack is on this machine at all. */
	bool WeaponArtIsPresent()
	{
		const UDataTable* Table = UCataclysmWeaponMeshes::LoadTable();
		if (!Table)
		{
			return false;
		}

		float Scale = 1.0f;
		return UCataclysmWeaponMeshes::MeshFor(
				   Table, FName(OneHandedBase), Scale) != nullptr;
	}

	FString NoArtReason()
	{
		return TEXT("game/Content/Medieval_Weapons/ is not on this machine, so "
					"every weapon mesh lookup answers null and there is nothing "
					"to see in a hand. That the table has a row for every "
					"weapon base IS checked; that a mesh reaches a hand is not. "
					"See game/docs/weapon-source-assets.md.");
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The table
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmWeaponMeshTableTest,
	"Cataclysm.WeaponMesh.EveryWeaponBaseHasARowInTheTable")
{
	const UDataTable* Table = UCataclysmWeaponMeshes::LoadTable();
	if (!TestNotNull(TEXT("DT_WeaponMeshes"), Table))
	{
		return false;
	}

	// EVERY WEAPON BASE, INCLUDING THE ONES THAT DRAW NOTHING. A base with no
	// row would draw nothing and say nothing, which is the failure this table
	// exists to make impossible.
	for (const TCHAR* Base : {CataclysmWeaponMeshTest::OneHandedBase,
							  CataclysmWeaponMeshTest::OffHandBase,
							  CataclysmWeaponMeshTest::TwoHandedBase,
							  CataclysmWeaponMeshTest::DrawsNothingBase})
	{
		TestTrue(FString::Printf(TEXT("%s has a row"), Base),
			UCataclysmWeaponMeshes::HasRowFor(Table, FName(Base)));
	}

	// AND A BASE THAT IS NOT A WEAPON HAS NONE, so that "has a row" means
	// something rather than answering true for anything asked of it.
	TestFalse(TEXT("a helm has no weapon mesh row"),
		UCataclysmWeaponMeshes::HasRowFor(Table, TEXT("Head_Helm")));

	return true;
}

CATACLYSM_TEST(FCataclysmWeaponDrawsNothingTest,
	"Cataclysm.WeaponMesh.AFistHasARowAndStillDrawsNothing")
{
	const UDataTable* Table = UCataclysmWeaponMeshes::LoadTable();
	if (!TestNotNull(TEXT("DT_WeaponMeshes"), Table))
	{
		return false;
	}

	// THE TWO FACTS TOGETHER ARE THE POINT. Having a row is what says somebody
	// decided; the empty mesh is the decision. Either alone would be
	// indistinguishable from a base nobody filled in.
	TestTrue(TEXT("the Fist has a row, so this was decided"),
		UCataclysmWeaponMeshes::HasRowFor(
			Table, FName(CataclysmWeaponMeshTest::DrawsNothingBase)));

	float Scale = 1.0f;
	TestNull(TEXT("and it draws nothing, because unarmed should show no weapon"),
		UCataclysmWeaponMeshes::MeshFor(
			Table, FName(CataclysmWeaponMeshTest::DrawsNothingBase), Scale));

	TestEqual(TEXT("and the scale it was given is left alone"), Scale, 1.0f);

	return true;
}

// --------------------------------------------------------------------------
// The hands
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmWeaponReachesTheHandTest,
	"Cataclysm.WeaponMesh.EquippingAWeaponPutsItInTheRightHand")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmWeaponMeshTest::SpawnPlayer(World);
	if (TestNotNull(TEXT("a player"), Player))
	{
		if (CataclysmWeaponMeshTest::WeaponArtIsPresent())
		{
			// WHAT IS WORN IS ASSERTED FIRST, so that a failure here names the
			// equipment rather than the mesh. Without it an equip that did not
			// take reads as "no weapon is drawn", which points at the wrong
			// half of the feature.
			ECataclysmGearSlot WentTo = ECataclysmGearSlot::Weapon1;
			const FName Worn = CataclysmWeaponMeshTest::Wear(
				Player, CataclysmWeaponMeshTest::OneHandedBase, WentTo);

			TestEqual(TEXT("the sword is worn in the first weapon slot"),
				Worn, FName(CataclysmWeaponMeshTest::OneHandedBase));

			const UStaticMeshComponent* Right =
				CataclysmWeaponMeshTest::HandNamed(Player, TEXT("RightHandWeapon"));

			if (TestNotNull(TEXT("the right hand component"), Right))
			{
				// COMPARED RATHER THAN PASSED TO TestNotNull, because
				// GetStaticMesh returns a TObjectPtr and the template cannot
				// deduce a raw pointer type from one.
				TestTrue(TEXT("a weapon is drawn in the right hand"),
					Right->GetStaticMesh() != nullptr);
			}
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmWeaponMeshTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmWeaponLeavesTheHandTest,
	"Cataclysm.WeaponMesh.ItStopsDrawingAWeaponThatWasTakenOff")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmWeaponMeshTest::SpawnPlayer(World);
	UCataclysmEquipmentComponent* Equipment =
		Player ? Player->GetEquipment() : nullptr;

	if (TestNotNull(TEXT("a player with equipment"), Equipment))
	{
		if (CataclysmWeaponMeshTest::WeaponArtIsPresent())
		{
			ECataclysmGearSlot WentTo = ECataclysmGearSlot::Weapon1;
			const FName Worn = CataclysmWeaponMeshTest::Wear(
				Player, CataclysmWeaponMeshTest::OneHandedBase, WentTo);

			TestEqual(TEXT("the sword is worn to begin with"),
				Worn, FName(CataclysmWeaponMeshTest::OneHandedBase));

			UStaticMeshComponent* Right =
				CataclysmWeaponMeshTest::HandNamed(Player, TEXT("RightHandWeapon"));

			if (TestNotNull(TEXT("the right hand component"), Right))
			{
				TestTrue(TEXT("it starts by holding something"),
					Right->GetStaticMesh() != nullptr);

				// AND NOW THE HAND IS EMPTIED. This is the assertion this file
				// exists for: a RefreshWeaponMeshes that only ever assigned
				// would pass every other check here and leave a weapon in the
				// hand of a character that is no longer carrying one.
				FCataclysmItem Removed;
				Equipment->Unequip(ECataclysmGearSlot::Weapon1, Removed);
				Player->RefreshWeaponMeshes();

				TestTrue(TEXT("and taking it off empties the hand"),
					Right->GetStaticMesh() == nullptr);
			}
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmWeaponMeshTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmWeaponSwapRedrawsTest,
	"Cataclysm.WeaponMesh.SwappingATwoHandedWeaponForAOneHandedOneRedrawsTheHand")
{
	// WRITTEN BECAUSE THE OTHER TESTS IN THIS FILE FAILED FIRST TIME AND THE
	// FIX COULD HAVE HIDDEN A REAL FAULT. They equipped a one-handed weapon
	// straight over the two-handed Greataxe the character starts wearing, and
	// the right hand came back empty. The fix was to empty both weapon slots
	// first, which made those tests test what they say -- and would also have
	// buried the question of whether the swap itself is broken.
	//
	// SWAPPING IS AN ORDINARY THING A PLAYER DOES, so it gets its own test
	// rather than being assumed. This starts from the character's real starting
	// state, wearing the two-handed Greataxe, and equips a one-handed sword
	// over it exactly as a player would.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmWeaponMeshTest::SpawnPlayer(World);
	UCataclysmEquipmentComponent* Equipment =
		Player ? Player->GetEquipment() : nullptr;

	if (TestNotNull(TEXT("a player with equipment"), Equipment))
	{
		if (CataclysmWeaponMeshTest::WeaponArtIsPresent())
		{
			// THE STARTING STATE IS PUT ON DELIBERATELY rather than relied on.
			// GiveStartingWeapon runs from BeginPlay behind a HasAuthority
			// check, and a test world is not the running game, so what a
			// character happens to be wearing here is not something to build an
			// assertion on top of.
			// AN LVALUE, BECAUSE Equip TAKES THE SLOT BY NON-CONST REFERENCE
			// AND CAN CHANGE IT. A two-handed weapon lands in Weapon1 whichever
			// slot was asked for, and the caller is told which one it actually
			// went to. Passing the enumerator directly does not compile.
			ECataclysmGearSlot Slot = ECataclysmGearSlot::Weapon1;
			FCataclysmItem Removed;
			FCataclysmItem AlsoRemoved;
			Equipment->Equip(
				CataclysmWeaponMeshTest::Plain(TEXT("Weapon_Greataxe")),
				Removed, AlsoRemoved, Slot);
			Player->RefreshWeaponMeshes();

			UStaticMeshComponent* Right =
				CataclysmWeaponMeshTest::HandNamed(Player, TEXT("RightHandWeapon"));

			if (TestNotNull(TEXT("the right hand component"), Right))
			{
				TestTrue(TEXT("the two-handed axe is drawn to begin with"),
					Right->GetStaticMesh() != nullptr);

				const UStaticMesh* Axe = Right->GetStaticMesh();

				// AND NOW THE SWAP, WITH NO UNEQUIP FIRST. The equipment
				// component takes the two-hander off itself; a player never
				// has to.
				// THE COMPONENT CHOOSES THE SLOT AND TELLS YOU WHICH. `Equip`'s
				// fourth parameter is an OUTPUT, not a request: it takes the
				// first free candidate slot, and `EquipInto` is the one that
				// takes a slot you name. That matters here, because a
				// two-handed weapon occupies Weapon1 and leaves Weapon2 reading
				// as empty, so a one-handed weapon swapped in over it lands in
				// **Weapon2** and is drawn in the LEFT hand.
				//
				// THAT IS THE DESIGN AND NOT A FAULT.
				// UCataclysmEquipmentComponent says so where it relocates a
				// two-hander: "The two weapon slots are interchangeable -- the
				// design says there is no primary hand". This test asserted the
				// right hand first time and failed, which was the test being
				// wrong about the design rather than the code being wrong.
				ECataclysmGearSlot SwappedInto = ECataclysmGearSlot::Weapon1;
				Equipment->Equip(
					CataclysmWeaponMeshTest::Plain(
						CataclysmWeaponMeshTest::OneHandedBase),
					Removed, AlsoRemoved, SwappedInto);
				Player->RefreshWeaponMeshes();

				const FCataclysmItem* Worn = Equipment->EquippedAt(SwappedInto);
				if (TestNotNull(TEXT("the sword is worn after the swap"), Worn))
				{
					TestEqual(TEXT("and it is the sword"), Worn->Base,
						FName(CataclysmWeaponMeshTest::OneHandedBase));
				}

				// THE HAND THAT MATCHES THE SLOT IT ACTUALLY WENT TO.
				const UStaticMeshComponent* Holding =
					CataclysmWeaponMeshTest::HandNamed(Player,
						SwappedInto == ECataclysmGearSlot::Weapon1
							? TEXT("RightHandWeapon") : TEXT("LeftHandWeapon"));

				if (TestNotNull(TEXT("the hand it went to"), Holding))
				{
					TestTrue(TEXT("holds a weapon after the swap"),
						Holding->GetStaticMesh() != nullptr);

					TestTrue(TEXT("and it is not the axe"),
						Holding->GetStaticMesh() != Axe);
				}

				// AND THE AXE IS GONE FROM BOTH SLOTS. This is the half that
				// would break silently: a swap that drew the new weapon without
				// clearing the old one leaves the character holding both.
				const FCataclysmItem* First =
					Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
				const FCataclysmItem* Second =
					Equipment->EquippedAt(ECataclysmGearSlot::Weapon2);
				const FName Axed = TEXT("Weapon_Greataxe");

				TestFalse(TEXT("the two-handed axe is no longer worn"),
					(First && First->Base == Axed)
					|| (Second && Second->Base == Axed));
			}
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmWeaponMeshTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmTwoHandedUsesOneHandTest,
	"Cataclysm.WeaponMesh.ATwoHandedWeaponLeavesTheOffHandEmpty")
{
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmWeaponMeshTest::SpawnPlayer(World);
	if (TestNotNull(TEXT("a player"), Player))
	{
		if (CataclysmWeaponMeshTest::WeaponArtIsPresent())
		{
			ECataclysmGearSlot WentTo = ECataclysmGearSlot::Weapon1;
			const FName Worn = CataclysmWeaponMeshTest::Wear(
				Player, CataclysmWeaponMeshTest::TwoHandedBase, WentTo);

			TestEqual(TEXT("the greatsword is worn in the first weapon slot"),
				Worn, FName(CataclysmWeaponMeshTest::TwoHandedBase));

			const UStaticMeshComponent* Right =
				CataclysmWeaponMeshTest::HandNamed(Player, TEXT("RightHandWeapon"));
			const UStaticMeshComponent* Left =
				CataclysmWeaponMeshTest::HandNamed(Player, TEXT("LeftHandWeapon"));

			if (TestNotNull(TEXT("the right hand"), Right)
				&& TestNotNull(TEXT("the left hand"), Left))
			{
				TestTrue(TEXT("a two-handed weapon is drawn in the right hand"),
					Right->GetStaticMesh() != nullptr);

				// A LIMITATION, WRITTEN DOWN AS ONE. UCataclysmEquipmentComponent
				// puts a two-handed weapon in Weapon1 and blocks Weapon2, so the
				// left hand has nothing to draw. Holding it in both hands needs
				// a two-handed grip pose, and nothing this project owns has one.
				TestTrue(TEXT("and the off hand stays empty"),
					Left->GetStaticMesh() == nullptr);
			}
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmWeaponMeshTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}


CATACLYSM_TEST(FCataclysmPlantedWeaponLeavesTheHandTest,
	"Cataclysm.WeaponMesh.AWeaponLeftInTheGroundIsNotDrawnInTheHand")
{
	// THE VISIBLE HALF OF "YOU FIGHT UNARMED UNTIL YOU DO". The Greatsword's
	// Buried Fire leaves the sword standing in the ground, and nothing is
	// unequipped -- the item stays in Weapon1 the whole time -- so without this
	// the character would go on holding a greatsword the player can see is
	// somewhere else. Issue #1141.
	//
	// AND THE HALF THAT WOULD BREAK SILENTLY IS THE SECOND ONE. Hands that empty
	// and never fill again look exactly like a skill that worked, until the
	// player pulls the sword free and finds they are still holding nothing.
	// `ACataclysmPlantedWeapon::EndPlay` lets go of its caster before asking for
	// the redraw for that reason, and this is what says so.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}

	ACataclysmPlayerCharacter* Player = CataclysmWeaponMeshTest::SpawnPlayer(World);
	if (TestNotNull(TEXT("a player"), Player))
	{
		if (CataclysmWeaponMeshTest::WeaponArtIsPresent())
		{
			ECataclysmGearSlot WentTo = ECataclysmGearSlot::Weapon1;
			const FName Worn = CataclysmWeaponMeshTest::Wear(
				Player, CataclysmWeaponMeshTest::TwoHandedBase, WentTo);

			TestEqual(TEXT("the greatsword is worn to begin with"),
				Worn, FName(CataclysmWeaponMeshTest::TwoHandedBase));

			UStaticMeshComponent* Right =
				CataclysmWeaponMeshTest::HandNamed(Player, TEXT("RightHandWeapon"));

			if (TestNotNull(TEXT("the right hand component"), Right))
			{
				TestTrue(TEXT("it starts by holding something"),
					Right->GetStaticMesh() != nullptr);

				// DRIVEN THROUGH THE ACTOR RATHER THAN THROUGH THE SKILL,
				// because what is being checked is the drawing. What plants the
				// sword in play is UCataclysmStrikeSkill and
				// CataclysmSkillTemplateTests.cpp covers that.
				ACataclysmPlantedWeapon* Sword = ACataclysmPlantedWeapon::Plant(
					Player, Player->GetActorLocation(), TEXT("Greatsword"),
					/*InFire=*/nullptr, /*InMorePerSecond=*/12.0f);

				if (TestNotNull(TEXT("the sword went into the ground"), Sword))
				{
					// THE ITEM IS STILL WORN, which is what makes this a real
					// test rather than a test of unequipping. Without it, empty
					// hands would only mean the greatsword had come off.
					TestNotNull(TEXT("and the greatsword is still worn"),
						Player->GetEquipment()->EquippedAt(WentTo));

					TestTrue(TEXT("but the hand is empty while it stands there"),
						Right->GetStaticMesh() == nullptr);

					Sword->Destroy();

					TestTrue(TEXT("and it is back in the hand once it is gone"),
						Right->GetStaticMesh() != nullptr);
				}
			}
		}
		else
		{
			CataclysmTestSkip::ReportSkippedHalf(
				*this, CataclysmWeaponMeshTest::NoArtReason());
		}
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
