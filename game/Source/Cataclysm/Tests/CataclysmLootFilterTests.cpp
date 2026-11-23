// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmLootFilter.h"
#include "Misc/ScopeExit.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The loot filter, issue #2846, slice LF1: rules that hide a drop's name or restyle it, the owner's default "Hide worse
 * than worn", the Alt reveal and the three built-in filters.
 *
 * WHAT IS NOT COVERED, said plainly: the drawing. `ACataclysmHUD::DrawDropNames` asks `UCataclysmLootFilterRules::
 * VisibleDrops` which names to draw and draws each with its verdict, and nothing inside a draw call runs under -nullrhi
 * (#650). Nor does a test press Alt: the controller's two key handlers set the flag `SetRevealingHiddenDropsForTest`
 * sets. `tools/tests/test_the_loot_filter_is_wired_in.py` holds that the display and the controller make those calls.
 */
namespace CataclysmLootFilterTest
{
	using FFilter = UCataclysmLootFilterRules;

	/** A piece of the given rarity and base: as many affixes and enchantments as the rarity is made of. */
	FCataclysmItem Piece(ECataclysmRarity Rarity, const TCHAR* Base)
	{
		FCataclysmItem Item;
		Item.Base = FName(Base);
		Item.EnchantmentCount = UCataclysmItemValues::EnchantmentsFor(Rarity);
		for (int32 Index = 0; Index < UCataclysmItemValues::AffixSlotsFor(Rarity); ++Index)
		{
			FCataclysmRolledAffix Rolled;
			Rolled.Affix = FName(*FString::Printf(TEXT("Affix_%d"), Index));
			Rolled.Tier = 1;
			Item.Affixes.Add(Rolled);
		}
		return Item;
	}

	/** The facts of a piece of gear in a slot, written out, so a rule is tested without a drop or a table. */
	FCataclysmLootFacts Gear(ECataclysmRarity Rarity, const TCHAR* Slot)
	{
		FCataclysmLootFacts Facts;
		Facts.Kind = ECataclysmLootKind::Gear;
		Facts.Rarity = Rarity;
		Facts.Slot = Slot;
		return Facts;
	}

	/** Every slot empty, as `WornRaritiesOf` takes them. */
	TArray<const FCataclysmItem*> NothingWorn()
	{
		TArray<const FCataclysmItem*> Worn;
		Worn.Init(nullptr, UCataclysmEquipmentComponent::SlotCount);
		return Worn;
	}

	/** A context at tier 1 with these worn rarities. */
	FCataclysmLootContext Wearing(const TArray<const FCataclysmItem*>& Worn, bool bTwoHanded = false)
	{
		FCataclysmLootContext Context;
		Context.WornRarity = FFilter::WornRaritiesOf(Worn, bTwoHanded);
		return Context;
	}

	/** The rarity worn in a slot as a number, or -1 when nothing is read there: an enum compared as its rung. */
	int32 RarityIn(const FCataclysmLootContext& Context, const TCHAR* Slot)
	{
		const ECataclysmRarity* Found = Context.WornRarity.Find(Slot);
		return Found != nullptr ? static_cast<int32>(*Found) : -1;
	}

	int32 Rung(ECataclysmRarity Rarity)
	{
		return static_cast<int32>(Rarity);
	}

	bool Hidden(const FCataclysmLootFilter& Filter, const FCataclysmLootFacts& Facts,
				const FCataclysmLootContext& Context)
	{
		return FFilter::VerdictFor(Filter, Facts, Context).bHidden;
	}

	/** A gear drop lying at a place, its item set and a name given, as the pickup tests build one. */
	ACataclysmDroppedItem* DropAt(UWorld* World, const FVector& Where, const FCataclysmItem& Item, const TCHAR* Name)
	{
		ACataclysmDroppedItem* Drop = World->SpawnActor<ACataclysmDroppedItem>(Where, FRotator::ZeroRotator);
		if (Drop)
		{
			Drop->Item = Item;
			Drop->DisplayName = Name;
		}
		return Drop;
	}
}

// ---------------------------------------------------------------------------
// LF1. The owner's default: a Superb helm worn hides Everyday and Quality helms
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterWornHelmTest,
	"Cataclysm.LootFilter.ASuperbHelmWornHidesEverydayAndQualityHelmsAndShowsASuperbOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner, 2026-11-22: "if you had a superb helm, it would hide trash and quality helms." Strictly below: the same
 * rarity shows. CONTROL: a Quality chest piece shows, because no chest is worn, and a helm is not judged by the chest.
 */
bool FCataclysmLootFilterWornHelmTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterTest;
	const FCataclysmItem Helm = Piece(ECataclysmRarity::Superb, TEXT("Head_Helm"));
	TArray<const FCataclysmItem*> Worn = NothingWorn();
	Worn[static_cast<int32>(ECataclysmGearSlot::Head)] = &Helm;
	const FCataclysmLootContext Context = Wearing(Worn);
	const FCataclysmLootFilter Default = FFilter::HideWorseThanWorn();

	TestEqual(TEXT("the worn helm is read as Superb"),
			  RarityIn(Context, TEXT("Head")), Rung(ECataclysmRarity::Superb));
	TestTrue(TEXT("**an Everyday helm is hidden**"), Hidden(Default, Gear(ECataclysmRarity::Everyday, TEXT("Head")), Context));
	TestTrue(TEXT("**a Quality helm is hidden**"), Hidden(Default, Gear(ECataclysmRarity::Quality, TEXT("Head")), Context));
	TestFalse(TEXT("**a Superb helm shows: strictly below only**"),
			  Hidden(Default, Gear(ECataclysmRarity::Superb, TEXT("Head")), Context));
	TestFalse(TEXT("a Masterful helm shows"), Hidden(Default, Gear(ECataclysmRarity::Masterful, TEXT("Head")), Context));
	TestFalse(TEXT("CONTROL: a Quality chest shows, no chest being worn"),
			  Hidden(Default, Gear(ECataclysmRarity::Quality, TEXT("Chest")), Context));
	TestFalse(TEXT("CONTROL: with nothing worn, an Everyday helm shows"),
			  Hidden(Default, Gear(ECataclysmRarity::Everyday, TEXT("Head")), Wearing(NothingWorn())));
	TestEqual(TEXT("the default filter's name"), Default.Name, FString(TEXT("Hide worse than worn")));
	return true;
}

