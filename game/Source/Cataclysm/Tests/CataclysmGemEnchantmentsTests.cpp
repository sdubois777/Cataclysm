// Copyright Stephen Dubois. All Rights Reserved.

/**
 * The two enchantments that wait on gems. Issue #46, slice G7, ruled by the coordinating session on 2026-11-22.
 *
 *   "Sockets on this equipment fit any gem type": read off the piece when a gem is socketed and when its gems are
 *   applied, never written into a socket as Any (Q1). A gem it let in stays socketed and stops acting while the piece
 *   no longer carries it (Q2, a judgement).
 *
 *   "Gems in this equipment are 50% less effective": each of the piece's gems at half its value, before its bucket,
 *   and no other source of the gem's stat (Q3, derived from Path of Exile's Darkness Enthroned); not rounded (Q4).
 *
 * EVERY TEST READS THE TABLES THE GAME LOADS: the two enchantments' rows in `DT_EnchantmentEffects`, the gems in
 * `DT_Gems` and the bases. A test that granted the two stats by hand would pass with no row in the data, which is the
 * hole `Cataclysm.StatExemption.EveryStatWithNoAttributeIsActuallyRead`'s probes do not close; these do.
 */

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmStatPipeline.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmForgeLayout.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmForge.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Items/CataclysmItem.h"

namespace CataclysmGemEnchantmentsTest
{
	/** The two enchantments' row names, as `row_name` in `tools/generate_datatables.py` derives them from the words. */
	const FName FitsAny(TEXT("Positive_Sockets_on_this_equipment_fit_any_gem_type"));
	const FName LessEffective(TEXT("Negative_Gems_in_this_equipment_are_50_less_effective"));

	/** An enchantment that has nothing to do with gems: what a piece carries once fit-any is replaced. */
	const FName Unrelated(TEXT("Positive_Double_your_energy_shield"));

	const FName OfLife(TEXT("Gem_Of_Life"));		// Defense; max_health increased, 0.25 at Superb
	const FName Viper(TEXT("Gem_Of_The_Viper"));	// Attack; poison_chance flat, 0.6 at Superb

	FCataclysmSocket Socket(ECataclysmSocketType Type, FName Gem = NAME_None)
	{
		FCataclysmSocket Out;
		Out.Type = Type;
		Out.Gem = Gem;
		Out.GemRarity = ECataclysmRarity::Superb;
		return Out;
	}

	/** A piece of `Base` with `Sockets`, drawn with `Positive` and `Negative` at the top of their ranges. */
	FCataclysmItem Piece(const TCHAR* Base, const TArray<FCataclysmSocket>& Sockets, FName Positive,
						 FName Negative = NAME_None)
	{
		FCataclysmItem Out;
		Out.Base = FName(Base);
		Out.Sockets = Sockets;
		FCataclysmRolledEnchantment Rolled;
		Rolled.Positive = Positive;
		Rolled.Negative = Negative;
		Out.Enchantments = {Rolled};
		Out.EnchantmentCount = 1;
		return Out;
	}

	/** The value of the one gem modifier on `Stat` among the piece's modifiers, or -1 when there is none or several. */
	float GemValueOn(const FCataclysmItem& Item, const TCHAR* Stat)
	{
		const TMap<FName, TArray<FCataclysmStatModifier>> Totals = UCataclysmItemModifiers::ModifiersFor(
			Item, UCataclysmItemModifiers::LoadBaseTable(), UCataclysmDropRoll::LoadAffixTable());
		TArray<float> Found;
		if (const TArray<FCataclysmStatModifier>* OnStat = Totals.Find(FName(Stat)))
		{
			for (const FCataclysmStatModifier& Modifier : *OnStat)
			{
				if (Modifier.Source == ECataclysmModifierSource::Gem)
				{
					Found.Add(Modifier.Value);
				}
			}
		}
		return Found.Num() == 1 ? Found[0] : -1.0f;
	}

