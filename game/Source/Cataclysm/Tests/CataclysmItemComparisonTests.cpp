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
#include "Items/CataclysmForge.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPowerScore.h"
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
 * AND WHICH RING, the owner's report of 2026-11-24 ("it just compares with ring 1 no matter what"), ruled by the
 * coordinating session: with every ring slot full, the worn ring of the hovered ring's own rarity, the lowest Power Score
 * among several and the lowest slot among equals (tests 2 and 2b); with none of its rarity worn, the lowest rarity worn
 * (test 2c); a free slot still first (test 2); and the two hands by the same rule (test 2d). Each against a real `Equip`
 * as its control, since the comparison asks `Equip`.
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

	/** An item's whole name, as its tooltip's first line writes it: empty for a piece with no rarity. */
	FString WholeNameOf(const FCataclysmItem& Item)
	{
		return UCataclysmItemName::NameOf(Item, UCataclysmItemModifiers::LoadBaseTable(),
										  UCataclysmDropRoll::LoadAffixTable());
	}

	/** The heading a comparison result is given, with the tables the tooltip reads. */
	FString HeadingOf(const FCataclysmComparisonResult& Result)
	{
		return UCataclysmItemComparison::HeadingFor(Result, UCataclysmItemModifiers::LoadBaseTable(),
													UCataclysmDropRoll::LoadAffixTable());
	}

	/** Real flat affixes `game/Data/Affixes.csv` allows on a ring: two prefixes, then two suffixes. */
	const TCHAR* const RingAffixRows[] = {TEXT("Stat_Flat_maximum_health"), TEXT("Stat_Flat_maximum_mana"),
										  TEXT("Stat_Flat_health_regeneration"), TEXT("Stat_Flat_mana_regeneration")};

	/** Real flat affixes `game/Data/Affixes.csv` allows on a weapon: a prefix, then three suffixes. */
	const TCHAR* const WeaponAffixRows[] = {TEXT("Stat_Flat_damage"), TEXT("Stat_Flat_life_leech"),
											TEXT("Stat_Flat_critical_strike_chance"), TEXT("Stat_Flat_penetration")};

	/**
	 * A piece of a rarity from Everyday to Masterful at a gear level: one to four of `Rows` and no enchantment, which is
	 * what those four rarities are (`UCataclysmItemValues::RarityOf`). MARKED BY ITS RESIDUE, so a test can say which
	 * piece came off.
	 */
	FCataclysmItem OfRarity(const TCHAR* Base, const TCHAR* const* Rows, ECataclysmRarity Rarity, int32 GearLevel,
							float Mark)
	{
		FCataclysmItem Item = PlainOf(Base, GearLevel);
		const int32 Affixes = FMath::Min(UCataclysmItemValues::AffixSlotsFor(Rarity), 4);
		for (int32 Index = 0; Index < Affixes; ++Index)
		{
			FCataclysmRolledAffix Rolled;
			Rolled.Affix = FName(Rows[Index]);
			Rolled.Tier = UCataclysmItemValues::MaxAffixTier;
			Rolled.Roll = 1.0f;
			Item.Affixes.Add(Rolled);
		}
		Item.Residue = Mark;
		return Item;
	}

	/** A ring of a rarity, at a gear level, marked. */
	FCataclysmItem RingOf(ECataclysmRarity Rarity, int32 GearLevel, float Mark)
	{
		return OfRarity(RingBase, RingAffixRows, Rarity, GearLevel, Mark);
	}

	/** Its rarity, by the definition of rarity, for a set-up check. */
	bool IsOfRarity(const FCataclysmItem& Item, ECataclysmRarity Rarity)
	{
		ECataclysmRarity Found = ECataclysmRarity::Everyday;
		return UCataclysmItemModifiers::RarityOfItem(Item, Found) && Found == Rarity;
	}

	/**
	 * Puts `Rings` on through `Equip`, in order, each into the next free ring slot, so ring N is worn in Ring N. Fails
	 * the test by name, and answers false, when one is not.
	 */
	bool WearRingsInOrder(FAutomationTestBase& Test, const FComparedCharacter& Character,
						  const TArray<FCataclysmItem>& Rings)
	{
		const TArray<ECataclysmGearSlot>& Slots = UCataclysmGearSlots::RingSlots();
		bool bInOrder = Rings.Num() <= Slots.Num();
		for (int32 Index = 0; bInOrder && Index < Rings.Num(); ++Index)
		{
			bInOrder = Character.Wear(Rings[Index]) == Slots[Index];
		}
		return Test.TestTrue(TEXT("set-up: ring N is worn in Ring N"), bInOrder);
	}

	/** What a comparison chose for a hovered piece, and what a real equip of it chose: each slot and what came off. */
	struct FComparedAndEquipped
	{
		FCataclysmComparisonResult Result;
		ECataclysmGearSlot ReallyWent = ECataclysmGearSlot::Count;
		FCataclysmItem ReallyOff;
	};

	/**
	 * `Hovered` compared on `Character`, and really put on through `Equip` on `Control`, the CONTROL: a second character
	 * wearing the same pieces. Neither character changes the other.
	 */
	FComparedAndEquipped CompareAndEquip(const FCataclysmItem& Hovered, const FComparedCharacter& Character,
										 const FComparedCharacter& Control, ACataclysmComparisonStandIn& StandIn)
	{
		FComparedAndEquipped Out;
		Out.Result = UCataclysmItemComparison::Compare(Hovered, *Character.Equipment, Character.AbilitySystem, StandIn);
		FCataclysmItem AlsoOff;
		Control.Equipment->Equip(Hovered, Out.ReallyOff, AlsoOff, Out.ReallyWent);
		return Out;
	}

	/** The mark of the one piece a comparison says would come off, or -1 when it is not exactly one piece. */
	float ReplacedMarkOf(const FCataclysmComparisonResult& Result)
	{
		return Result.Replaced.Num() == 1 ? Result.Replaced[0].Residue : -1.0f;
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
	// THE AFFIX ROW'S OWN TIER 7 TOP AT +10, read from the table since issue #2883 (48 at +0, 120 at +10 today).
	const UDataTable* AffixTable = UCataclysmDropRoll::LoadAffixTable();
	const FCataclysmAffixRow* HealthRow = AffixTable
		? AffixTable->FindRow<FCataclysmAffixRow>(FName(HealthAffix), TEXT("ItemComparisonTests"), false)
		: nullptr;
	if (!TestNotNull(TEXT("set-up: the flat health affix row"), HealthRow))
	{
		return false;
	}
	const float AffixWorth = UCataclysmItemValues::AffixValue(*HealthRow, UCataclysmItemValues::MaxAffixTier, 1.0f, 10);
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
	// A PLAIN HELM HAS NO RARITY, so no whole name (`UCataclysmItemValues::RarityOf` takes no piece of no affix and no
	// enchantment), and it is called by its base's name, "Helm", as its inventory cell calls it -- never "Head_Helm",
	// which the heading said until the item comparison window ran it.
	TestTrue(TEXT("CONTROL: the plain helm has no whole name"), WholeNameOf(Worn).IsEmpty());
	TestEqual(TEXT("the heading calls the plain helm by its base's name"), HeadingOf(Result),
			  FString(TEXT("If worn, in place of Helm:")));
	TestTrue(TEXT("the left column names what it would replace"), Left.Contains(TEXT("If worn, in place of Helm:")));

	// AND A PIECE THAT HAS A RARITY BY ITS WHOLE NAME: the hovered helm, one affix, put where the plain one was.
	FCataclysmComparisonResult Named = Result;
	Named.Replaced = {Hovered};
	TestFalse(TEXT("CONTROL: the helm with an affix has a whole name"), WholeNameOf(Hovered).IsEmpty());
	TestEqual(TEXT("a piece with a rarity is called by its whole name"), HeadingOf(Named),
			  FString::Printf(TEXT("If worn, in place of %s:"), *WholeNameOf(Hovered)));
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
	const FCataclysmItem Hovered = RingOf(ECataclysmRarity::Quality, 0, 99.0f);
	if (!TestTrue(TEXT("set-up: the hovered ring is Quality"), IsOfRarity(Hovered, ECataclysmRarity::Quality)))
	{
		return false;
	}

	// THREE RINGS WORN, ONE OF THEM QUALITY: the fourth slot is free, so it goes there and replaces nothing. A FREE
	// SLOT IS STILL TAKEN FIRST, before the ring of its own rarity.
	{
		FComparedCharacter Character(World);
		FComparedCharacter Control(World);
		const TArray<FCataclysmItem> Worn = {RingOf(ECataclysmRarity::Masterful, 0, 1.0f),
											 RingOf(ECataclysmRarity::Quality, 0, 2.0f),
											 RingOf(ECataclysmRarity::Everyday, 0, 3.0f)};
		if (!WearRingsInOrder(*this, Character, Worn) || !WearRingsInOrder(*this, Control, Worn))
		{
			return false;
		}
		const FComparedAndEquipped Both = CompareAndEquip(Hovered, Character, Control, *StandIn);
		TestTrue(TEXT("CONTROL: a real equip puts it in Ring 4"), Both.ReallyWent == ECataclysmGearSlot::Ring4);
		TestTrue(TEXT("CONTROL: and takes nothing off"), Both.ReallyOff.Base.IsNone());

		const FCataclysmComparisonResult& Result = Both.Result;
		TestTrue(TEXT("three worn: it is compared"), Result.bCompared);
		TestTrue(TEXT("three worn: in the slot the real equip chose"), Result.Slot == Both.ReallyWent);
		TestEqual(TEXT("three worn: nothing would come off, though a Quality ring is worn"), Result.Replaced.Num(), 0);

		FString Left;
		FString Right;
		ColumnsOf(Hovered, Result, Left, Right);
		TestEqual(TEXT("three worn: the right column says it is compared with nothing"), Right,
				  FString(TEXT("Compared with nothing: Ring 4 is empty.")));
		TestTrue(TEXT("three worn: the left column names the slot"), Left.Contains(TEXT("If worn in Ring 4:")));
	}

	// EIGHT RINGS WORN, THE OWNER'S CASE OF 2026-11-24: a Masterful ring in Ring 1, one Quality ring in Ring 3, and an
	// Everyday ring in Ring 6 below both. A Quality ring hovered is compared with the Quality ring, not with Ring 1 --
	// which it was "no matter what" until the report -- and not with the lowest ring either.
	{
		FComparedCharacter Character(World);
		FComparedCharacter Control(World);
		const TArray<FCataclysmItem> Worn = {
			RingOf(ECataclysmRarity::Masterful, 0, 1.0f), RingOf(ECataclysmRarity::Masterful, 0, 2.0f),
			RingOf(ECataclysmRarity::Quality, 0, 3.0f), RingOf(ECataclysmRarity::Superb, 0, 4.0f),
			RingOf(ECataclysmRarity::Superb, 0, 5.0f), RingOf(ECataclysmRarity::Everyday, 0, 6.0f),
			RingOf(ECataclysmRarity::Superb, 0, 7.0f), RingOf(ECataclysmRarity::Masterful, 0, 8.0f)};
		if (!WearRingsInOrder(*this, Character, Worn) || !WearRingsInOrder(*this, Control, Worn))
		{
			return false;
		}
		TestTrue(TEXT("set-up: Ring 1 holds a Masterful ring"), IsOfRarity(Worn[0], ECataclysmRarity::Masterful));

		const FComparedAndEquipped Both = CompareAndEquip(Hovered, Character, Control, *StandIn);
		TestTrue(TEXT("CONTROL: a real equip replaces Ring 3, the Quality ring"),
				 Both.ReallyWent == ECataclysmGearSlot::Ring3);
		TestEqual(TEXT("CONTROL: and takes off the ring marked 3"), Both.ReallyOff.Residue, 3.0f);

		const FCataclysmComparisonResult& Result = Both.Result;
		TestTrue(TEXT("eight worn: it is compared"), Result.bCompared);
		TestTrue(TEXT("eight worn: in the slot the real equip chose"), Result.Slot == Both.ReallyWent);
		TestFalse(TEXT("eight worn: not with Ring 1's Masterful ring"), Result.Slot == ECataclysmGearSlot::Ring1);
		TestEqual(TEXT("eight worn: the ring that would come off is the one the real equip takes off"),
				  ReplacedMarkOf(Result), Both.ReallyOff.Residue);
		// BY THE NAME THE HEADING CALLS A PIECE (`UCataclysmForge::NameOf`), checked first to tell the two rings apart.
		const FString QualityName = UCataclysmForge::NameOf(Worn[2], UCataclysmItemModifiers::LoadBaseTable(),
															UCataclysmDropRoll::LoadAffixTable());
		TestNotEqual(TEXT("CONTROL: the Quality ring and Ring 1's Masterful ring have different names"), QualityName,
					 UCataclysmForge::NameOf(Worn[0], UCataclysmItemModifiers::LoadBaseTable(),
											 UCataclysmDropRoll::LoadAffixTable()));
		TestEqual(TEXT("eight worn: the heading names the Quality ring"), HeadingOf(Result),
				  FString::Printf(TEXT("If worn, in place of %s:"), *QualityName));

		// AND THE TOOLTIP BOTH HOVERS DRAW, the bag's and the ground's (`TooltipColumnsFor`, issue #2807): the worn
		// column is the Quality ring's own tooltip.
		FCataclysmCarriedSlot AsCarried;
		AsCarried.Item = Hovered;
		FString Left;
		FString Right;
		const bool bCompared = UCataclysmItemComparison::TooltipColumnsFor(
			AsCarried, Character.Equipment, Character.AbilitySystem, StandIn, /*WornNow=*/nullptr,
			UCataclysmItemModifiers::LoadBaseTable(), UCataclysmDropRoll::LoadAffixTable(),
			UCataclysmDropRoll::LoadCraftingMaterialTable(), UCataclysmDropRoll::LoadPositiveEnchantmentTable(),
			UCataclysmDropRoll::LoadNegativeEnchantmentTable(), Left, Right);
		TestTrue(TEXT("eight worn: the hover's tooltip compares it"), bCompared);
		TestEqual(TEXT("eight worn: and its worn column is the Quality ring's own tooltip"), Right,
				  FString(TEXT("Worn now:\n")) + OwnLinesOf(Worn[2]));

		AddInfo(FString::Printf(TEXT("RING SAME RARITY: compared with %s, really replaced %s, mark %.0f"),
								*UCataclysmGearSlots::DisplayName(Result.Slot),
								*UCataclysmGearSlots::DisplayName(Both.ReallyWent), Both.ReallyOff.Residue));
	}
	return true;
}