// ---------------------------------------------------------------------------
// LF2. Rings: the lowest ring worn decides, and an empty ring slot hides nothing
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterRingsTest,
	"Cataclysm.LootFilter.RingsAreJudgedByTheLowestRingWorn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The owner: "Rings would be the only exception as it would have to be based off your lowest ring tier." Seven
 * Legendary rings and one Superb: a Quality ring is hidden, a Superb and a Masterful ring show. With the Superb ring
 * taken off, one ring slot is empty and no ring is hidden (an empty slot hides nothing).
 */
bool FCataclysmLootFilterRingsTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterTest;
	const FCataclysmItem Legendary = Piece(ECataclysmRarity::Legendary, TEXT("Ring_Band"));
	const FCataclysmItem Superb = Piece(ECataclysmRarity::Superb, TEXT("Ring_Band"));
	TArray<const FCataclysmItem*> Worn = NothingWorn();
	const TArray<ECataclysmGearSlot>& Rings = UCataclysmGearSlots::RingSlots();
	if (!TestEqual(TEXT("set-up: eight ring slots"), Rings.Num(), 8))
	{
		return false;
	}
	for (const ECataclysmGearSlot Slot : Rings)
	{
		Worn[static_cast<int32>(Slot)] = &Legendary;
	}
	Worn[static_cast<int32>(Rings.Last())] = &Superb;
	const FCataclysmLootFilter Default = FFilter::HideWorseThanWorn();
	const FCataclysmLootContext Context = Wearing(Worn);

	TestEqual(TEXT("**the rings are read at the lowest worn, Superb**"),
			  RarityIn(Context, TEXT("Ring")), Rung(ECataclysmRarity::Superb));
	TestTrue(TEXT("**a Quality ring is hidden**"), Hidden(Default, Gear(ECataclysmRarity::Quality, TEXT("Ring")), Context));
	TestFalse(TEXT("**a Superb ring shows**"), Hidden(Default, Gear(ECataclysmRarity::Superb, TEXT("Ring")), Context));
	TestFalse(TEXT("**a Masterful ring shows, below the seven Legendary rings but not below the lowest**"),
			  Hidden(Default, Gear(ECataclysmRarity::Masterful, TEXT("Ring")), Context));

	Worn[static_cast<int32>(Rings.Last())] = nullptr;
	const FCataclysmLootContext OneEmpty = Wearing(Worn);
	TestFalse(TEXT("**one ring slot empty: no ring rarity is read**"), OneEmpty.WornRarity.Contains(TEXT("Ring")));
	TestFalse(TEXT("**and an Everyday ring shows**"),
			  Hidden(Default, Gear(ECataclysmRarity::Everyday, TEXT("Ring")), OneEmpty));
	return true;
}

