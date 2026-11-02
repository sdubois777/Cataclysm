// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmComparisonStandIn.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Interface/CataclysmItemComparison.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * Tests for a carried item's tooltip comparing it with what wearing it would replace. Issue #832.
 *
 * WHAT THE RULINGS OF 2026-11-02 ASK, and which test holds each: (a) the piece the equip action would replace -- tests
 * 2 and 3, each against a real `Equip` as its control; (b) the character's totals, gathered on a stand-in with nothing
 * written to the character -- tests 1 and 4; (c) a two-handed weapon compared with both weapons it replaces -- test 3;
 * (e) the basic attack's hit, priced by the game's own no-target function -- test 7. Ruling (d), shown on hover with no
 * key held, is the widget's and cannot be watched under `-nullrhi`.
 *
 * THE ITEM BASES ARE REAL ROWS OF `game/Data/ItemBases.csv`, because `Equip` looks the base up to choose a slot, and
 * the comparison asks `Equip`.
 */
namespace CataclysmItemComparisonTest
{
	const TCHAR* HelmBase = TEXT("Head_Helm");
	const TCHAR* BootsBase = TEXT("Boots_Sabatons");
	const TCHAR* RingBase = TEXT("Ring_Band");
	const TCHAR* SwordBase = TEXT("Weapon_Sword");
	const TCHAR* DaggerBase = TEXT("Weapon_Dagger");
	const TCHAR* GreatswordBase = TEXT("Weapon_Greatsword");

	/** A real affix granting flat maximum health, top value 120 at +10 in `game/Data/Affixes.csv`. */
	const TCHAR* HealthAffix = TEXT("Stat_Flat_maximum_health");