// ---------------------------------------------------------------------------
// 2b. Among rings of its own rarity, the one with the lowest Power Score
// ---------------------------------------------------------------------------

/**
 * Eight rings worn, three of them Quality: Ring 2 at +6, Ring 4 and Ring 6 at +2. A Quality ring hovered is compared
 * with the lowest-scoring Quality ring, and of the two that tie, the lower slot: Ring 4. CONTROLS: the three Quality
 * rings' Power Scores are as the set-up says, a socketed gem raises a ring's score (so the score, not the gear level
 * alone, decides), and a real equip takes off the ring the comparison names. Prints "RING LOWEST SCORE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonRingLowestScore,
	"Cataclysm.ItemComparison.ARingIsComparedWithTheLowestScoringRingOfItsOwnRarity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonRingLowestScore::RunTest(const FString& Parameters)
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

	const TArray<FCataclysmItem> Worn = {
		RingOf(ECataclysmRarity::Masterful, 0, 1.0f), RingOf(ECataclysmRarity::Quality, 6, 2.0f),
		RingOf(ECataclysmRarity::Superb, 0, 3.0f), RingOf(ECataclysmRarity::Quality, 2, 4.0f),
		RingOf(ECataclysmRarity::Everyday, 0, 5.0f), RingOf(ECataclysmRarity::Quality, 2, 6.0f),
		RingOf(ECataclysmRarity::Superb, 0, 7.0f), RingOf(ECataclysmRarity::Masterful, 0, 8.0f)};

	// CONTROLS ON THE SCORE ITSELF: the +6 Quality ring scores more than the two +2 ones, which tie; and a gem in a
	// socket raises a ring's score at the same gear level.
	const float ScoreAtSix = UCataclysmPowerScore::WornPieceScoreOf(Worn[1], ECataclysmGearSlot::Ring2);
	const float ScoreAtTwo = UCataclysmPowerScore::WornPieceScoreOf(Worn[3], ECataclysmGearSlot::Ring4);
	const float ScoreAtTwoToo = UCataclysmPowerScore::WornPieceScoreOf(Worn[5], ECataclysmGearSlot::Ring6);
	TestTrue(TEXT("CONTROL: a Quality ring at +6 scores more than one at +2"), ScoreAtSix > ScoreAtTwo);
	TestEqual(TEXT("CONTROL: the two Quality rings at +2 score the same"), ScoreAtTwoToo, ScoreAtTwo);
	FCataclysmItem Gemmed = Worn[3];
	FCataclysmSocket Socket;
	Socket.Gem = FName(TEXT("Gem_Of_The_Viper"));
	Socket.GemRarity = ECataclysmRarity::Everyday;
	Gemmed.Sockets.Add(Socket);
	TestTrue(TEXT("CONTROL: a socketed gem raises a ring's score"),
			 UCataclysmPowerScore::WornPieceScoreOf(Gemmed, ECataclysmGearSlot::Ring4) > ScoreAtTwo);
	TestEqual(TEXT("CONTROL: a ring with no rarity scores nothing, as the character's score counts it"),
			  UCataclysmPowerScore::WornPieceScoreOf(PlainOf(RingBase, 10), ECataclysmGearSlot::Ring4), 0.0f);

	FComparedCharacter Character(World);
	FComparedCharacter Control(World);
	if (!WearRingsInOrder(*this, Character, Worn) || !WearRingsInOrder(*this, Control, Worn))
	{
		return false;
	}

	const FCataclysmItem Hovered = RingOf(ECataclysmRarity::Quality, 0, 99.0f);
	const FComparedAndEquipped Both = CompareAndEquip(Hovered, Character, Control, *StandIn);
	TestTrue(TEXT("CONTROL: a real equip replaces Ring 4"), Both.ReallyWent == ECataclysmGearSlot::Ring4);
	TestEqual(TEXT("CONTROL: and takes off the ring marked 4"), Both.ReallyOff.Residue, 4.0f);

	const FCataclysmComparisonResult& Result = Both.Result;
	TestTrue(TEXT("it is compared"), Result.bCompared);
	TestTrue(TEXT("**with the lowest-scoring Quality ring, the lower slot of the two that tie: Ring 4**"),
			 Result.Slot == ECataclysmGearSlot::Ring4);
	TestTrue(TEXT("in the slot the real equip chose"), Result.Slot == Both.ReallyWent);
	TestEqual(TEXT("and the ring that would come off is the one the real equip takes off"), ReplacedMarkOf(Result),
			  Both.ReallyOff.Residue);

	AddInfo(FString::Printf(TEXT("RING LOWEST SCORE: Quality +6 %.3f, +2 %.3f and %.3f; compared with %s, really "
								 "replaced %s"),
							ScoreAtSix, ScoreAtTwo, ScoreAtTwoToo, *UCataclysmGearSlots::DisplayName(Result.Slot),
							*UCataclysmGearSlots::DisplayName(Both.ReallyWent)));
	return true;
}

// ---------------------------------------------------------------------------
// 2c. With no ring of its own rarity worn, the lowest rarity worn
// ---------------------------------------------------------------------------

/**
 * Eight rings worn and none of them Quality: two Everyday rings, Ring 3 at +3 and Ring 6 at +1, are the lowest rarity
 * worn -- the loot filter's reference -- and the rest are Superb and Masterful. A Quality ring hovered is compared with
 * the lower-scoring Everyday ring, Ring 6. CONTROL: a real equip takes off the same ring. Prints "RING LOWEST RARITY".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonRingLowestRarity,
	"Cataclysm.ItemComparison.ARingOfARarityNotWornIsComparedWithTheLowestRarityWorn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonRingLowestRarity::RunTest(const FString& Parameters)
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

	const TArray<FCataclysmItem> Worn = {
		RingOf(ECataclysmRarity::Masterful, 0, 1.0f), RingOf(ECataclysmRarity::Masterful, 0, 2.0f),
		RingOf(ECataclysmRarity::Everyday, 3, 3.0f), RingOf(ECataclysmRarity::Masterful, 0, 4.0f),
		RingOf(ECataclysmRarity::Superb, 0, 5.0f), RingOf(ECataclysmRarity::Everyday, 1, 6.0f),
		RingOf(ECataclysmRarity::Superb, 0, 7.0f), RingOf(ECataclysmRarity::Masterful, 0, 8.0f)};
	int32 QualityWorn = 0;
	for (const FCataclysmItem& Ring : Worn)
	{
		QualityWorn += IsOfRarity(Ring, ECataclysmRarity::Quality) ? 1 : 0;
	}
	TestEqual(TEXT("set-up: no Quality ring is worn"), QualityWorn, 0);

	FComparedCharacter Character(World);
	FComparedCharacter Control(World);
	if (!WearRingsInOrder(*this, Character, Worn) || !WearRingsInOrder(*this, Control, Worn))
	{
		return false;
	}

	const FCataclysmItem Hovered = RingOf(ECataclysmRarity::Quality, 0, 99.0f);
	const FComparedAndEquipped Both = CompareAndEquip(Hovered, Character, Control, *StandIn);
	TestTrue(TEXT("CONTROL: a real equip replaces Ring 6"), Both.ReallyWent == ECataclysmGearSlot::Ring6);
	TestEqual(TEXT("CONTROL: and takes off the ring marked 6"), Both.ReallyOff.Residue, 6.0f);

	const FCataclysmComparisonResult& Result = Both.Result;
	TestTrue(TEXT("it is compared"), Result.bCompared);
	TestTrue(TEXT("**with the lower-scoring of the two Everyday rings, the lowest rarity worn: Ring 6**"),
			 Result.Slot == ECataclysmGearSlot::Ring6);
	TestTrue(TEXT("in the slot the real equip chose"), Result.Slot == Both.ReallyWent);
	TestEqual(TEXT("and the ring that would come off is the one the real equip takes off"), ReplacedMarkOf(Result),
			  Both.ReallyOff.Residue);

	AddInfo(FString::Printf(TEXT("RING LOWEST RARITY: compared with %s, really replaced %s, mark %.0f"),
							*UCataclysmGearSlots::DisplayName(Result.Slot),
							*UCataclysmGearSlots::DisplayName(Both.ReallyWent), Both.ReallyOff.Residue));
	return true;
}

// ---------------------------------------------------------------------------
// 2d. Weapons, as rings: the hand of its own rarity, else the lower hand
// ---------------------------------------------------------------------------

/**
 * A Quality sword in Weapon 1 and a Masterful dagger in Weapon 2. A Masterful axe hovered is compared with the dagger,
 * the hand of its own rarity; a Superb axe, whose rarity neither hand holds, with the sword, the lower of the two
 * hands. Until the owner's report of 2026-11-24 both were compared with Weapon 1. CONTROL: a real equip of each takes
 * off the same weapon. Prints "WEAPON HANDS".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonWeaponHands,
	"Cataclysm.ItemComparison.AWeaponIsComparedWithTheHandOfItsOwnRarityElseTheLowerHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonWeaponHands::RunTest(const FString& Parameters)
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

	const FCataclysmItem Sword = OfRarity(SwordBase, WeaponAffixRows, ECataclysmRarity::Quality, 0, 1.0f);
	const FCataclysmItem Dagger = OfRarity(DaggerBase, WeaponAffixRows, ECataclysmRarity::Masterful, 0, 2.0f);
	const TCHAR* AxeBase = TEXT("Weapon_Axe");
	struct FHandCase
	{
		const TCHAR* Name;
		FCataclysmItem Hovered;
		ECataclysmGearSlot Expected;
		float ExpectedMark;
	};
	const FHandCase Cases[] = {
		{TEXT("a Masterful axe, the dagger's rarity"),
		 OfRarity(AxeBase, WeaponAffixRows, ECataclysmRarity::Masterful, 0, 99.0f), ECataclysmGearSlot::Weapon2, 2.0f},
		{TEXT("a Superb axe, which neither hand holds"),
		 OfRarity(AxeBase, WeaponAffixRows, ECataclysmRarity::Superb, 0, 99.0f), ECataclysmGearSlot::Weapon1, 1.0f},
	};

	FString Said;
	for (const FHandCase& Case : Cases)
	{
		FComparedCharacter Character(World);
		FComparedCharacter Control(World);
		const bool bDressed = Character.Wear(Sword) == ECataclysmGearSlot::Weapon1
			&& Character.Wear(Dagger) == ECataclysmGearSlot::Weapon2
			&& Control.Wear(Sword) == ECataclysmGearSlot::Weapon1
			&& Control.Wear(Dagger) == ECataclysmGearSlot::Weapon2;
		if (!TestTrue(TEXT("set-up: the sword in Weapon 1 and the dagger in Weapon 2"), bDressed))
		{
			return false;
		}

		const FComparedAndEquipped Both = CompareAndEquip(Case.Hovered, Character, Control, *StandIn);
		TestTrue(FString::Printf(TEXT("CONTROL: %s: a real equip replaces the expected hand"), Case.Name),
				 Both.ReallyWent == Case.Expected);
		TestEqual(FString::Printf(TEXT("CONTROL: %s: and takes off the expected weapon"), Case.Name),
				  Both.ReallyOff.Residue, Case.ExpectedMark);

		const FCataclysmComparisonResult& Result = Both.Result;
		TestTrue(FString::Printf(TEXT("%s: it is compared"), Case.Name), Result.bCompared);
		TestTrue(FString::Printf(TEXT("**%s: in the expected hand**"), Case.Name), Result.Slot == Case.Expected);
		TestEqual(FString::Printf(TEXT("%s: and the weapon that would come off is the one the real equip takes off"),
								  Case.Name),
				  ReplacedMarkOf(Result), Both.ReallyOff.Residue);
		Said += FString::Printf(TEXT("%s: compared with %s; "), Case.Name,
								*UCataclysmGearSlots::DisplayName(Result.Slot));
	}

	AddInfo(TEXT("WEAPON HANDS: ") + Said);
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
	// PLAIN WEAPONS, CALLED BY THEIR BASES' NAMES, as the helm in the first test is.
	TestTrue(TEXT("the heading names both"), Left.Contains(TEXT("If worn, in place of Sword and Dagger:")));
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

// ---------------------------------------------------------------------------
// 8 and 9. The hit on the run's family's creatures: the coordinating session's ruling of 2026-11-19
// ---------------------------------------------------------------------------

namespace CataclysmItemComparisonTest
{
	const TCHAR* GreataxeBase = TEXT("Weapon_Greataxe");
	const TCHAR* TorcBase = TEXT("Necklace_Torc");

	/** A Torc carrying one T1 "increased damage against <Family> enemies", rolled at the top of its band. */
	FCataclysmItem TorcAgainst(const TCHAR* Family)
	{
		FCataclysmItem Item = PlainOf(TorcBase);
		FCataclysmRolledAffix Rolled;
		Rolled.Affix = FName(FString::Printf(TEXT("Stat_Increased_damage_against_%s_enemies"), Family));
		Rolled.Tier = 1;
		Rolled.Roll = 1.0f;
		Item.Affixes.Add(Rolled);
		return Item;
	}

	/** Whether any of these lines begins with this name. */
	bool AnyLineNamed(const TArray<FString>& Lines, const FString& Name)
	{
		return Lines.ContainsByPredicate([&Name](const FString& Line) { return Line.StartsWith(Name + TEXT(":")); });
	}
}