// ---------------------------------------------------------------------------
// LF3. Weapons: the lower hand decides; one two-handed weapon decides alone
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterWeaponsTest,
	"Cataclysm.LootFilter.WeaponsAreJudgedByTheLowerHandAndATwoHanderByItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The coordinating session's settlement of 2026-11-22: weapons work as rings do. A Masterful and a Superb weapon:
 * Quality is hidden, Superb shows. One Legendary two-handed weapon, the second slot empty and blocked: Masterful is
 * hidden. CONTROL: the same one weapon and an empty hand that is NOT a two-hander's: nothing is hidden.
 */
bool FCataclysmLootFilterWeaponsTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterTest;
	const FCataclysmItem Masterful = Piece(ECataclysmRarity::Masterful, TEXT("Weapon_Sword"));
	const FCataclysmItem Superb = Piece(ECataclysmRarity::Superb, TEXT("Weapon_Dagger"));
	const FCataclysmItem Legendary = Piece(ECataclysmRarity::Legendary, TEXT("Weapon_Sword"));
	const TArray<ECataclysmGearSlot>& Hands = UCataclysmGearSlots::WeaponSlots();
	if (!TestEqual(TEXT("set-up: two weapon slots"), Hands.Num(), 2))
	{
		return false;
	}
	const FCataclysmLootFilter Default = FFilter::HideWorseThanWorn();

	TArray<const FCataclysmItem*> Two = NothingWorn();
	Two[static_cast<int32>(Hands[0])] = &Masterful;
	Two[static_cast<int32>(Hands[1])] = &Superb;
	const FCataclysmLootContext BothHands = Wearing(Two);
	TestTrue(TEXT("**two weapons: a Quality weapon is hidden**"),
			 Hidden(Default, Gear(ECataclysmRarity::Quality, TEXT("Weapon")), BothHands));
	TestFalse(TEXT("**a Superb weapon shows: the lower hand decides**"),
			  Hidden(Default, Gear(ECataclysmRarity::Superb, TEXT("Weapon")), BothHands));

	TArray<const FCataclysmItem*> One = NothingWorn();
	One[static_cast<int32>(Hands[0])] = &Legendary;
	TestTrue(TEXT("**a two-handed weapon decides alone: a Masterful weapon is hidden**"),
			 Hidden(Default, Gear(ECataclysmRarity::Masterful, TEXT("Weapon")), Wearing(One, /*bTwoHanded=*/true)));
	TestFalse(TEXT("CONTROL: one one-handed weapon and an empty hand hide nothing"),
			  Hidden(Default, Gear(ECataclysmRarity::Everyday, TEXT("Weapon")), Wearing(One, /*bTwoHanded=*/false)));
	return true;
}

// ---------------------------------------------------------------------------
// LF4. Every condition must match, and the highest matching rule decides
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterRulesTest,
	"Cataclysm.LootFilter.EveryConditionMustMatchAndTheHighestMatchingRuleDecides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Each condition narrows on its own, and two in one rule must both hold. A Show rule above a Hide rule keeps a drop it
 * matches, and the same rules the other way round hide it, as in Last Epoch. A drop no rule matches is shown, with no
 * rule named.
 */
bool FCataclysmLootFilterRulesTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterTest;
	const FCataclysmLootContext Context;

	FCataclysmLootFacts Helm = Gear(ECataclysmRarity::Superb, TEXT("Head"));
	Helm.GearLevel = 4;
	FCataclysmRolledAffix Evasion;
	Evasion.Affix = FName(TEXT("Flat_Evasion"));
	Evasion.Tier = 5;
	FCataclysmRolledAffix Armor;
	Armor.Affix = FName(TEXT("Flat_Armor"));
	Armor.Tier = 2;
	Helm.Affixes = {Evasion, Armor};
	Helm.SocketCount = 2;

	FCataclysmLootFilterRule Rule;
	TestTrue(TEXT("a rule with no condition matches"), FFilter::Matches(Rule, Helm, Context));

	Rule.Rarities = {ECataclysmRarity::Superb};
	TestTrue(TEXT("rarity Superb matches"), FFilter::Matches(Rule, Helm, Context));
	Rule.Slots = {TEXT("Chest")};
	TestFalse(TEXT("**rarity AND slot: the slot fails, so the rule fails**"), FFilter::Matches(Rule, Helm, Context));
	Rule.Slots = {TEXT("Head")};
	TestTrue(TEXT("both hold"), FFilter::Matches(Rule, Helm, Context));

	FCataclysmLootFilterRule Level;
	Level.MinGearLevel = 5;
	TestFalse(TEXT("gear level 5 and up: +4 fails"), FFilter::Matches(Level, Helm, Context));
	Level.MinGearLevel = 4;
	TestTrue(TEXT("gear level 4 and up: +4 holds"), FFilter::Matches(Level, Helm, Context));

	FCataclysmLootFilterRule Affixes;
	Affixes.Affixes = {FName(TEXT("Flat_Evasion")), FName(TEXT("Flat_Armor"))};
	Affixes.MinAffixTier = 3;
	TestTrue(TEXT("one of the two at tier 3 or above"), FFilter::Matches(Affixes, Helm, Context));
	Affixes.MinAffixCount = 2;
	TestFalse(TEXT("**two of them at tier 3: armor is tier 2, so it fails**"), FFilter::Matches(Affixes, Helm, Context));
	Affixes.MinAffixTier = 2;
	TestTrue(TEXT("two of them at tier 2 holds"), FFilter::Matches(Affixes, Helm, Context));

	FCataclysmLootFilterRule Enchanted;
	Enchanted.HasEnchantment = ECataclysmLootTriState::Yes;
	TestFalse(TEXT("an enchantment wanted, none carried"), FFilter::Matches(Enchanted, Helm, Context));
	Enchanted.HasEnchantment = ECataclysmLootTriState::No;
	TestTrue(TEXT("no enchantment wanted, none carried"), FFilter::Matches(Enchanted, Helm, Context));

	FCataclysmLootFilterRule Sockets;
	Sockets.MinSockets = 3;
	TestFalse(TEXT("three sockets wanted, two held"), FFilter::Matches(Sockets, Helm, Context));

	FCataclysmLootFacts Sword = Gear(ECataclysmRarity::Quality, TEXT("Weapon"));
	Sword.DamageTypes = {FName(TEXT("Fire")), FName(TEXT("War"))};
	FCataclysmLootFilterRule Fire;
	Fire.DamageTypes = {FName(TEXT("Fire"))};
	TestTrue(TEXT("a weapon carrying Fire"), FFilter::Matches(Fire, Sword, Context));
	TestFalse(TEXT("CONTROL: the helm carries no damage type"), FFilter::Matches(Fire, Helm, Context));

	FCataclysmLootFacts Material;
	Material.Kind = ECataclysmLootKind::Material;
	Material.MaterialTier = 2;
	FCataclysmLootFilterRule HighTiers;
	HighTiers.MinMaterialTier = 3;
	TestFalse(TEXT("materials of tier 3 and up: a tier 2 one fails"), FFilter::Matches(HighTiers, Material, Context));
	TestFalse(TEXT("and gear is not a material"), FFilter::Matches(HighTiers, Helm, Context));

	// THE TIER-RELATIVE CONDITION, ruled (a): at tier 5 the tier's rarity is Legendary, three rungs above Quality.
	FCataclysmLootContext TierFive;
	TierFive.DifficultyTier = 5;
	FCataclysmLootFilterRule Relative;
	Relative.RungsBelowTierAtLeast = 3;
	TestEqual(TEXT("tier 5's rarity is Legendary"), Rung(FFilter::TierRarity(5)), Rung(ECataclysmRarity::Legendary));
	TestTrue(TEXT("**Quality is three rungs below tier 5's**"), FFilter::Matches(Relative, Sword, TierFive));
	TestFalse(TEXT("**Superb is two rungs below, not three**"), FFilter::Matches(Relative, Helm, TierFive));

	// ORDER: a Show above a Hide keeps the helm; the other way round hides it.
	FCataclysmLootFilterRule ShowSuperb;
	ShowSuperb.Rarities = {ECataclysmRarity::Superb};
	FCataclysmLootFilterRule HideAllGear;
	HideAllGear.Action = ECataclysmLootFilterAction::Hide;
	HideAllGear.Kind = ECataclysmLootKind::Gear;
	FCataclysmLootFilter ShowFirst;
	ShowFirst.Rules = {ShowSuperb, HideAllGear};
	FCataclysmLootFilter HideFirst;
	HideFirst.Rules = {HideAllGear, ShowSuperb};
	TestFalse(TEXT("**a Show rule above the Hide rule keeps the helm**"), Hidden(ShowFirst, Helm, Context));
	TestEqual(TEXT("and names rule 0"), FFilter::VerdictFor(ShowFirst, Helm, Context).Rule, 0);
	TestTrue(TEXT("**the Hide rule above hides it**"), Hidden(HideFirst, Helm, Context));
	TestTrue(TEXT("the Quality sword falls to the Hide rule below the Show"), Hidden(ShowFirst, Sword, Context));

	FCataclysmLootFilter OnlyChests;
	FCataclysmLootFilterRule Chests;
	Chests.Action = ECataclysmLootFilterAction::Hide;
	Chests.Slots = {TEXT("Chest")};
	OnlyChests.Rules = {Chests};
	const FCataclysmLootVerdict Unmatched = FFilter::VerdictFor(OnlyChests, Helm, Context);
	TestTrue(TEXT("**no rule matches: shown as it always was, no rule named**"),
			 !Unmatched.bHidden && Unmatched.Rule == INDEX_NONE && Unmatched.Scale == 1.0f
				 && Unmatched.TextColour == INDEX_NONE && Unmatched.BackgroundColour == INDEX_NONE);

	// THE LIMIT: a rule past the 75th is not read.
	FCataclysmLootFilter Long;
	Long.Rules.Init(Chests, FFilter::MaxRules);
	Long.Rules.Add(HideAllGear);
	TestFalse(TEXT("**the 76th rule is not read**"), Hidden(Long, Helm, Context));
	TestEqual(TEXT("the limit is Last Epoch's 75"), FFilter::MaxRules, 75);
	return true;
}