	/** An item of a named base, with no affixes. */
	FCataclysmItem PlainOf(const TCHAR* Base, int32 GearLevel = 0)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.GearLevel = GearLevel;
		return Item;
	}

	/** An item carrying one perfectly rolled top-tier flat health affix. */
	FCataclysmItem WithHealthOf(const TCHAR* Base, int32 GearLevel = 0)
	{
		FCataclysmItem Item = PlainOf(Base, GearLevel);
		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(HealthAffix);
		Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
		Rolled.Roll = 1.0f;
		Item.Affixes.Add(Rolled);
		return Item;
	}

	/**
	 * A character: an actor carrying the player's five attribute sets and an equipment component, as the equipment
	 * tests build one. No player state, so a refresh gathers with no build: no attribute points scale anything.
	 */
	struct FComparedCharacter
	{
		explicit FComparedCharacter(UWorld* InWorld)
		{
			Actor = InWorld->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject deduces its type from the argument.
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmPrimaryAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Equipment = NewObject<UCataclysmEquipmentComponent>(Actor);
			Equipment->RegisterComponent();
		}

		~FComparedCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Put an item on through `Equip`, and answer where it went. */
		ECataclysmGearSlot Wear(const FCataclysmItem& Item) const
		{
			FCataclysmItem First;
			FCataclysmItem Second;
			ECataclysmGearSlot Went = ECataclysmGearSlot::Count;
			Equipment->Equip(Item, First, Second, Went);
			return Went;
		}

		/** Refresh the stat line and answer maximum health. */
		float RefreshedMaxHealth() const
		{
			Equipment->RefreshAttributes(AbilitySystem);
			return AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
	};

	/** The stand-in, checked to carry the sets a comparison reads. Null fails the test by name. */
	ACataclysmComparisonStandIn* StandInFor(FAutomationTestBase& Test, UWorld* World)
	{
		ACataclysmComparisonStandIn* StandIn = ACataclysmComparisonStandIn::SpawnIn(World);
		const bool bReady = StandIn && StandIn->GetCataclysmAbilitySystem()
			&& StandIn->GetCataclysmAbilitySystem()->HasAttributeSetForAttribute(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		Test.TestTrue(TEXT("set-up: the stand-in carries the player's attribute sets"), bReady);
		return bReady ? StandIn : nullptr;
	}

	/** The columns, with every table the tooltip reads. */
	void ColumnsOf(const FCataclysmItem& Hovered, const FCataclysmComparisonResult& Result, FString& OutLeft,
				   FString& OutRight)
	{
		FCataclysmCarriedSlot AsCarried;
		AsCarried.Item = Hovered;
		UCataclysmItemComparison::Columns(AsCarried, Result, UCataclysmItemModifiers::LoadBaseTable(),
										  UCataclysmDropRoll::LoadAffixTable(),
										  UCataclysmDropRoll::LoadCraftingMaterialTable(),
										  UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
										  UCataclysmDropRoll::LoadNegativeEnchantmentTable(), OutLeft, OutRight);
	}

	/** An item's own tooltip lines, joined, as the worn column writes each. */
	FString OwnLinesOf(const FCataclysmItem& Item)
	{
		FCataclysmCarriedSlot AsCarried;
		AsCarried.Item = Item;
		return FString::Join(UCataclysmItemTooltip::LinesFor(AsCarried, UCataclysmItemModifiers::LoadBaseTable(),
															 UCataclysmDropRoll::LoadAffixTable(),
															 UCataclysmDropRoll::LoadCraftingMaterialTable(),
															 UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
															 UCataclysmDropRoll::LoadNegativeEnchantmentTable()),
							 TEXT("\n"));
	}

	/** An item's name, as the heading writes it. */
	FString NameOfItem(const FCataclysmItem& Item)
	{
		return UCataclysmItemName::NameOf(Item, UCataclysmItemModifiers::LoadBaseTable(),
										  UCataclysmDropRoll::LoadAffixTable());
	}
}

// ---------------------------------------------------------------------------
// 1. A helm against the helm it would replace
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonHelmAgainstHelm,
	"Cataclysm.ItemComparison.AHelmIsComparedWithTheHelmItWouldReplace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonHelmAgainstHelm::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemComparisonTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A PLAIN HELM WORN AND ONE WITH FLAT HEALTH HOVERED, both at +10, where the affix's stated top value is its value.
	// The two share a base, so their armour implicits match and health is the only total that can move.
	FComparedCharacter Character(World);
	const FCataclysmItem Worn = PlainOf(HelmBase, 10);
	const FCataclysmItem Hovered = WithHealthOf(HelmBase, 10);
	Character.Wear(Worn);
	const float HealthNow = Character.RefreshedMaxHealth();

	// CONTROL: what really wearing it gives, on a second character, and that nothing scales the affix's value.
	FComparedCharacter Control(World);
	Control.Wear(Worn);
	Control.Wear(Hovered);
	const float HealthWith = Control.RefreshedMaxHealth();
	const float AffixWorth = UCataclysmItemValues::AffixValue(120.0f, 0.0f, UCataclysmItemValues::MaxAffixTier, 1.0f, 10);
	TestEqual(TEXT("CONTROL: really wearing it raises maximum health by exactly the affix's value"),
			  HealthWith - HealthNow, AffixWorth, 0.01f);

	ACataclysmComparisonStandIn* StandIn = StandInFor(*this, World);
	if (!StandIn)
	{
		return false;
	}

	const FCataclysmComparisonResult Result =
		UCataclysmItemComparison::Compare(Hovered, *Character.Equipment, Character.AbilitySystem, *StandIn);
	TestTrue(TEXT("it is compared"), Result.bCompared);
	TestTrue(TEXT("with the head slot"), Result.Slot == ECataclysmGearSlot::Head);
	if (TestEqual(TEXT("the plain helm would come off"), Result.Replaced.Num(), 1))
	{
		TestEqual(TEXT("and it is that helm: no affix"), Result.Replaced[0].Affixes.Num(), 0);
	}

	if (TestEqual(TEXT("one total changes"), Result.ChangeLines.Num(), 1))
	{
		TestEqual(TEXT("and it is health, from the figure worn now to the figure really worn"), Result.ChangeLines[0],
				  FString::Printf(TEXT("Health: %s to %s, up %s"), *UCataclysmCharacterSheetLayout::Number(HealthNow),
								  *UCataclysmCharacterSheetLayout::Number(HealthWith),
								  *UCataclysmCharacterSheetLayout::Number(AffixWorth)));
	}

	FString Left;
	FString Right;
	ColumnsOf(Hovered, Result, Left, Right);
	TestTrue(TEXT("the left column names what it would replace"),
			 Left.Contains(FString::Printf(TEXT("If worn, in place of %s:"), *NameOfItem(Worn))));
	TestEqual(TEXT("the right column is the worn helm's own tooltip"), Right,
			  FString(TEXT("Worn now:\n")) + OwnLinesOf(Worn));

	AddInfo(FString::Printf(TEXT("ITEM COMPARISON: helm: %s"), *FString::Join(Result.ChangeLines, TEXT(" | "))));
	return true;
}