	/** Socketing `Gem` (Superb, of `GemType`) into socket `Index` of `Item`, with everything it takes held. */
	FString RefusalToSocket(const FCataclysmItem& Item, int32 Index, FName Gem, const TCHAR* GemType)
	{
		return UCataclysmForge::SocketGemOf(Item, Index, Gem, ECataclysmRarity::Superb, GemType, /*GemsHeld=*/1,
											/*AgentsHeld=*/1, /*DifficultyTier=*/1, /*GoldHeld=*/1000000000)
			.Refusal;
	}

	/**
	 * The tables the game loads, and the two shipped rows in the effect table. FALSE, SAYING WHICH IS MISSING, so a test
	 * run before the workbook stage fails at its set-up and not at an assertion that would read as the feature failing.
	 */
	bool TheTablesAndTheRows(FAutomationTestBase& Test)
	{
		const UDataTable* Effects = UCataclysmItemModifiers::LoadEnchantmentEffectTable();
		if (!Test.TestTrue(TEXT("set-up: the generated base, affix, gem and enchantment effect tables"),
						   UCataclysmItemModifiers::LoadBaseTable() && UCataclysmDropRoll::LoadAffixTable()
							   && UCataclysmGems::LoadGeneratedTable() && Effects))
		{
			return false;
		}
		const FCataclysmItem Fits = Piece(TEXT("Weapon_Sword"), {}, FitsAny);
		const FCataclysmItem Less = Piece(TEXT("Head_Helm"), {}, NAME_None, LessEffective);
		return Test.TestTrue(TEXT("set-up: DT_EnchantmentEffects holds the fit-any row (the slice's workbook stage)"),
							 UCataclysmGems::FitsAnyGem(Fits, Effects))
			&& Test.TestEqual(TEXT("set-up: DT_EnchantmentEffects holds the 50% less row (the slice's workbook stage)"),
							  UCataclysmGems::SocketedGemEffectOf(Less, Effects), 0.5f, 0.0001f);
	}
}

// --------------------------------------------------------------------------------------------------------------------
// GE1. A weapon whose sockets fit any gem takes an off-type gem at the Forge; the same weapon without it refuses.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemEnchantmentsForgeTest,
	"Cataclysm.GemEnchantments.AWeaponWhoseSocketsFitAnyGemTakesAnOffTypeGemAtTheForge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A sword with one Attack socket. Carrying "Sockets on this equipment fit any gem type", the Forge takes Of Life, a
 * Defense gem, into it. THE CONTROL: the same sword carrying an unrelated enchantment refuses it, naming the socket's
 * own type, which the enchantment never changed (Q1). Prints "FIT ANY FORGE".
 */