/**
 * The owner's case of 2026-11-19, a Greataxe +0 and a run of Demonic creatures: a Torc of "increased damage against
 * Demonic enemies" raises the new line, the hit on a Demonic creature's health, and leaves the plain hit where it is;
 * a Torc against Celestial enemies raises neither. Prints "FAMILY HIT".
 *
 * THE FIGURES ARE WORKED OUT HERE AND NOT READ BACK. The Greataxe's 72 attack damage is stated at +10, and a
 * two-handed weapon doubles it: 72 / 2.5 x 2 = 57.6 at +0. The Slashing bonus is 10% more against health: 63.36. A T1
 * affix rolled at the top of its band, with no floor, is its 400% top x 1/3 (T1 is a third of T7) / 2.5 (stated at
 * +10): 53.33%, so the hit becomes 57.6 x 1.5333 x 1.1 = 97.15.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonFamilyHit,
	"Cataclysm.ItemComparison.ANecklaceAgainstTheRunsFamilyRaisesItsLineAndNotThePlainHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonFamilyHit::RunTest(const FString& Parameters)
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
	Character.Wear(PlainOf(GreataxeBase));
	Character.Equipment->RefreshAttributes(Character.AbilitySystem);
	const TArray<FName> Demonic = {FName(TEXT("Demonic"))};

	// THE READING ITSELF, against the figures worked out above.
	const FCataclysmComparisonReading Now = UCataclysmItemComparison::ReadingOf(
		Character.Equipment->GetSlots(), *Character.Equipment, Character.AbilitySystem, *StandIn, &Demonic);
	if (!TestEqual(TEXT("the reading priced the one family it was given"), Now.FamilyHits.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("CONTROL: the Greataxe's plain hit is 72 / 2.5 x 2 = 57.6"), Now.BasicHit, 57.6f, 0.01f);
	TestEqual(TEXT("its hit on a Demonic creature's health is 10% more, Slashing's bonus: 63.36"), Now.FamilyHits[0],
			  63.36f, 0.01f);

	const FCataclysmItem Against = TorcAgainst(TEXT("Demonic"));
	const FCataclysmComparisonResult Raised =
		UCataclysmItemComparison::Compare(Against, *Character.Equipment, Character.AbilitySystem, *StandIn, nullptr,
										  &Demonic);
	TestTrue(TEXT("a Torc against Demonic enemies is compared"), Raised.bCompared);
	TestTrue(TEXT("and the line for the hit on a Demonic creature goes from 63.4 to 97.2"),
			 Raised.ChangeLines.Contains(UCataclysmItemComparison::ChangeLineNamed(
				 UCataclysmItemComparison::FamilyHitLineName(FName(TEXT("Demonic"))), /*bPercentage=*/false, 63.36f,
				 97.152f)));
	TestFalse(TEXT("while the plain hit, which knows no creature, does not move"),
			  AnyLineNamed(Raised.ChangeLines, UCataclysmItemComparison::BasicHitLineName));

	// CONTROL: ANOTHER FAMILY'S AFFIX DOES NOTHING TO A DEMONIC CREATURE.
	const FCataclysmComparisonResult Other = UCataclysmItemComparison::Compare(
		TorcAgainst(TEXT("Celestial")), *Character.Equipment, Character.AbilitySystem, *StandIn, nullptr, &Demonic);
	TestTrue(TEXT("CONTROL: a Torc against Celestial enemies is compared"), Other.bCompared);
	TestFalse(TEXT("CONTROL: and moves no line for a Demonic creature"),
			  AnyLineNamed(Other.ChangeLines,
						   UCataclysmItemComparison::FamilyHitLineName(FName(TEXT("Demonic")))));
	TestFalse(TEXT("CONTROL: nor the plain hit"),
			  AnyLineNamed(Other.ChangeLines, UCataclysmItemComparison::BasicHitLineName));

	AddInfo(FString::Printf(TEXT("FAMILY HIT: plain %.2f, on a Demonic creature %.2f; Demonic Torc: %s; Celestial "
								 "Torc: %s"),
							Now.BasicHit, Now.FamilyHits[0], *FString::Join(Raised.ChangeLines, TEXT(" | ")),
							*FString::Join(Other.ChangeLines, TEXT(" | "))));
	return true;
}