// ---------------------------------------------------------------------------
// 2. A ring against the ring the equip action would replace
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonRingAgainstRing,
	"Cataclysm.ItemComparison.ARingIsComparedWithTheRingTheEquipActionWouldReplace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonRingAgainstRing::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemComparisonTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmComparisonStandIn* StandIn = StandInFor(*this, World);
	if (!StandIn)
	{
		return false;
	}
	const FCataclysmItem Hovered = WithHealthOf(RingBase);

	// THREE RINGS WORN: the fourth slot is free, so it goes there and replaces nothing.
	{
		FComparedCharacter Character(World);
		FComparedCharacter Control(World);
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			Character.Wear(PlainOf(RingBase));
			Control.Wear(PlainOf(RingBase));
		}
		const ECataclysmGearSlot ReallyWent = Control.Wear(Hovered);
		TestTrue(TEXT("CONTROL: a real equip puts it in Ring 4"), ReallyWent == ECataclysmGearSlot::Ring4);

		const FCataclysmComparisonResult Result =
			UCataclysmItemComparison::Compare(Hovered, *Character.Equipment, Character.AbilitySystem, *StandIn);
		TestTrue(TEXT("three worn: it is compared"), Result.bCompared);
		TestTrue(TEXT("three worn: in the slot the real equip chose"), Result.Slot == ReallyWent);
		TestEqual(TEXT("three worn: nothing would come off"), Result.Replaced.Num(), 0);

		FString Left;
		FString Right;
		ColumnsOf(Hovered, Result, Left, Right);
		TestEqual(TEXT("three worn: the right column says it is compared with nothing"), Right,
				  FString(TEXT("Compared with nothing: Ring 4 is empty.")));
		TestTrue(TEXT("three worn: the left column names the slot"), Left.Contains(TEXT("If worn in Ring 4:")));
	}

	// EIGHT RINGS WORN: none is free, so `Equip` replaces the first, marked here by its residue.
	{
		FComparedCharacter Character(World);
		FComparedCharacter Control(World);
		for (int32 Ring = 0; Ring < 8; ++Ring)
		{
			FCataclysmItem Worn = PlainOf(RingBase);
			Worn.Residue = Ring == 0 ? 11.0f : 0.0f;
			Character.Wear(Worn);
			Control.Wear(Worn);
		}
		FCataclysmItem ReallyOff;
		FCataclysmItem ReallyAlsoOff;
		ECataclysmGearSlot ReallyWent = ECataclysmGearSlot::Count;
		Control.Equipment->Equip(Hovered, ReallyOff, ReallyAlsoOff, ReallyWent);
		TestTrue(TEXT("CONTROL: a real equip replaces Ring 1"), ReallyWent == ECataclysmGearSlot::Ring1);
		TestEqual(TEXT("CONTROL: and takes off the marked ring"), ReallyOff.Residue, 11.0f);

		const FCataclysmComparisonResult Result =
			UCataclysmItemComparison::Compare(Hovered, *Character.Equipment, Character.AbilitySystem, *StandIn);
		TestTrue(TEXT("eight worn: in the slot the real equip chose"), Result.Slot == ReallyWent);
		if (TestEqual(TEXT("eight worn: one ring would come off"), Result.Replaced.Num(), 1))
		{
			TestEqual(TEXT("eight worn: the one the real equip takes off"), Result.Replaced[0].Residue,
					  ReallyOff.Residue);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// 3. A two-handed weapon against both weapons it would replace
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonTwoHandedAgainstBoth,
	"Cataclysm.ItemComparison.ATwoHandedWeaponIsComparedWithBothWeaponsItWouldReplace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonTwoHandedAgainstBoth::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemComparisonTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmComparisonStandIn* StandIn = StandInFor(*this, World);
	if (!StandIn)
	{
		return false;
	}

	FComparedCharacter Character(World);
	const FCataclysmItem Sword = PlainOf(SwordBase);
	const FCataclysmItem Dagger = PlainOf(DaggerBase);
	TestTrue(TEXT("set-up: the sword is in the first hand"), Character.Wear(Sword) == ECataclysmGearSlot::Weapon1);
	TestTrue(TEXT("set-up: the dagger in the second"), Character.Wear(Dagger) == ECataclysmGearSlot::Weapon2);

	const FCataclysmItem Greatsword = PlainOf(GreatswordBase);
	const FCataclysmComparisonResult Result =
		UCataclysmItemComparison::Compare(Greatsword, *Character.Equipment, Character.AbilitySystem, *StandIn);
	TestTrue(TEXT("it is compared"), Result.bCompared);
	TestTrue(TEXT("in the first weapon slot"), Result.Slot == ECataclysmGearSlot::Weapon1);
	if (TestEqual(TEXT("both weapons would come off"), Result.Replaced.Num(), 2))
	{
		TestEqual(TEXT("the sword first"), Result.Replaced[0].Base, Sword.Base);
		TestEqual(TEXT("then the dagger"), Result.Replaced[1].Base, Dagger.Base);
	}

	FString Left;
	FString Right;
	ColumnsOf(Greatsword, Result, Left, Right);
	TestTrue(TEXT("the heading names both"),
			 Left.Contains(FString::Printf(TEXT("If worn, in place of %s and %s:"), *NameOfItem(Sword),
										   *NameOfItem(Dagger))));
	TestEqual(TEXT("the worn column holds both weapons' own tooltips"), Right,
			  FString(TEXT("Worn now:\n")) + OwnLinesOf(Sword) + TEXT("\n\n") + OwnLinesOf(Dagger));
	return true;
}

// ---------------------------------------------------------------------------
// 4. Asking changes nothing
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonAskingChangesNothing,
	"Cataclysm.ItemComparison.AskingChangesNothingWornAndAnnouncesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonAskingChangesNothing::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemComparisonTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmComparisonStandIn* StandIn = StandInFor(*this, World);
	if (!StandIn)
	{
		return false;
	}

	// A HELM OF 1000 WORN, AND BOOTS OF 1000 HOVERED, with flat health: wearing them would take the Worn Residue to
	// 2000, past tier 1's Consumption Threshold of 1950, so a real equip through the wearer would be asked about.
	FComparedCharacter Character(World);
	FCataclysmItem Helm = PlainOf(HelmBase);
	Helm.Residue = 1000.0f;
	Character.Wear(Helm);
	FCataclysmItem Boots = WithHealthOf(BootsBase);
	Boots.Residue = 1000.0f;
	TestTrue(TEXT("CONTROL: wearing the boots would cross tier 1's threshold"),
			 Character.Equipment->WornResidue() + Boots.Residue
				 >= UCataclysmEquipmentComponent::ConsumptionThresholdFor(1));

	const float HealthBefore = Character.RefreshedMaxHealth();
	const TArray<FCataclysmItem> SlotsBefore = Character.Equipment->GetSlots();
	const int32 ChangesBefore = Character.Equipment->ChangeCount();
	const float ResidueBefore = Character.Equipment->WornResidue();
	int32 Announced = 0;
	Character.Equipment->EquipmentChanged.AddLambda([&Announced] { ++Announced; });

	const FCataclysmComparisonResult Result =
		UCataclysmItemComparison::Compare(Boots, *Character.Equipment, Character.AbilitySystem, *StandIn);
	TestTrue(TEXT("it is compared"), Result.bCompared);

	int32 SlotsChanged = 0;
	const TArray<FCataclysmItem>& SlotsAfter = Character.Equipment->GetSlots();
	for (int32 Index = 0; Index < SlotsBefore.Num(); ++Index)
	{
		if (!SlotsAfter.IsValidIndex(Index) || SlotsAfter[Index].Base != SlotsBefore[Index].Base
			|| SlotsAfter[Index].Residue != SlotsBefore[Index].Residue)
		{
			++SlotsChanged;
		}
	}
	TestEqual(TEXT("no slot changed by asking"), SlotsChanged, 0);
	TestEqual(TEXT("and the Worn Residue is what it was"), Character.Equipment->WornResidue(), ResidueBefore);
	TestEqual(TEXT("nor was the change count raised"), Character.Equipment->ChangeCount(), ChangesBefore);
	TestEqual(TEXT("and no listener heard of it"), Announced, 0);
	TestEqual(TEXT("and the character's maximum health is what it was"),
			  Character.AbilitySystem->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()),
			  HealthBefore);
	return true;
}