// ---------------------------------------------------------------------------
// LF5. A quantity is never hidden, and a restyle sets colour, background and size
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterRestyleTest,
	"Cataclysm.LootFilter.AQuantityIsNeverHiddenAndARestyleSetsColourBackgroundAndSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Hide rule with no condition hides gear and leaves materials, gold and gems shown: they are collected whether named
 * or not. A restyle carries its palette entries and its size; an index outside the palette is dropped, and a size past
 * the last is held to 1.5.
 */
bool FCataclysmLootFilterRestyleTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterTest;
	const FCataclysmLootContext Context;
	FCataclysmLootFilterRule HideAll;
	HideAll.Action = ECataclysmLootFilterAction::Hide;
	FCataclysmLootFilter Filter;
	Filter.Rules = {HideAll};

	TestTrue(TEXT("gear is hidden"), Hidden(Filter, Gear(ECataclysmRarity::Everyday, TEXT("Head")), Context));
	for (const ECataclysmLootKind Kind : {ECataclysmLootKind::Material, ECataclysmLootKind::Gold, ECataclysmLootKind::Gem})
	{
		FCataclysmLootFacts Quantity;
		Quantity.Kind = Kind;
		const FCataclysmLootVerdict Verdict = FFilter::VerdictFor(Filter, Quantity, Context);
		TestTrue(FString::Printf(TEXT("**kind %d is matched and still shown**"), static_cast<int32>(Kind)),
				 !Verdict.bHidden && Verdict.Rule == 0);
	}

	FCataclysmLootFilterRule Restyle;
	Restyle.Action = ECataclysmLootFilterAction::Restyle;
	Restyle.TextColour = 8;
	Restyle.BackgroundColour = 11;
	Restyle.Size = 2;
	Filter.Rules = {Restyle};
	const FCataclysmLootVerdict Styled = FFilter::VerdictFor(Filter, Gear(ECataclysmRarity::Quality, TEXT("Head")), Context);
	TestEqual(TEXT("text colour: palette 8, the first extra"), Styled.TextColour, 8);
	TestEqual(TEXT("background: palette 11, black"), Styled.BackgroundColour, 11);
	TestEqual(TEXT("size 2 is 1.5 times"), Styled.Scale, 1.5f);
	TestFalse(TEXT("a restyled drop is shown"), Styled.bHidden);

	Filter.Rules[0].TextColour = 12;
	Filter.Rules[0].Size = 7;
	const FCataclysmLootVerdict Held = FFilter::VerdictFor(Filter, Gear(ECataclysmRarity::Quality, TEXT("Head")), Context);
	TestEqual(TEXT("an index outside the palette keeps the name's colour"), Held.TextColour, INDEX_NONE);
	TestEqual(TEXT("a size past the last is held to 1.5"), Held.Scale, 1.5f);

	TestEqual(TEXT("the palette holds twelve"), FFilter::PaletteSize, 12);
	TestTrue(TEXT("palette 11 is black"), FFilter::PaletteColour(11, nullptr).Equals(FLinearColor::Black));
	TestTrue(TEXT("a rarity's entry with no table is white"), FFilter::PaletteColour(0, nullptr).Equals(FLinearColor::White));
	return true;
}