bool FCataclysmGemEnchantmentsForgeTest::RunTest(const FString&)
{
	using namespace CataclysmGemEnchantmentsTest;
	if (!TheTablesAndTheRows(*this))
	{
		return false;
	}
	const FCataclysmItem Enchanted = Piece(TEXT("Weapon_Sword"), {Socket(ECataclysmSocketType::Attack)}, FitsAny);
	const FCataclysmItem Plain = Piece(TEXT("Weapon_Sword"), {Socket(ECataclysmSocketType::Attack)}, Unrelated);
	const FString Taken = RefusalToSocket(Enchanted, 0, OfLife, TEXT("Defense"));
	const FString Refused = RefusalToSocket(Plain, 0, OfLife, TEXT("Defense"));
	AddInfo(FString::Printf(TEXT("FIT ANY FORGE: enchanted [%s]; plain [%s]"), *Taken, *Refused));

	TestEqual(TEXT("the enchanted sword's Attack socket takes a Defense gem"), Taken, FString());
	TestEqual(TEXT("control: the plain sword's refuses it, by the socket's own type"), Refused,
			  FString(TEXT("A Defense gem does not fit an Attack socket.")));
	TestTrue(TEXT("and the enchanted sword's socket is still an Attack socket (Q1: read, never written)"),
			 Enchanted.Sockets[0].Type == ECataclysmSocketType::Attack);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GE2. "50% less effective" halves each gem in its own piece, before its bucket, and nothing else.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemEnchantmentsHalfTest,
	"Cataclysm.GemEnchantments.LessEffectiveHalvesEachGemInItsOwnPieceOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A helm with Of Life (Superb, 25 increased maximum health) in its Defense socket, carrying "Gems in this equipment
 * are 50% less effective": 12.5, still increased (Q3), unrounded (Q4). THE CONTROL: the same helm without it gives 25,
 * since the drawback is read off the piece that carries it. A greataxe's Of The Viper (Superb, flat 60) is halved to 30 and not doubled, as a two-handed weapon's gems never are
 * (Q7). Prints "LESS EFFECTIVE".
 */
bool FCataclysmGemEnchantmentsHalfTest::RunTest(const FString&)
{
	using namespace CataclysmGemEnchantmentsTest;
	if (!TheTablesAndTheRows(*this))
	{
		return false;
	}
	const FCataclysmItem Halved =
		Piece(TEXT("Head_Helm"), {Socket(ECataclysmSocketType::Defense, OfLife)}, NAME_None, LessEffective);
	const FCataclysmItem Whole = Piece(TEXT("Head_Helm"), {Socket(ECataclysmSocketType::Defense, OfLife)}, Unrelated);
	const FCataclysmItem Greataxe =
		Piece(TEXT("Weapon_Greataxe"), {Socket(ECataclysmSocketType::Attack, Viper)}, NAME_None, LessEffective);
	const float Half = GemValueOn(Halved, TEXT("max_health"));
	const float Full = GemValueOn(Whole, TEXT("max_health"));
	const float Axe = GemValueOn(Greataxe, TEXT("poison_chance"));
	AddInfo(FString::Printf(TEXT("LESS EFFECTIVE: helm %.3f; plain helm %.3f; greataxe %.3f"), Half, Full, Axe));

	TestEqual(TEXT("Of Life in the helm that carries it: 12.5, half its Superb 25"), Half, 12.5f, 0.0001f);
	TestEqual(TEXT("control: Of Life in a helm without it: 25"), Full, 25.0f, 0.0001f);
	TestEqual(TEXT("Of The Viper in a greataxe that carries it: 30, half of 60 and not doubled"), Axe, 30.0f, 0.0001f);
	const TMap<FName, TArray<FCataclysmStatModifier>> Totals = UCataclysmItemModifiers::ModifiersFor(
		Halved, UCataclysmItemModifiers::LoadBaseTable(), UCataclysmDropRoll::LoadAffixTable());
	const TArray<FCataclysmStatModifier>* Health = Totals.Find(FName(TEXT("max_health")));
	TestTrue(TEXT("still in the increased bucket, the gem's own (Q3)"),
			 Health && Health->ContainsByPredicate([](const FCataclysmStatModifier& Modifier)
			 {
				 return Modifier.Source == ECataclysmModifierSource::Gem
					 && Modifier.Bucket == ECataclysmStatBucket::Increased;
			 }));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GE3. A gem the enchantment let in stays socketed and stops acting when the enchantment is replaced.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemEnchantmentsReplacedTest,
	"Cataclysm.GemEnchantments.AGemTheEnchantmentLetInStopsActingWhenItIsReplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A sword with Of Life (Superb) in its Attack socket. While it carries "Sockets on this equipment fit any gem type",
 * Of Life adds 25 increased maximum health. With an unrelated enchantment in its place it adds nothing, and the Forge
 * still unsockets it (Q2, a judgement). THE CONTROL: Of The Viper in the same sword's Attack socket, which fits it by
 * type, acts either way. Prints "REPLACED".
 */
bool FCataclysmGemEnchantmentsReplacedTest::RunTest(const FString&)
{
	using namespace CataclysmGemEnchantmentsTest;
	if (!TheTablesAndTheRows(*this))
	{
		return false;
	}
	const TArray<FCataclysmSocket> Sockets = {Socket(ECataclysmSocketType::Attack, OfLife),
											  Socket(ECataclysmSocketType::Attack, Viper)};
	const FCataclysmItem While = Piece(TEXT("Weapon_Sword"), Sockets, FitsAny);
	const FCataclysmItem After = Piece(TEXT("Weapon_Sword"), Sockets, Unrelated);
	const float LifeWhile = GemValueOn(While, TEXT("max_health"));
	const float LifeAfter = GemValueOn(After, TEXT("max_health"));
	const float ViperAfter = GemValueOn(After, TEXT("poison_chance"));
	const FString Unsocket = UCataclysmForge::UnsocketGemOf(After, 0, /*DustHeld=*/1, /*bRoomForTheGem=*/true,
															 /*DifficultyTier=*/1, /*GoldHeld=*/1000000000).Refusal;
	AddInfo(FString::Printf(TEXT("REPLACED: Of Life while %.3f, after %.3f; Of The Viper after %.3f; unsocket [%s]"),
							LifeWhile, LifeAfter, ViperAfter, *Unsocket));

	TestEqual(TEXT("Of Life acts while the sword's sockets fit any gem: 25"), LifeWhile, 25.0f, 0.0001f);
	TestEqual(TEXT("and adds nothing once the enchantment is replaced"), LifeAfter, -1.0f);
	TestTrue(TEXT("while staying in its socket"), After.Sockets[0].Gem == OfLife);
	TestEqual(TEXT("and the Forge still unsockets it"), Unsocket, FString());
	TestEqual(TEXT("control: Of The Viper, which fits the Attack socket, acts after: 60"), ViperAfter, 60.0f, 0.0001f);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GE4. The tooltip's socket lines say which sockets fit any gem and which gem is inactive.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemEnchantmentsLinesTest,
	"Cataclysm.GemEnchantments.TheSocketLinesSayWhatFitsAndWhatActs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A sword with Of Life (Superb) in its Attack socket and an empty Defense socket. Carrying "Sockets on this equipment
 * fit any gem type": "Socket for any gem (enchanted): Of Life (Superb)" and "Socket for any gem (enchanted): empty".
 * With an unrelated enchantment: "Attack socket: Of Life (Superb) (does not fit: inactive)" and "Defense socket:
 * empty" (Q8). Prints "SOCKET LINES".
 */
bool FCataclysmGemEnchantmentsLinesTest::RunTest(const FString&)
{
	using namespace CataclysmGemEnchantmentsTest;
	if (!TheTablesAndTheRows(*this))
	{
		return false;
	}
	const TArray<FCataclysmSocket> Sockets = {Socket(ECataclysmSocketType::Attack, OfLife),
											  Socket(ECataclysmSocketType::Defense)};
	const TArray<FString> While = UCataclysmGems::SocketLinesFor(Piece(TEXT("Weapon_Sword"), Sockets, FitsAny));
	const TArray<FString> After = UCataclysmGems::SocketLinesFor(Piece(TEXT("Weapon_Sword"), Sockets, Unrelated));
	AddInfo(FString::Printf(TEXT("SOCKET LINES: [%s] [%s]"), *FString::Join(While, TEXT(" | ")),
							*FString::Join(After, TEXT(" | "))));

	TestEqual(TEXT("enchanted: two lines"), While.Num(), 2);
	TestEqual(TEXT("enchanted"), FString::Join(While, TEXT(" | ")),
			  FString(TEXT("Socket for any gem (enchanted): Of Life (Superb) | Socket for any gem (enchanted): empty")));
	TestEqual(TEXT("replaced"), FString::Join(After, TEXT(" | ")),
			  FString(TEXT("Attack socket: Of Life (Superb) (does not fit: inactive) | Defense socket: empty")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GE5. Both on one two-handed piece: any gem goes in, the Forge screen offers it, and it acts at half (Q7).
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemEnchantmentsBothTest,
	"Cataclysm.GemEnchantments.BothOnOnePieceTakeAnyGemAtHalfAndTheForgeScreenOffersIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A greataxe with one Defense socket, carrying both enchantments. Of The Viper, an Attack gem, is offered for it on
 * the Forge screen from a bag holding one ("Socket Of The Viper (Superb) into socket 1 (any gem)"), is taken by the
 * Forge, and once in, adds 30: half its Superb 60, not doubled. THE CONTROL: the same greataxe carrying only the 50%
 * less row is offered nothing, refuses the gem, and a Viper already in its Defense socket adds nothing. Prints "BOTH".
 */
bool FCataclysmGemEnchantmentsBothTest::RunTest(const FString&)
{
	using namespace CataclysmGemEnchantmentsTest;
	if (!TheTablesAndTheRows(*this))
	{
		return false;
	}
	UCataclysmInventoryComponent* Bag = NewObject<UCataclysmInventoryComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("set-up: a bag"), Bag)
		|| !TestEqual(TEXT("set-up: one Superb Of The Viper in it"), Bag->AddGems(Viper, ECataclysmRarity::Superb, 1), 0))
	{
		return false;
	}
	const FCataclysmItem Both =
		Piece(TEXT("Weapon_Greataxe"), {Socket(ECataclysmSocketType::Defense)}, FitsAny, LessEffective);
	const FCataclysmItem LessOnly =
		Piece(TEXT("Weapon_Greataxe"), {Socket(ECataclysmSocketType::Defense)}, NAME_None, LessEffective);
	const TArray<FCataclysmForgeSocketAction> Offered = UCataclysmForgeLayout::SocketActionsFor(Both, Bag);
	const TArray<FCataclysmForgeSocketAction> NotOffered = UCataclysmForgeLayout::SocketActionsFor(LessOnly, Bag);
	const FString Taken = RefusalToSocket(Both, 0, Viper, TEXT("Attack"));
	const FString Refused = RefusalToSocket(LessOnly, 0, Viper, TEXT("Attack"));
	FCataclysmItem BothFilled = Both;
	BothFilled.Sockets[0].Gem = Viper;
	FCataclysmItem LessOnlyFilled = LessOnly;
	LessOnlyFilled.Sockets[0].Gem = Viper;
	const float Acting = GemValueOn(BothFilled, TEXT("poison_chance"));
	const float Inactive = GemValueOn(LessOnlyFilled, TEXT("poison_chance"));
	AddInfo(FString::Printf(TEXT("BOTH: offered %d [%s]; control offered %d; taken [%s]; refused [%s]; acting %.3f; "
								 "inactive %.3f"),
							Offered.Num(), Offered.Num() ? *Offered[0].Label : TEXT(""), NotOffered.Num(), *Taken,
							*Refused, Acting, Inactive));

	if (TestEqual(TEXT("the Forge screen offers the Viper for the Defense socket"), Offered.Num(), 1))
	{
		TestEqual(TEXT("as a socket for any gem"), Offered[0].Label,
				  FString(TEXT("Socket Of The Viper (Superb) into socket 1 (any gem)")));
	}
	TestEqual(TEXT("control: without fit-any it offers nothing"), NotOffered.Num(), 0);
	TestEqual(TEXT("the Forge takes it"), Taken, FString());
	TestEqual(TEXT("control: without fit-any it refuses"), Refused,
			  FString(TEXT("An Attack gem does not fit a Defense socket.")));
	TestEqual(TEXT("in, it adds 30: half of 60, not doubled"), Acting, 30.0f, 0.0001f);
	TestEqual(TEXT("control: in the Defense socket of the greataxe without fit-any, it adds nothing"), Inactive, -1.0f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