// ---------------------------------------------------------------------------
// 5. The lines
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonOnlyWhatMoves,
	"Cataclysm.ItemComparison.OnlyTheTotalsThatMoveAreListedAndEachSaysWhichWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonOnlyWhatMoves::RunTest(const FString& Parameters)
{
	using FComparison = UCataclysmItemComparison;

	TestEqual(TEXT("a rise says up, and by how much"), FComparison::ChangeLine(TEXT("max_health"), 606.0f, 726.0f),
			  FString(TEXT("Health: 606 to 726, up 120")));
	TestEqual(TEXT("a fall says down"), FComparison::ChangeLine(TEXT("armor"), 1240.0f, 1000.0f),
			  FString(TEXT("Armour: 1240 to 1000, down 240")));
	TestEqual(TEXT("a percentage is written as the sheet writes it"),
			  FComparison::ChangeLine(TEXT("crit_chance"), 5.0f, 7.5f),
			  FString(TEXT("Critical strike chance: 5% to 7.5%, up 2.5%")));
	TestEqual(TEXT("an unchanged total gives no line"), FComparison::ChangeLine(TEXT("max_health"), 606.0f, 606.0f),
			  FString());
	TestEqual(TEXT("nor does a change too small to show"),
			  FComparison::ChangeLine(TEXT("max_health"), 606.0f, 606.01f), FString());
	TestEqual(TEXT("the basic attack's hit is a line of its own"),
			  FComparison::ChangeLineNamed(FComparison::BasicHitLineName, /*bPercentage=*/false, 50.0f, 60.0f),
			  FString(TEXT("Basic attack hit, before the target's defences: 50 to 60, up 10")));

	// TWO READINGS: health and evasion move, the hit moves, and nothing else is listed, in the sheet's order.
	const TArray<FString>& Stats = UCataclysmCharacterSheetLayout::SheetStats();
	FCataclysmComparisonReading Now;
	Now.bRead = true;
	Now.Figures.Init(0.0f, Stats.Num());
	Now.BasicHit = 50.0f;
	FCataclysmComparisonReading With = Now;
	With.BasicHit = 60.0f;
	const int32 Health = Stats.IndexOfByKey(FString(TEXT("max_health")));
	const int32 Evasion = Stats.IndexOfByKey(FString(TEXT("evasion")));
	if (!TestTrue(TEXT("set-up: both are sheet stats"), Health != INDEX_NONE && Evasion != INDEX_NONE))
	{
		return false;
	}
	Now.Figures[Health] = 606.0f;
	With.Figures[Health] = 726.0f;
	With.Figures[Evasion] = 4.5f;

	const TArray<FString> Lines = FComparison::ChangeLines(Now, With);
	if (TestEqual(TEXT("only the three that move are listed"), Lines.Num(), 3))
	{
		TestEqual(TEXT("health first, as on the sheet"), Lines[0], FString(TEXT("Health: 606 to 726, up 120")));
		TestEqual(TEXT("then evasion"), Lines[1], FString(TEXT("Evasion: 0% to 4.5%, up 4.5%")));
		TestEqual(TEXT("then the hit, last"), Lines[2],
				  FString(TEXT("Basic attack hit, before the target's defences: 50 to 60, up 10")));
	}
	return true;
}