// ---------------------------------------------------------------------------
// LF6. The three built-in filters
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterBuiltInsTest,
	"Cataclysm.LootFilter.TheThreeBuiltInFilters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Everything hides nothing. Strict hides every Everyday and Quality piece and what the default hides: with Legendary
 * rings worn it hides a Masterful ring as the default does. CONTROL: Strict shows a Superb helm with nothing worn. The
 * three are found by name, ignoring case, and nothing else is.
 */
bool FCataclysmLootFilterBuiltInsTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterTest;
	const FCataclysmLootContext Bare;
	const FCataclysmLootFilter Everything = FFilter::Everything();
	const FCataclysmLootFilter Strict = FFilter::Strict();

	TestEqual(TEXT("Everything has no rules"), Everything.Rules.Num(), 0);
	TestFalse(TEXT("Everything shows an Everyday helm"),
			  Hidden(Everything, Gear(ECataclysmRarity::Everyday, TEXT("Head")), Bare));
	TestTrue(TEXT("**Strict hides an Everyday helm with nothing worn**"),
			 Hidden(Strict, Gear(ECataclysmRarity::Everyday, TEXT("Head")), Bare));
	TestTrue(TEXT("**and a Quality one**"), Hidden(Strict, Gear(ECataclysmRarity::Quality, TEXT("Head")), Bare));
	TestFalse(TEXT("CONTROL: Strict shows a Superb helm with nothing worn"),
			  Hidden(Strict, Gear(ECataclysmRarity::Superb, TEXT("Head")), Bare));

	const FCataclysmItem Ring = Piece(ECataclysmRarity::Legendary, TEXT("Ring_Band"));
	TArray<const FCataclysmItem*> Worn = NothingWorn();
	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::RingSlots())
	{
		Worn[static_cast<int32>(Slot)] = &Ring;
	}
	TestTrue(TEXT("**Strict also hides what the default hides: a Masterful ring under Legendary rings**"),
			 Hidden(Strict, Gear(ECataclysmRarity::Masterful, TEXT("Ring")), Wearing(Worn)));

	FCataclysmLootFilter Found;
	TestTrue(TEXT("\"strict\" is found"), FFilter::BuiltInNamed(TEXT("strict"), Found) && Found.Name == TEXT("Strict"));
	TestTrue(TEXT("\"HIDE WORSE THAN WORN\" is found"),
			 FFilter::BuiltInNamed(TEXT("HIDE WORSE THAN WORN"), Found) && Found.Name == FFilter::DefaultFilterName);
	TestTrue(TEXT("\"Everything\" is found"), FFilter::BuiltInNamed(TEXT("Everything"), Found));
	TestFalse(TEXT("\"Regular\" is not a built-in"), FFilter::BuiltInNamed(TEXT("Regular"), Found));
	return true;
}