/**
 * The family line counts a Slashing weapon's 10% more against health and no other sub-type's: a Warhammer's (Blunt)
 * hit on a creature equals its plain hit, a Greataxe's (Slashing) is 10% more. And a world with no run names no
 * family, so the comparison shows no family line there. Prints "SUB-TYPE".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmItemComparisonFamilySubType,
	"Cataclysm.ItemComparison.TheFamilyLineCountsASlashingWeaponsBonusAndNoOtherSubType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmItemComparisonFamilySubType::RunTest(const FString& Parameters)
{
	using namespace CataclysmItemComparisonTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	TestEqual(TEXT("a world with no run names no family"),
			  UCataclysmItemComparison::RunFamiliesIn(World).Num(), 0);

	const FName Demonic(TEXT("Demonic"));
	FString Said;
	for (const TCHAR* Base : {TEXT("Weapon_Greataxe"), TEXT("Weapon_Warhammer")})
	{
		FComparedCharacter Character(World);
		Character.Wear(PlainOf(Base));
		Character.Equipment->RefreshAttributes(Character.AbilitySystem);
		const float Plain =
			UCataclysmItemComparison::BasicHitOf(Character.AbilitySystem, *Character.Equipment, FName());
		const float OnDemonic = UCataclysmItemComparison::BasicHitOnAFamilyOf(
			Character.AbilitySystem, *Character.Equipment, FName(), Demonic);
		const bool bSlashing = FCString::Strcmp(Base, TEXT("Weapon_Greataxe")) == 0;

		TestTrue(FString::Printf(TEXT("CONTROL: a %s hits for something"), Base), Plain > 0.0f);
		TestEqual(FString::Printf(TEXT("a %s's hit on a creature's health is its plain hit x %s"), Base,
								  bSlashing ? TEXT("1.1, Slashing's bonus") : TEXT("1, Blunt having none against "
																				 "health")),
				  OnDemonic, Plain * (bSlashing ? 1.1f : 1.0f), 0.01f);
		Said += FString::Printf(TEXT("%s plain %.2f on a Demonic creature %.2f; "), Base, Plain, OnDemonic);
	}

	AddInfo(TEXT("SUB-TYPE: ") + Said);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