// ---------------------------------------------------------------------------
// 7. The basic attack's hit: ruling (e)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonBasicHit,
	"Cataclysm.ItemComparison.AWeaponComparisonListsTheBasicAttacksHitAsTheHitFunctionPricesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonBasicHit::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemComparisonTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmComparisonStandIn* StandIn = StandInFor(*this, World);
	if (!StandIn)
	{
		return false;
	}

	// A SWORD WORN, A GREATSWORD HOVERED; a control character really wearing the greatsword.
	FComparedCharacter Character(World);
	Character.Wear(PlainOf(SwordBase));
	Character.Equipment->RefreshAttributes(Character.AbilitySystem);
	const float HitNow = UCataclysmItemComparison::BasicHitOf(Character.AbilitySystem, *Character.Equipment, FName());

	FComparedCharacter Control(World);
	const FCataclysmItem Greatsword = PlainOf(GreatswordBase);
	Control.Wear(Greatsword);
	Control.Equipment->RefreshAttributes(Control.AbilitySystem);
	const float HitWith = UCataclysmItemComparison::BasicHitOf(Control.AbilitySystem, *Control.Equipment, FName());

	// CONTROL: with nothing worn that a skill's tags or the moment could change, the basic attack is the design's
	// anchor, 100% of weapon damage, so the no-target price is the attack damage attribute.
	TestEqual(TEXT("CONTROL: the sword's hit is its attack damage"), HitNow,
			  Character.AbilitySystem->GetNumericAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute()),
			  0.01f);
	TestTrue(TEXT("CONTROL: the greatsword hits harder"), HitWith > HitNow);

	const FCataclysmComparisonResult Result =
		UCataclysmItemComparison::Compare(Greatsword, *Character.Equipment, Character.AbilitySystem, *StandIn);
	TestEqual(TEXT("the hit now is the worn sword's"), Result.BasicHitNow, HitNow, 0.01f);
	TestEqual(TEXT("the hit with it worn is the greatsword's"), Result.BasicHitWith, HitWith, 0.01f);
	TestTrue(TEXT("and the comparison lists it"),
			 Result.ChangeLines.Contains(UCataclysmItemComparison::ChangeLineNamed(
				 UCataclysmItemComparison::BasicHitLineName, /*bPercentage=*/false, HitNow, HitWith)));

	AddInfo(FString::Printf(TEXT("ITEM COMPARISON: sword to greatsword: %s"),
							*FString::Join(Result.ChangeLines, TEXT(" | "))));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