// ---------------------------------------------------------------------------
// LF7. A hidden drop has no name until Alt is held
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterRevealTest,
	"Cataclysm.LootFilter.AHiddenDropHasNoNameUntilAltIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Three helm drops lying in a world, Everyday, Quality and Superb, read through the base table for their slot, with a
 * Superb helm worn. Without the reveal key two are left out; with it all three are kept and the two still say hidden,
 * so they are drawn faintly. The hidden drops are still in the world either way (the owner's ruling O3).
 */
bool FCataclysmLootFilterRevealTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	ACataclysmDroppedItem* Everyday =
		DropAt(World, FVector(100.0f, 0.0f, 0.0f), Piece(ECataclysmRarity::Everyday, TEXT("Head_Helm")), TEXT("Everyday"));
	ACataclysmDroppedItem* Quality =
		DropAt(World, FVector(200.0f, 0.0f, 0.0f), Piece(ECataclysmRarity::Quality, TEXT("Head_Hood")), TEXT("Quality"));
	ACataclysmDroppedItem* Superb =
		DropAt(World, FVector(300.0f, 0.0f, 0.0f), Piece(ECataclysmRarity::Superb, TEXT("Head_Helm")), TEXT("Superb"));
	if (!TestTrue(TEXT("set-up: the base table and three drops"), Bases && Everyday && Quality && Superb))
	{
		return false;
	}
	TestEqual(TEXT("a helm drop's slot is read from the base table as Head"),
			  FFilter::FactsOf(*Everyday, Bases).Slot, FString(TEXT("Head")));

	const FCataclysmItem Helm = Piece(ECataclysmRarity::Superb, TEXT("Head_Helm"));
	TArray<const FCataclysmItem*> Worn = NothingWorn();
	Worn[static_cast<int32>(ECataclysmGearSlot::Head)] = &Helm;
	const FCataclysmLootContext Context = Wearing(Worn);
	const TArray<ACataclysmDroppedItem*> Named = {Everyday, Quality, Superb};

	TArray<ACataclysmDroppedItem*> Shown;
	TArray<FCataclysmLootVerdict> Verdicts;
	FFilter::VisibleDrops(Named, FFilter::HideWorseThanWorn(), Context, /*bRevealHidden=*/false, Bases, Shown, Verdicts);
	TestEqual(TEXT("**without Alt, one name: the Superb helm**"), Shown.Num(), 1);
	TestTrue(TEXT("and it is the Superb one"), Shown.Num() == 1 && Shown[0] == Superb);

	FFilter::VisibleDrops(Named, FFilter::HideWorseThanWorn(), Context, /*bRevealHidden=*/true, Bases, Shown, Verdicts);
	TestEqual(TEXT("**with Alt held, all three**"), Shown.Num(), 3);
	TestTrue(TEXT("**the two hidden ones still say hidden, so they are drawn faintly**"),
			 Verdicts.Num() == 3 && Verdicts[0].bHidden && Verdicts[1].bHidden && !Verdicts[2].bHidden);
	TestTrue(TEXT("the hidden drops are still in the world"), IsValid(Everyday) && IsValid(Quality));
	return true;
}

// ---------------------------------------------------------------------------
// LF8. The context reads what a character wears now
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmLootFilterContextTest,
	"Cataclysm.LootFilter.TheContextReadsWhatTheCharacterWearsNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A possessed player with a Superb helm put on: its context reads Head as Superb. The helm taken off and a Masterful
 * one put on, the context read again says Masterful, so a change of gear changes what is hidden the next time the
 * display asks. CONTROL: no character, no worn rarity.
 */
bool FCataclysmLootFilterContextTest::RunTest(const FString&)
{
	using namespace CataclysmLootFilterTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player state, a controller and a character"), State && Controller && Character))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestNotNull(TEXT("set-up: its equipment"), Equipment))
	{
		return false;
	}

	FCataclysmItem Removed;
	FCataclysmItem AlsoRemoved;
	Equipment->EquipInto(Piece(ECataclysmRarity::Superb, TEXT("Head_Helm")), ECataclysmGearSlot::Head, Removed,
						 AlsoRemoved);
	const FCataclysmLootContext First = FFilter::ContextOf(Character);
	TestEqual(TEXT("**the worn helm is read as Superb**"),
			  RarityIn(First, TEXT("Head")), Rung(ECataclysmRarity::Superb));

	Equipment->EquipInto(Piece(ECataclysmRarity::Masterful, TEXT("Head_Helm")), ECataclysmGearSlot::Head, Removed,
						 AlsoRemoved);
	const FCataclysmLootContext Second = FFilter::ContextOf(Character);
	TestEqual(TEXT("**after the change it is read as Masterful**"),
			  RarityIn(Second, TEXT("Head")), Rung(ECataclysmRarity::Masterful));
	TestTrue(TEXT("**so a Superb helm drop is now hidden**"),
			 Hidden(FFilter::HideWorseThanWorn(), Gear(ECataclysmRarity::Superb, TEXT("Head")), Second));

	TestEqual(TEXT("CONTROL: no character, no worn rarity"), FFilter::ContextOf(nullptr).WornRarity.Num(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
