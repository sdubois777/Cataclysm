// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmForge.h"

#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Empire/CataclysmSeedMix.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDropRoll.h"
// A gold amount as a player reads it, "2,055 gold", issue #2830.
#include "Items/CataclysmDroppedItem.h"
// A socket's type and a gem's name, issue #46 slice G4.
#include "Items/CataclysmGems.h"
#include "Items/CataclysmWearing.h"
#include "Misc/Crc.h"

const TCHAR* UCataclysmForge::SettingAgentRow = TEXT("Material_Jeweler_s_Setting_Agent");
const TCHAR* UCataclysmForge::OperationsTableAssetPath = TEXT("/Game/Data/DT_ForgeOperations.DT_ForgeOperations");
const TCHAR* UCataclysmForge::UpgradeOperationRow = TEXT("Operation_Upgrade_Item_Level_1_Tier");
const TCHAR* UCataclysmForge::SocketGemOperationRow = TEXT("Operation_Socket_Gem");
const TCHAR* UCataclysmForge::UnsocketGemOperationRow = TEXT("Operation_Unsocket_Gem");
const TCHAR* UCataclysmForge::CombineOperationRow = TEXT("Operation_Combine");
const TCHAR* UCataclysmForge::DismantlingDustRow = TEXT("Material_Dismantling_Dust");
const TCHAR* UCataclysmForge::ShatteredCoreRow = TEXT("Material_Shattered_Core");
const TCHAR* UCataclysmForge::FocusingLensRow = TEXT("Material_Focusing_Lens");
const TCHAR* UCataclysmForge::AddSocketOperationRow = TEXT("Operation_Add_Socket");
const TCHAR* UCataclysmForge::ChangeSocketTypeOperationRow = TEXT("Operation_Change_Socket_Type");
const TCHAR* UCataclysmForge::AetherialShardRow = TEXT("Material_Aetherial_Shard");
const TCHAR* UCataclysmForge::RegretEssenceRow = TEXT("Material_Regret_Essence");
const TCHAR* UCataclysmForge::ImprintOperationRow = TEXT("Operation_Imprint_Enchantment");
const TCHAR* UCataclysmForge::ChangeNegativeOperationRow = TEXT("Operation_Change_Enchantment_Negative");
const TCHAR* UCataclysmForge::CorruptedMoteRow = TEXT("Material_Corrupted_Mote");
const TCHAR* UCataclysmForge::PrimalSparkRow = TEXT("Material_Primal_Spark");
const TCHAR* UCataclysmForge::PotencyCrystalRow = TEXT("Material_Potency_Crystal");
const TCHAR* UCataclysmForge::RerollAffixValueOperationRow = TEXT("Operation_Reroll_Affix_Value");
const TCHAR* UCataclysmForge::PerfectRollOperationRow = TEXT("Operation_Perfect_Roll_Attempt");
const TCHAR* UCataclysmForge::UpgradeAffixTierOperationRow = TEXT("Operation_Upgrade_Affix_Tier");
const TCHAR* UCataclysmForge::SchematicFragmentsRow = TEXT("Material_Schematic_Fragments");
const TCHAR* UCataclysmForge::CrystalOfInstabilityRow = TEXT("Material_Crystal_of_Instability");
const TCHAR* UCataclysmForge::DeterministicAffixOperationRow = TEXT("Operation_Deterministic_Affix");
const TCHAR* UCataclysmForge::AffixShiftingOperationRow = TEXT("Operation_Affix_Shifting_Stat_Type");
const TCHAR* UCataclysmForge::ElementalCinderRow = TEXT("Material_Elemental_Cinder");
const TCHAR* UCataclysmForge::SovereignIngotRow = TEXT("Material_Sovereign_Ingot");
const TCHAR* UCataclysmForge::PrismaticCatalystRow = TEXT("Material_Prismatic_Catalyst");
const TCHAR* UCataclysmForge::InfuseRandomDamageTypeOperationRow = TEXT("Operation_Infuse_Random_Damage_Type");
const TCHAR* UCataclysmForge::InfuseSpecificDamageTypeOperationRow = TEXT("Operation_Infuse_Specific_Damage_Type");
const TCHAR* UCataclysmForge::ChangeDamageTypeOperationRow = TEXT("Operation_Change_Damage_Type");
const TCHAR* UCataclysmForge::CreationWeaponRefusal =
	TEXT("The weapon made at creation carries the damage type chosen for the character; the forge does not change its "
		 "damage types.");

namespace
{
	/** Whether an affix row may roll on a slot: its comma separated `AllowedSlots`, as `CandidatesFor` reads it. */
	bool ForgeAffixAllowedOn(const FCataclysmAffixRow& Row, const FString& Slot)
	{
		TArray<FString> Allowed;
		Row.AllowedSlots.ParseIntoArray(Allowed, TEXT(","), true);
		for (const FString& Each : Allowed)
		{
			if (Each.TrimStartAndEnd().Equals(Slot, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	/** A resistance family covering some of the eight damage types, which chooses which when it rolls. */
	bool ForgeChoosesDamageTypes(const FCataclysmAffixRow& Row)
	{
		return Row.AffixKind.Equals(TEXT("Resistance"), ESearchCase::IgnoreCase) && Row.Breadth > 0
			&& Row.Breadth < UCataclysmItemModifiers::DamageTypeNames().Num();
	}

	/** The damage types whose resistance group the item does not hold, in the design's order. */
	TArray<FName> ForgeUnheldDamageTypes(const UDataTable* AffixTable, const FCataclysmAffixRow& Row,
										 const TSet<FString>& Held)
	{
		TArray<FName> Out;
		for (const FName& Type : UCataclysmItemModifiers::DamageTypeNames())
		{
			TSet<FString> Groups;
			UCataclysmDropRoll::GroupsOf(AffixTable, Row, {Type}, Groups);
			if (Groups.Intersect(Held).Num() == 0)
			{
				Out.Add(Type);
			}
		}
		return Out;
	}
}

FName UCataclysmForge::StoneFor(int32 Level)
{
	// THE ROW KEYS OF `game/Data/CraftingMaterials.csv`: a stone is named for the level it takes an item to.
	if (Level < 1 || Level > HighestStone)
	{
		return NAME_None;
	}
	return FName(*FString::Printf(TEXT("Material_Upgrade_Stone_%d"), Level));
}

int32 UCataclysmForge::BaseGoldFor(int32 BaseDays)
{
	return GoldPerCraftDay * FMath::Max(1, BaseDays);
}

int64 UCataclysmForge::GoldFor(int32 BaseGold, float Residue, int32 DifficultyTier)
{
	// THE SHEET'S MULTIPLIER, NOT ROUNDED, on the residue the item carries as the craft begins, as the time penalty
	// reads it. A residue below nothing is read as nothing.
	const double Multiplier = static_cast<double>(FMath::Max(0.0f, Residue)) / ResiduePerGoldStep + 1.0;
	const double Gold = static_cast<double>(BaseGold) * UCataclysmDropRoll::GoldScaleFor(DifficultyTier) * Multiplier;
	return FMath::Max<int64>(1, static_cast<int64>(FMath::RoundToDouble(Gold)));
}

int64 UCataclysmForge::CombineGoldFor(int32 Rung, int32 DifficultyTier)
{
	// DOUBLED FOR EACH RUNG, as each rung is two of the one below; held inside the eight rarities and ten stones.
	const double AtTheRung = static_cast<double>(BaseGoldOf(CombineOperationRow, CombineGoldAtTheLowestRung))
		* FMath::Pow(2.0, FMath::Clamp(Rung, 0, 9));
	return FMath::Max<int64>(
		1, static_cast<int64>(FMath::RoundToDouble(AtTheRung * UCataclysmDropRoll::GoldScaleFor(DifficultyTier))));
}

const UDataTable* UCataclysmForge::LoadOperationsTable()
{
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, OperationsTableAssetPath);
	if (Table == nullptr)
	{
		UE_LOG(LogCataclysm, Error,
			   TEXT("Could not load %s. It is produced by tools/generate_datatable_assets.py from game/Data/"
					"ForgeOperations.csv, which tools/generate_datatables.py produces from the Crafting sheet's "
					"operations table in docs/All_Things_Cataclysm.xlsx."),
			   OperationsTableAssetPath);
	}
	return Table;
}

int32 UCataclysmForge::BaseGoldOf(const TCHAR* Operation, int32 IfMissing)
{
	const UDataTable* Table = LoadOperationsTable();
	const FCataclysmForgeOperationRow* Row = Table != nullptr
		? Table->FindRow<FCataclysmForgeOperationRow>(FName(Operation), TEXT("UCataclysmForge::BaseGoldOf"),
													  /*bWarnIfMissing=*/false)
		: nullptr;
	if (Row == nullptr)
	{
		UE_LOG(LogCataclysm, Error, TEXT("The forge's operations table has no row %s, so it costs %d gold, the constant the "
									"sheet's figure mirrors."),
			   Operation, IfMissing);
		return IfMissing;
	}
	return FMath::RoundToInt(Row->BaseGold);
}

FString UCataclysmForge::GoldRefusal(const FString& What, int64 Gold, int64 Held)
{
	return FString::Printf(TEXT("%s takes %s; %s held."), *What, *ACataclysmDroppedItem::GoldName(Gold),
						   *ACataclysmDroppedItem::GoldName(FMath::Max<int64>(0, Held)));
}

FCataclysmForgeUpgrade UCataclysmForge::UpgradeOf(const FCataclysmItem& Item, int32 StonesOfTheNextLevelHeld,
												  int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeUpgrade Out;
	Out.FromLevel = Item.GearLevel;
	Out.ToLevel = Item.GearLevel + 1;
	Out.StonesHeld = FMath::Max(0, StonesOfTheNextLevelHeld);
	Out.ResidueBefore = Item.Residue;
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to upgrade.");
		return Out;
	}

	// SEQUENTIAL, AND +10 IS THE TOP: "Upgrades are sequential -- you cannot skip levels".
	if (Item.GearLevel >= UCataclysmItemValues::MaxGearLevel)
	{
		Out.ToLevel = Item.GearLevel;
		Out.Refusal = FString::Printf(TEXT("This is +%d, the highest an item can be raised to."), Item.GearLevel);
		return Out;
	}

	Out.Stone = StoneFor(Out.ToLevel);
	Out.StonesNeeded = StonesPerUpgrade;
	Out.ResidueAfter = Item.Residue + ResiduePerLevel;
	// THE LEVEL IT REACHES IN BASE DAYS, AND ITS GOLD BY THEM: the owner's ruling of 2026-11-21 and #2830.
	Out.Days = DaysFor(Item.Residue, Out.ToLevel);
	// THE SHEET'S BASE GOLD FOR A LEVEL, ONCE FOR EACH LEVEL REACHED, as its days are "per tier".
	Out.Gold = GoldFor(BaseGoldOf(UpgradeOperationRow, GoldPerCraftDay) * Out.ToLevel, Item.Residue, DifficultyTier);

	if (Out.StonesHeld < Out.StonesNeeded)
	{
		Out.Refusal = FString::Printf(TEXT("Raising it to +%d takes two +%d upgrade stones; %d held."), Out.ToLevel,
									  Out.ToLevel, Out.StonesHeld);
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(FString::Printf(TEXT("Raising it to +%d"), Out.ToLevel), Out.Gold, Out.GoldHeld);
		return Out;
	}

	Out.bPossible = true;
	return Out;
}

FCataclysmForgeCombine UCataclysmForge::CombineOf(int32 Level, int32 StonesOfThisLevelHeld, int32 DifficultyTier,
												  int64 GoldHeld)
{
	FCataclysmForgeCombine Out;
	Out.FromLevel = Level;
	Out.IntoLevel = Level + 1;
	Out.StonesHeld = FMath::Max(0, StonesOfThisLevelHeld);
	Out.StonesNeeded = StonesPerCombine;
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Level < 1 || Level >= HighestStone)
	{
		Out.Refusal = Level == HighestStone
			? FString::Printf(TEXT("A +%d stone is the highest; there is none above it to make."), HighestStone)
			: FString::Printf(TEXT("There is no +%d upgrade stone."), Level);
		return Out;
	}

	Out.From = StoneFor(Level);
	Out.Into = StoneFor(Level + 1);
	// A +1 STONE IS THE LOWEST RUNG (#2830).
	Out.Gold = CombineGoldFor(Level - 1, DifficultyTier);
	if (Out.StonesHeld < Out.StonesNeeded)
	{
		Out.Refusal = FString::Printf(TEXT("Combining takes two +%d upgrade stones; %d held."), Level, Out.StonesHeld);
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(FString::Printf(TEXT("Combining two +%d upgrade stones"), Level), Out.Gold,
								  Out.GoldHeld);
		return Out;
	}

	Out.bPossible = true;
	return Out;
}

FCataclysmForgeGemCombine UCataclysmForge::GemCombineOf(FName Gem, ECataclysmRarity Rarity, int32 HeldOfThis,
														int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeGemCombine Out;
	Out.Gem = Gem;
	Out.From = Rarity;
	Out.Into = Rarity;
	Out.GemsHeld = FMath::Max(0, HeldOfThis);
	Out.GemsNeeded = GemsPerCombine;
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Gem.IsNone())
	{
		Out.Refusal = TEXT("There is no gem to combine.");
		return Out;
	}
	const FString Name = UCataclysmGems::NameOf(Gem);
	if (Rarity == ECataclysmRarity::Cataclysmic)
	{
		Out.Refusal = TEXT("A Cataclysmic gem is the highest; there is none above it to make.");
		return Out;
	}

	Out.Into = static_cast<ECataclysmRarity>(static_cast<uint8>(Rarity) + 1);
	// THE STONE COMBINE'S GOLD, AT THE RARITY'S RUNG: an Everyday gem is the lowest (#2830).
	Out.Gold = CombineGoldFor(static_cast<int32>(Rarity), DifficultyTier);
	if (Out.GemsHeld < Out.GemsNeeded)
	{
		Out.Refusal = FString::Printf(TEXT("Combining takes two %s (%s); %d held."), *Name,
									  *UCataclysmGems::RarityName(Rarity), Out.GemsHeld);
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(FString::Printf(TEXT("Combining two %s (%s)"), *Name,
												  *UCataclysmGems::RarityName(Rarity)),
								  Out.Gold, Out.GoldHeld);
		return Out;
	}

	Out.bPossible = true;
	return Out;
}

FString UCataclysmForge::UpgradeQuestion(const FCataclysmForgeUpgrade& Upgrade)
{
	return FString::Printf(TEXT("Press again to upgrade to +%d: two +%d stones, %s, residue %d to %d, %d day%s of the run."),
						   Upgrade.ToLevel, Upgrade.ToLevel, *ACataclysmDroppedItem::GoldName(Upgrade.Gold),
						   FMath::RoundToInt(Upgrade.ResidueBefore), FMath::RoundToInt(Upgrade.ResidueAfter),
						   Upgrade.Days, Upgrade.Days == 1 ? TEXT("") : TEXT("s"));
}

FString UCataclysmForge::ThresholdQuestion(const FString& ItemName, float WornResidueAfter, float Threshold)
{
	// THE EQUIP'S WORDS, with the verb of this action, and its consequence in the design's words
	// (`UCataclysmWearing::ThresholdConsequence`).
	return FString::Printf(
		TEXT("Upgrading %s brings your Worn Residue to %d, past this tier's Consumption Threshold of %d. %s"),
		ItemName.IsEmpty() ? TEXT("this") : *ItemName, FMath::RoundToInt(WornResidueAfter),
		FMath::RoundToInt(Threshold), UCataclysmWearing::ThresholdConsequence);
}

int32 UCataclysmForge::DaysFor(float Residue, int32 BaseDays)
{
	return BaseDays + FMath::FloorToInt(FMath::Max(0.0f, Residue) / ResiduePerPenaltyDay);
}

FCataclysmForgeSocketing UCataclysmForge::SocketGemOf(const FCataclysmItem& Item, int32 Socket, FName Gem,
													  ECataclysmRarity Rarity, const FString& GemType, int32 GemsHeld,
													  int32 AgentsHeld, int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeSocketing Out;
	Out.Socket = Socket;
	Out.Gem = Gem;
	Out.Rarity = Rarity;
	Out.Material = FName(SettingAgentRow);
	Out.MaterialHeld = FMath::Max(0, AgentsHeld);
	Out.ResidueBefore = Item.Residue;
	Out.ResidueAfter = Item.Residue + SocketGemResidue;
	Out.Days = DaysFor(Item.Residue, SocketGemBaseDays);
	Out.Gold = GoldFor(BaseGoldOf(SocketGemOperationRow, BaseGoldFor(SocketGemBaseDays)), Item.Residue, DifficultyTier);
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to socket.");
		return Out;
	}
	if (!Item.Sockets.IsValidIndex(Socket))
	{
		Out.Refusal = FString::Printf(TEXT("That piece has no socket %d."), Socket + 1);
		return Out;
	}
	const FCataclysmSocket& Into = Item.Sockets[Socket];
	Out.SocketType = Into.Type;
	if (!Into.Gem.IsNone())
	{
		Out.Refusal = FString::Printf(TEXT("That socket already holds %s; unsocket it first."),
									  *UCataclysmGems::NameOf(Into.Gem));
		return Out;
	}
	if (Gem.IsNone() || GemsHeld <= 0)
	{
		Out.Refusal = FString::Printf(TEXT("No %s (%s) is carried."), *UCataclysmGems::NameOf(Gem),
									  *UCataclysmGems::RarityName(Rarity));
		return Out;
	}
	// A GEM FITS A SOCKET OF ITS OWN TYPE, OR ONE FOR ANY GEM. Ruled 2026-11-21. OR ANY SOCKET OF A PIECE WHOSE
	// ENCHANTMENT SAYS ITS SOCKETS FIT ANY GEM TYPE, read off the piece now and never written into the socket (slice G10,
	// Q1 of 2026-11-22). The refusal below still names the socket's own type, which the enchantment does not change.
	if (!UCataclysmGems::GemFitsIn(Item, Socket, GemType, UCataclysmItemModifiers::LoadEnchantmentEffectTable()))
	{
		// "AN" BEFORE A VOWEL SOUND: an Attack gem, a Defense socket, a Utility socket.
		const auto Article = [](const FString& Word)
		{
			return Word.StartsWith(TEXT("A")) || Word.StartsWith(TEXT("E")) || Word.StartsWith(TEXT("I"))
					|| Word.StartsWith(TEXT("O"))
				? TEXT("an")
				: TEXT("a");
		};
		// NOT NAMED `Gem`, which is this function's parameter: hiding it is an error in this build (C4457).
		const FString GemTypeWord = GemType.TrimStartAndEnd();
		const FString Type = UCataclysmGems::SocketTypeName(Into.Type);
		Out.Refusal = FString::Printf(TEXT("%s %s gem does not fit %s %s socket."),
									  FCString::Strcmp(Article(GemTypeWord), TEXT("an")) == 0 ? TEXT("An") : TEXT("A"),
									  *GemTypeWord, Article(Type), *Type);
		return Out;
	}
	if (Out.MaterialHeld < 1)
	{
		Out.Refusal = TEXT("Socketing a gem takes a Jeweler's Setting Agent; 0 held.");
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(TEXT("Socketing a gem"), Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

FCataclysmForgeSocketing UCataclysmForge::UnsocketGemOf(const FCataclysmItem& Item, int32 Socket, int32 DustHeld,
														bool bRoomForTheGem, int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeSocketing Out;
	Out.bUnsocket = true;
	Out.Socket = Socket;
	Out.Material = FName(DismantlingDustRow);
	Out.MaterialHeld = FMath::Max(0, DustHeld);
	Out.ResidueBefore = Item.Residue;
	Out.ResidueAfter = Item.Residue + UnsocketGemResidue;
	Out.Days = DaysFor(Item.Residue, UnsocketGemBaseDays);
	Out.Gold = GoldFor(BaseGoldOf(UnsocketGemOperationRow, BaseGoldFor(UnsocketGemBaseDays)), Item.Residue,
					   DifficultyTier);
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to unsocket.");
		return Out;
	}
	if (!Item.Sockets.IsValidIndex(Socket))
	{
		Out.Refusal = FString::Printf(TEXT("That piece has no socket %d."), Socket + 1);
		return Out;
	}
	const FCataclysmSocket& From = Item.Sockets[Socket];
	Out.SocketType = From.Type;
	Out.Gem = From.Gem;
	Out.Rarity = From.GemRarity;
	if (From.Gem.IsNone())
	{
		Out.Refusal = TEXT("That socket holds no gem.");
		return Out;
	}
	// NO FREE REMOVAL: only Dismantling Dust takes a gem out, and it comes back whole. Ruled 2026-11-21.
	if (Out.MaterialHeld < 1)
	{
		Out.Refusal = TEXT("Unsocketing a gem takes Dismantling Dust; 0 held.");
		return Out;
	}
	if (!bRoomForTheGem)
	{
		Out.Refusal = TEXT("The bag has no free cell for the gem.");
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(TEXT("Unsocketing a gem"), Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

FString UCataclysmForge::SocketingQuestion(const FCataclysmForgeSocketing& Plan)
{
	const FString Gem = FString::Printf(TEXT("%s (%s)"), *UCataclysmGems::NameOf(Plan.Gem),
										*UCataclysmGems::RarityName(Plan.Rarity));
	const FString Cost = FString::Printf(TEXT("%s, residue %d to %d, %d day%s of the run"),
										 *ACataclysmDroppedItem::GoldName(Plan.Gold), FMath::RoundToInt(Plan.ResidueBefore),
										 FMath::RoundToInt(Plan.ResidueAfter), Plan.Days,
										 Plan.Days == 1 ? TEXT("") : TEXT("s"));
	if (Plan.bUnsocket)
	{
		return FString::Printf(TEXT("Press again to unsocket %s: one Dismantling Dust, %s; the gem goes back to the bag."),
							   *Gem, *Cost);
	}
	const FString Into = Plan.SocketType == ECataclysmSocketType::Any
		? FString(TEXT("the socket for any gem"))
		: FString::Printf(TEXT("the %s socket"), *UCataclysmGems::SocketTypeName(Plan.SocketType));
	return FString::Printf(TEXT("Press again to socket %s into %s: one Jeweler's Setting Agent, %s."), *Gem, *Into,
						   *Cost);
}

FCataclysmForgeSocketWork UCataclysmForge::AddSocketOf(const FCataclysmItem& Item, int32 MaxSockets, int32 CoresHeld,
														int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeSocketWork Out;
	Out.bAdd = true;
	Out.Socket = Item.Sockets.Num();
	Out.MaxSockets = MaxSockets;
	Out.Material = FName(ShatteredCoreRow);
	Out.MaterialHeld = FMath::Max(0, CoresHeld);
	Out.ResidueBefore = Item.Residue;
	Out.ResidueAfter = Item.Residue + AddSocketResidue;
	Out.Days = DaysFor(Item.Residue, AddSocketBaseDays);
	Out.Gold = GoldFor(BaseGoldOf(AddSocketOperationRow, BaseGoldFor(AddSocketBaseDays)), Item.Residue, DifficultyTier);
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to add a socket to.");
		return Out;
	}
	if (MaxSockets < 0)
	{
		Out.Refusal = TEXT("How many sockets that piece can take is not known.");
		return Out;
	}
	// THE SLOT'S MAXIMUM, `game/Data/ItemSockets.csv`: the design's table "is a MAXIMUM". Ruled 2026-11-22 (Q3).
	if (Item.Sockets.Num() >= MaxSockets)
	{
		Out.Refusal = FString::Printf(TEXT("That piece holds its most sockets, %d."), MaxSockets);
		return Out;
	}
	if (Out.MaterialHeld < 1)
	{
		Out.Refusal = TEXT("Adding a socket takes a Shattered Core; 0 held.");
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(TEXT("Adding a socket"), Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

FCataclysmForgeSocketWork UCataclysmForge::ChangeSocketTypeOf(const FCataclysmItem& Item, int32 Socket,
															   ECataclysmSocketType NewType, int32 LensesHeld,
															   int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeSocketWork Out;
	Out.Socket = Socket;
	Out.ToType = NewType;
	Out.Material = FName(FocusingLensRow);
	Out.MaterialHeld = FMath::Max(0, LensesHeld);
	Out.ResidueBefore = Item.Residue;
	Out.ResidueAfter = Item.Residue + ChangeSocketTypeResidue;
	Out.Days = DaysFor(Item.Residue, ChangeSocketTypeBaseDays);
	Out.Gold = GoldFor(BaseGoldOf(ChangeSocketTypeOperationRow, BaseGoldFor(ChangeSocketTypeBaseDays)), Item.Residue,
					   DifficultyTier);
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to change.");
		return Out;
	}
	if (!Item.Sockets.IsValidIndex(Socket))
	{
		Out.Refusal = FString::Printf(TEXT("That piece has no socket %d."), Socket + 1);
		return Out;
	}
	const FCataclysmSocket& Changed = Item.Sockets[Socket];
	Out.FromType = Changed.Type;
	// A SOCKET FOR ANY GEM IS NOT CHANGED: it fits every gem, so a change could only narrow it. Ruled 2026-11-22 (Q5).
	if (Changed.Type == ECataclysmSocketType::Any)
	{
		Out.Refusal = TEXT("That socket fits any gem already.");
		return Out;
	}
	// NO FREE REMOVAL: a gem comes out only with Dismantling Dust. Ruled 2026-11-22 (Q4).
	if (!Changed.Gem.IsNone())
	{
		Out.Refusal = TEXT("Unsocket the gem first.");
		return Out;
	}
	if (NewType == ECataclysmSocketType::Any)
	{
		Out.Refusal = TEXT("A socket can be made Attack, Defense or Utility, not one for any gem.");
		return Out;
	}
	if (NewType == Changed.Type)
	{
		Out.Refusal = FString::Printf(TEXT("That socket is a %s socket already."), *UCataclysmGems::SocketTypeName(NewType));
		return Out;
	}
	if (Out.MaterialHeld < 1)
	{
		Out.Refusal = TEXT("Changing a socket's type takes a Focusing Lens; 0 held.");
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(TEXT("Changing a socket's type"), Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

FString UCataclysmForge::ItemSeedText(const FCataclysmItem& Item)
{
	const auto Names = [](const TArray<FName>& List)
	{
		TArray<FString> Words;
		for (const FName& Each : List)
		{
			Words.Add(Each.ToString());
		}
		return FString::Join(Words, TEXT("+"));
	};
	TArray<FString> Affixes;
	for (const FCataclysmRolledAffix& Affix : Item.Affixes)
	{
		Affixes.Add(FString::Printf(TEXT("%s,%d,%.6f,%s"), *Affix.Affix.ToString(), Affix.Tier,
									static_cast<double>(Affix.Roll), *Names(Affix.DamageTypes)));
	}
	TArray<FString> Enchantments;
	for (const FCataclysmRolledEnchantment& Enchantment : Item.Enchantments)
	{
		Enchantments.Add(FString::Printf(TEXT("%s,%s,%.6f,%.6f"), *Enchantment.Positive.ToString(),
										 *Enchantment.Negative.ToString(), static_cast<double>(Enchantment.PositiveRoll),
										 static_cast<double>(Enchantment.NegativeRoll)));
	}
	TArray<FString> Sockets;
	for (const FCataclysmSocket& Socket : Item.Sockets)
	{
		Sockets.Add(FString::Printf(TEXT("%s,%s,%d"), *UCataclysmGems::SocketTypeName(Socket.Type),
									*Socket.Gem.ToString(), static_cast<int32>(Socket.GemRarity)));
	}
	return FString::Printf(
		TEXT("Base=%s;Level=%d;Types=%s;Affixes=%s;Enchantments=%d:%s;Sockets=%s;Residue=%.6f;Kills=%d;Made=%d"),
		*Item.Base.ToString(), Item.GearLevel, *Names(Item.DamageTypes), *FString::Join(Affixes, TEXT("|")),
		Item.EnchantmentCount, *FString::Join(Enchantments, TEXT("|")), *FString::Join(Sockets, TEXT("|")),
		static_cast<double>(Item.Residue), Item.Kills, Item.bMadeAtCreation ? 1 : 0);
}

int32 UCataclysmForge::AddedSocketSeed(const FCataclysmItem& Item, int32 WalkSeedMask, int32 Day)
{
	// TWO MIXES, so all four inputs go in: the run's mask, the salt and the day; then the item's text and its count.
	const int32 OfTheRunAndDay = CataclysmSeedMix::Mix(WalkSeedMask, AddedSocketSalt, Day);
	const uint32 Crc = FCrc::StrCrc32(*ItemSeedText(Item));
	return CataclysmSeedMix::Mix(OfTheRunAndDay, static_cast<int32>(Crc), Item.Sockets.Num());
}

ECataclysmSocketType UCataclysmForge::AddedSocketType(const FCataclysmItem& Item, int32 WalkSeedMask, int32 Day)
{
	FRandomStream Stream(AddedSocketSeed(Item, WalkSeedMask, Day));
	return UCataclysmGems::RollSocketType(Stream);
}

FString UCataclysmForge::SocketWorkQuestion(const FCataclysmForgeSocketWork& Plan)
{
	const FString Cost = FString::Printf(TEXT("%s, residue %d to %d, %d day%s of the run"),
										 *ACataclysmDroppedItem::GoldName(Plan.Gold), FMath::RoundToInt(Plan.ResidueBefore),
										 FMath::RoundToInt(Plan.ResidueAfter), Plan.Days,
										 Plan.Days == 1 ? TEXT("") : TEXT("s"));
	if (Plan.bAdd)
	{
		return FString::Printf(TEXT("Press again to add a socket: one Shattered Core, %s; its type is rolled."), *Cost);
	}
	return FString::Printf(TEXT("Press again to make socket %d a %s socket: one Focusing Lens, %s."), Plan.Socket + 1,
						   *UCataclysmGems::SocketTypeName(Plan.ToType), *Cost);
}

FName UCataclysmForge::MaterialFor(ECataclysmForgeAffixCraft Craft)
{
	switch (Craft)
	{
	case ECataclysmForgeAffixCraft::PerfectRoll:	return FName(PrimalSparkRow);
	case ECataclysmForgeAffixCraft::UpgradeTier:	return FName(PotencyCrystalRow);
	case ECataclysmForgeAffixCraft::RerollValue:
	default:										return FName(CorruptedMoteRow);
	}
}

FCataclysmForgeAffixWork UCataclysmForge::AffixWorkOf(const FCataclysmItem& Item, ECataclysmForgeAffixCraft Craft,
													   int32 Affix, int32 MaterialHeld, int32 DifficultyTier,
													   int64 GoldHeld)
{
	FCataclysmForgeAffixWork Out;
	Out.Craft = Craft;
	Out.Affix = Affix;
	Out.Material = MaterialFor(Craft);
	Out.MaterialHeld = FMath::Max(0, MaterialHeld);
	Out.ResidueBefore = Item.Residue;
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to craft.");
		return Out;
	}
	if (!Item.Affixes.IsValidIndex(Affix))
	{
		Out.Refusal = FString::Printf(TEXT("That piece has no affix %d."), Affix + 1);
		return Out;
	}
	const FCataclysmRolledAffix& Rolled = Item.Affixes[Affix];
	Out.AffixRow = Rolled.Affix;
	Out.FromTier = Rolled.Tier;
	Out.ToTier = Rolled.Tier;
	Out.FromRoll = Rolled.Roll;
	Out.ToRoll = Rolled.Roll;

	// EACH CRAFT'S ROW: its residue, its base days and its Base Gold, falling back on the constant the sheet's figure
	// mirrors. An upgrade's residue and days are the affix's tier before it, "10 CR*(Current affix tier)" and
	// "1(current affix tier)", and its Base Gold the sheet's flat 250 (Q13).
	float Added = 0.0f;
	int32 BaseDays = 0;
	int32 BaseGold = 0;
	FString Doing;
	switch (Craft)
	{
	case ECataclysmForgeAffixCraft::PerfectRoll:
		Added = PerfectRollResidue;
		BaseDays = PerfectRollBaseDays;
		BaseGold = BaseGoldOf(PerfectRollOperationRow, BaseGoldFor(PerfectRollBaseDays));
		Doing = TEXT("Perfecting an affix's roll");
		Out.ToRoll = 1.0f;
		break;
	case ECataclysmForgeAffixCraft::UpgradeTier:
		Added = UpgradeAffixTierResiduePerTier * static_cast<float>(Rolled.Tier);
		BaseDays = Rolled.Tier;
		BaseGold = BaseGoldOf(UpgradeAffixTierOperationRow, GoldPerCraftDay);
		Doing = TEXT("Raising an affix's tier");
		Out.ToTier = Rolled.Tier + 1;
		break;
	case ECataclysmForgeAffixCraft::RerollValue:
	default:
		Added = RerollAffixValueResidue;
		BaseDays = RerollAffixValueBaseDays;
		BaseGold = BaseGoldOf(RerollAffixValueOperationRow, BaseGoldFor(RerollAffixValueBaseDays));
		Doing = TEXT("Rerolling an affix's value");
		break;
	}
	Out.ResidueAfter = Item.Residue + Added;
	Out.Days = DaysFor(Item.Residue, BaseDays);
	Out.Gold = GoldFor(BaseGold, Item.Residue, DifficultyTier);

	// A PERFECTING OF A ROLL AT THE TOP WOULD SPEND A PRIMAL SPARK FOR NOTHING. A JUDGEMENT of this slice, as a socket's
	// change to its own type is refused.
	if (Craft == ECataclysmForgeAffixCraft::PerfectRoll && Rolled.Roll >= 1.0f)
	{
		Out.Refusal = TEXT("That affix's roll is the top of its tier already.");
		return Out;
	}
	// TIER 7 IS THE TOP: "Raises an affix by one tier, to a maximum of 7".
	if (Craft == ECataclysmForgeAffixCraft::UpgradeTier && Rolled.Tier >= UCataclysmItemValues::MaxAffixTier)
	{
		Out.ToTier = Rolled.Tier;
		Out.Refusal = FString::Printf(TEXT("That affix is tier %d, the highest."), Rolled.Tier);
		return Out;
	}
	if (Out.MaterialHeld < 1)
	{
		const TCHAR* Material = Craft == ECataclysmForgeAffixCraft::PerfectRoll ? TEXT("a Primal Spark")
			: Craft == ECataclysmForgeAffixCraft::UpgradeTier					 ? TEXT("a Potency Crystal")
																				 : TEXT("a Corrupted Mote");
		Out.Refusal = FString::Printf(TEXT("%s takes %s; 0 held."), *Doing, Material);
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(Doing, Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

int32 UCataclysmForge::RerolledAffixSeed(const FCataclysmItem& Item, int32 Affix, int32 WalkSeedMask, int32 Day)
{
	// AN ADDED SOCKET'S TWO MIXES, with this craft's salt and the affix's place for the socket count.
	const int32 OfTheRunAndDay = CataclysmSeedMix::Mix(WalkSeedMask, RerolledAffixSalt, Day);
	const uint32 Crc = FCrc::StrCrc32(*ItemSeedText(Item));
	return CataclysmSeedMix::Mix(OfTheRunAndDay, static_cast<int32>(Crc), Affix);
}

float UCataclysmForge::RerolledAffixRoll(const FCataclysmItem& Item, int32 Affix, int32 WalkSeedMask, int32 Day)
{
	// EVENLY FROM 0 UP TO 1, AS A DROP DRAWS AN AFFIX'S ROLL (`UCataclysmDropRoll`, `Rolled.Roll = Stream.FRand()`).
	const FRandomStream Stream(RerolledAffixSeed(Item, Affix, WalkSeedMask, Day));
	return Stream.FRand();
}

int32 UCataclysmForge::RollPercent(float Roll)
{
	return FMath::RoundToInt(FMath::Clamp(Roll, 0.0f, 1.0f) * 100.0f);
}

FString UCataclysmForge::AffixWorkQuestion(const FCataclysmForgeAffixWork& Plan)
{
	const FString Cost = FString::Printf(TEXT("%s, residue %d to %d, %d day%s of the run"),
										 *ACataclysmDroppedItem::GoldName(Plan.Gold), FMath::RoundToInt(Plan.ResidueBefore),
										 FMath::RoundToInt(Plan.ResidueAfter), Plan.Days,
										 Plan.Days == 1 ? TEXT("") : TEXT("s"));
	switch (Plan.Craft)
	{
	case ECataclysmForgeAffixCraft::PerfectRoll:
		return FString::Printf(TEXT("Press again to perfect affix %d, from %d to 100 percent of tier %d's range: one Primal "
									"Spark, %s."),
							   Plan.Affix + 1, RollPercent(Plan.FromRoll), Plan.FromTier, *Cost);
	case ECataclysmForgeAffixCraft::UpgradeTier:
		return FString::Printf(TEXT("Press again to raise affix %d from tier %d to tier %d, its roll kept at %d percent: one "
									"Potency Crystal, %s."),
							   Plan.Affix + 1, Plan.FromTier, Plan.ToTier, RollPercent(Plan.FromRoll), *Cost);
	case ECataclysmForgeAffixCraft::RerollValue:
	default:
		return FString::Printf(TEXT("Press again to reroll affix %d: one Corrupted Mote, %s; its roll, now %d percent of tier "
									"%d's range, is drawn again and can fall."),
							   Plan.Affix + 1, *Cost, RollPercent(Plan.FromRoll), Plan.FromTier);
	}
}

FString UCataclysmForge::SlotOf(const FCataclysmItem& Item, const UDataTable* BaseTable)
{
	const FCataclysmItemBaseRow* Row = BaseTable != nullptr && !Item.Base.IsNone()
		? BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("UCataclysmForge::SlotOf"), /*bWarnIfMissing=*/false)
		: nullptr;
	return Row != nullptr ? Row->Slot : FString();
}

void UCataclysmForge::HeldGroupsOf(const FCataclysmItem& Item, const UDataTable* AffixTable, TSet<FString>& OutGroups)
{
	OutGroups.Reset();
	for (const FCataclysmRolledAffix& Rolled : Item.Affixes)
	{
		const FCataclysmAffixRow* Row = AffixTable != nullptr
			? AffixTable->FindRow<FCataclysmAffixRow>(Rolled.Affix, TEXT("UCataclysmForge::HeldGroupsOf"),
													  /*bWarnIfMissing=*/false)
			: nullptr;
		if (Row != nullptr)
		{
			TSet<FString> Groups;
			UCataclysmDropRoll::GroupsOf(AffixTable, *Row, Rolled.DamageTypes, Groups);
			OutGroups.Append(Groups);
		}
	}
}

TArray<FName> UCataclysmForge::AddableAffixesFor(const FCataclysmItem& Item, const UDataTable* AffixTable,
												 const UDataTable* BaseTable)
{
	TArray<FName> Out;
	const FString Slot = SlotOf(Item, BaseTable);
	if (Slot.IsEmpty() || AffixTable == nullptr)
	{
		return Out;
	}
	TSet<FString> Held;
	HeldGroupsOf(Item, AffixTable, Held);
	AffixTable->ForeachRow<FCataclysmAffixRow>(TEXT("UCataclysmForge::AddableAffixesFor"),
		[&](const FName& Key, const FCataclysmAffixRow& Row)
		{
			// A DROP'S POOL FOR THE SLOT, PREFIX OR SUFFIX (Q10), as `CandidatesFor` reads it.
			if (!ForgeAffixAllowedOn(Row, Slot))
			{
				return;
			}
			// NEVER A STAT GROUP THE PIECE HOLDS, the drop's rule: a resistance family covering some types while enough
			// types are free, any other while its own groups are.
			if (ForgeChoosesDamageTypes(Row))
			{
				if (ForgeUnheldDamageTypes(AffixTable, Row, Held).Num() >= Row.Breadth)
				{
					Out.Add(Key);
				}
				return;
			}
			TSet<FString> Groups;
			UCataclysmDropRoll::GroupsOf(AffixTable, Row, {}, Groups);
			if (Groups.Intersect(Held).Num() == 0)
			{
				Out.Add(Key);
			}
		});
	return Out;
}

int32 UCataclysmForge::AffixCraftSeed(const FCataclysmItem& Item, int32 Salt, int32 Index, int32 WalkSeedMask, int32 Day)
{
	// A REROLL'S TWO MIXES (`RerolledAffixSeed`), with the craft's salt and its index.
	const int32 OfTheRunAndDay = CataclysmSeedMix::Mix(WalkSeedMask, Salt, Day);
	const uint32 Crc = FCrc::StrCrc32(*ItemSeedText(Item));
	return CataclysmSeedMix::Mix(OfTheRunAndDay, static_cast<int32>(Crc), Index);
}

FCataclysmForgeAffixChange UCataclysmForge::DeterministicAffixOf(const FCataclysmItem& Item, FName AffixRow, int32 Tier,
																  int32 Seed, const UDataTable* AffixTable,
																  const UDataTable* BaseTable, int32 FragmentsHeld,
																  int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeAffixChange Out;
	Out.Affix = Item.Affixes.Num();
	Out.ToRow = AffixRow;
	Out.Tier = Tier;
	Out.Material = FName(SchematicFragmentsRow);
	Out.MaterialHeld = FMath::Max(0, FragmentsHeld);
	Out.ResidueBefore = Item.Residue;
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);
	Out.bHadRarity = UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num(), Out.FromRarity);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to craft.");
		return Out;
	}
	if (SlotOf(Item, BaseTable).IsEmpty())
	{
		Out.Refusal = TEXT("What that piece is cannot be found.");
		return Out;
	}
	// A FREE SLOT OF THE FOUR: a piece with an enchantment fills all four, so it takes none.
	if (Item.Affixes.Num() + Item.EnchantmentCount >= UCataclysmItemValues::SlotsPerPiece)
	{
		Out.Refusal = TEXT("That piece's four slots are full.");
		return Out;
	}
	if (Tier < 1 || Tier > UCataclysmItemValues::MaxAffixTier)
	{
		Out.Refusal = FString::Printf(TEXT("An affix can be added at tier 1 to %d."), UCataclysmItemValues::MaxAffixTier);
		return Out;
	}
	const FCataclysmAffixRow* Row = AffixTable != nullptr && !AffixRow.IsNone()
		? AffixTable->FindRow<FCataclysmAffixRow>(AffixRow, TEXT("UCataclysmForge::DeterministicAffixOf"),
												  /*bWarnIfMissing=*/false)
		: nullptr;
	if (Row == nullptr || !AddableAffixesFor(Item, AffixTable, BaseTable).Contains(AffixRow))
	{
		Out.Refusal = FString::Printf(TEXT("%s cannot be added to that piece."), *AffixNameOf(AffixRow, AffixTable));
		return Out;
	}

	// ADDING AN AFFIX PROMOTES THE PIECE, by the definition of rarity: one more affix in the same slots.
	UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num() + 1, Out.ToRarity);
	Out.ResidueAfter = Item.Residue + DeterministicAffixResiduePerTier * static_cast<float>(Tier);
	Out.Days = DaysFor(Item.Residue, FMath::Min(Tier, DeterministicAffixMaxBaseDays));
	Out.Gold = GoldFor(BaseGoldOf(DeterministicAffixOperationRow, GoldPerCraftDay), Item.Residue, DifficultyTier);

	// THE DRAW: a resistance family's damage types among those the piece does not hold, as `CandidatesFor` draws them,
	// then the roll in the tier, as a drop draws it (Q11).
	FRandomStream Stream(Seed);
	if (ForgeChoosesDamageTypes(*Row))
	{
		TSet<FString> Held;
		HeldGroupsOf(Item, AffixTable, Held);
		TArray<FName> Pool = ForgeUnheldDamageTypes(AffixTable, *Row, Held);
		for (int32 Taken = 0; Taken < Row->Breadth && Taken < Pool.Num(); ++Taken)
		{
			const int32 Index = Stream.RandRange(Taken, Pool.Num() - 1);
			Pool.Swap(Taken, Index);
			Out.ToDamageTypes.Add(Pool[Taken]);
		}
	}
	Out.Roll = Stream.FRand();

	if (Out.MaterialHeld < 1)
	{
		Out.Refusal = TEXT("Adding an affix takes Schematic Fragments; 0 held.");
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(TEXT("Adding an affix"), Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

FCataclysmForgeAffixChange UCataclysmForge::AffixShiftOf(const FCataclysmItem& Item, int32 Affix, int32 Seed,
														  const UDataTable* AffixTable, const UDataTable* BaseTable,
														  int32 CrystalsHeld, int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeAffixChange Out;
	Out.bShift = true;
	Out.Affix = Affix;
	Out.Material = FName(CrystalOfInstabilityRow);
	Out.MaterialHeld = FMath::Max(0, CrystalsHeld);
	Out.ResidueBefore = Item.Residue;
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);
	Out.bHadRarity = UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num(), Out.FromRarity);
	Out.ToRarity = Out.FromRarity;

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to craft.");
		return Out;
	}
	const FString Slot = SlotOf(Item, BaseTable);
	if (Slot.IsEmpty())
	{
		Out.Refusal = TEXT("What that piece is cannot be found.");
		return Out;
	}
	if (!Item.Affixes.IsValidIndex(Affix))
	{
		Out.Refusal = FString::Printf(TEXT("That piece has no affix %d."), Affix + 1);
		return Out;
	}
	const FCataclysmRolledAffix& Rolled = Item.Affixes[Affix];
	Out.FromRow = Rolled.Affix;
	Out.ToRow = Rolled.Affix;
	Out.ToDamageTypes = Rolled.DamageTypes;
	// THE TIER AND THE ROLL ARE KEPT (Q4): "Rerolls the stat type, not its value".
	Out.Tier = Rolled.Tier;
	Out.Roll = Rolled.Roll;
	Out.ResidueAfter = Item.Residue + AffixShiftingResidue;
	Out.Days = DaysFor(Item.Residue, AffixShiftingBaseDays);
	Out.Gold = GoldFor(BaseGoldOf(AffixShiftingOperationRow, BaseGoldFor(AffixShiftingBaseDays)), Item.Residue,
					   DifficultyTier);

	const FCataclysmAffixRow* FromRow = AffixTable != nullptr
		? AffixTable->FindRow<FCataclysmAffixRow>(Rolled.Affix, TEXT("UCataclysmForge::AffixShiftOf"),
												  /*bWarnIfMissing=*/false)
		: nullptr;
	if (FromRow == nullptr)
	{
		Out.Refusal = TEXT("That affix is not in the affix table.");
		return Out;
	}

	// THE DRAW: the drop's pool for the slot and the affix's own position, its resistance types drawn as a drop draws
	// them, less the affix's own row and any candidate of a stat group the piece holds, the affix's own groups among
	// them (Q3); then one of what is left, evenly.
	FRandomStream Stream(Seed);
	TArray<FCataclysmAffixCandidate> Candidates;
	UCataclysmDropRoll::CandidatesFor(AffixTable, Slot, FromRow->Position, Stream, Candidates);
	TSet<FString> Held;
	HeldGroupsOf(Item, AffixTable, Held);
	TArray<FCataclysmAffixCandidate> Open;
	for (const FCataclysmAffixCandidate& Candidate : Candidates)
	{
		const FCataclysmAffixRow* Row = Candidate.Affix == Rolled.Affix
			? nullptr
			: AffixTable->FindRow<FCataclysmAffixRow>(Candidate.Affix, TEXT("UCataclysmForge::AffixShiftOf"),
													  /*bWarnIfMissing=*/false);
		if (Row == nullptr)
		{
			continue;
		}
		TSet<FString> Groups;
		UCataclysmDropRoll::GroupsOf(AffixTable, *Row, Candidate.DamageTypes, Groups);
		if (Groups.Intersect(Held).Num() == 0)
		{
			Open.Add(Candidate);
		}
	}
	if (Open.IsEmpty())
	{
		Out.Refusal = TEXT("No other affix can take that one's place on that piece.");
		return Out;
	}
	const FCataclysmAffixCandidate& Drawn = Open[Stream.RandRange(0, Open.Num() - 1)];
	Out.ToRow = Drawn.Affix;
	Out.ToDamageTypes = Drawn.DamageTypes;

	if (Out.MaterialHeld < 1)
	{
		Out.Refusal = TEXT("Shifting an affix takes a Crystal of Instability; 0 held.");
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(TEXT("Shifting an affix"), Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

FString UCataclysmForge::AffixNameOf(FName AffixRow, const UDataTable* AffixTable)
{
	const FCataclysmAffixRow* Row = AffixTable != nullptr && !AffixRow.IsNone()
		? AffixTable->FindRow<FCataclysmAffixRow>(AffixRow, TEXT("UCataclysmForge::AffixNameOf"), /*bWarnIfMissing=*/false)
		: nullptr;
	return Row != nullptr && !Row->AffixName.IsEmpty() ? Row->AffixName : AffixRow.ToString();
}

FString UCataclysmForge::AffixChangeQuestion(const FCataclysmForgeAffixChange& Plan, const UDataTable* AffixTable)
{
	const FString Cost = FString::Printf(TEXT("%s, residue %d to %d, %d day%s of the run"),
										 *ACataclysmDroppedItem::GoldName(Plan.Gold), FMath::RoundToInt(Plan.ResidueBefore),
										 FMath::RoundToInt(Plan.ResidueAfter), Plan.Days,
										 Plan.Days == 1 ? TEXT("") : TEXT("s"));
	if (Plan.bShift)
	{
		return FString::Printf(TEXT("Press again to shift affix %d, %s, to another stat, keeping tier %d and its roll: one "
									"Crystal of Instability, %s; the new stat is drawn."),
							   Plan.Affix + 1, *AffixNameOf(Plan.FromRow, AffixTable), Plan.Tier, *Cost);
	}
	return FString::Printf(TEXT("Press again to add %s at tier %d as affix %d, making the piece %s: Schematic Fragments, %s; "
								"its roll is drawn in the tier."),
						   *AffixNameOf(Plan.ToRow, AffixTable), Plan.Tier, Plan.Affix + 1,
						   *UCataclysmGems::RarityName(Plan.ToRarity), *Cost);
}

FName UCataclysmForge::MaterialFor(ECataclysmForgeDamageTypeCraft Craft)
{
	switch (Craft)
	{
	case ECataclysmForgeDamageTypeCraft::InfuseSpecific:	return FName(SovereignIngotRow);
	case ECataclysmForgeDamageTypeCraft::Change:			return FName(PrismaticCatalystRow);
	case ECataclysmForgeDamageTypeCraft::InfuseRandom:
	default:												return FName(ElementalCinderRow);
	}
}

bool UCataclysmForge::CarriesDamageTypes(const FCataclysmItem& Item, const UDataTable* BaseTable)
{
	const FCataclysmItemBaseRow* Row = BaseTable != nullptr && !Item.Base.IsNone()
		? BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("UCataclysmForge::CarriesDamageTypes"),
													/*bWarnIfMissing=*/false)
		: nullptr;
	return Row != nullptr && Row->MaxDamageTypes > 0 && !Row->WeaponType.IsEmpty();
}

TArray<FName> UCataclysmForge::DamageTypesOpenTo(const FCataclysmItem& Item, const UDataTable* WeaponSkillTable,
												 const UDataTable* BaseTable)
{
	TArray<FName> Open;
	const FCataclysmItemBaseRow* Row = BaseTable != nullptr && !Item.Base.IsNone()
		? BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("UCataclysmForge::DamageTypesOpenTo"),
													/*bWarnIfMissing=*/false)
		: nullptr;
	if (Row == nullptr || Row->MaxDamageTypes <= 0)
	{
		return Open;
	}
	// THE DROP'S POOL, the types designed for the weapon's type, less those it carries.
	for (const FName& Type : UCataclysmDropRoll::DamageTypesAvailableTo(WeaponSkillTable, Row->WeaponType))
	{
		if (!Item.DamageTypes.Contains(Type))
		{
			Open.Add(Type);
		}
	}
	return Open;
}

int32 UCataclysmForge::MaxDamageTypesOf(const FCataclysmItem& Item, const UDataTable* WeaponSkillTable,
										const UDataTable* BaseTable)
{
	const FCataclysmItemBaseRow* Row = BaseTable != nullptr && !Item.Base.IsNone()
		? BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("UCataclysmForge::MaxDamageTypesOf"),
													/*bWarnIfMissing=*/false)
		: nullptr;
	return Row != nullptr
		? UCataclysmDropRoll::MaxDamageTypesFor(
			  *Row, UCataclysmDropRoll::DamageTypesAvailableTo(WeaponSkillTable, Row->WeaponType).Num())
		: 0;
}

FCataclysmForgeDamageTypeWork UCataclysmForge::DamageTypeWorkOf(const FCataclysmItem& Item,
																 ECataclysmForgeDamageTypeCraft Craft, FName Chosen,
																 int32 Seed, const UDataTable* WeaponSkillTable,
																 const UDataTable* BaseTable, int32 MaterialHeld,
																 int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeDamageTypeWork Out;
	Out.Craft = Craft;
	Out.Material = MaterialFor(Craft);
	Out.MaterialHeld = FMath::Max(0, MaterialHeld);
	Out.ResidueBefore = Item.Residue;
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);
	Out.TypesHeld = Item.DamageTypes.Num();

	// EACH CRAFT'S ROW: its residue, its base days and its Base Gold, falling back on the constant the sheet mirrors.
	float Added = 0.0f;
	int32 BaseDays = 0;
	FString Doing;
	switch (Craft)
	{
	case ECataclysmForgeDamageTypeCraft::InfuseSpecific:
		Added = InfuseSpecificDamageTypeResidue;
		BaseDays = InfuseSpecificDamageTypeBaseDays;
		Out.Gold = GoldFor(BaseGoldOf(InfuseSpecificDamageTypeOperationRow, BaseGoldFor(InfuseSpecificDamageTypeBaseDays)),
						   Item.Residue, DifficultyTier);
		Doing = TEXT("Infusing a chosen damage type");
		break;
	case ECataclysmForgeDamageTypeCraft::Change:
		Added = ChangeDamageTypeResidue;
		BaseDays = ChangeDamageTypeBaseDays;
		Out.Gold = GoldFor(BaseGoldOf(ChangeDamageTypeOperationRow, BaseGoldFor(ChangeDamageTypeBaseDays)), Item.Residue,
						   DifficultyTier);
		Doing = TEXT("Changing a damage type");
		break;
	case ECataclysmForgeDamageTypeCraft::InfuseRandom:
	default:
		Added = InfuseRandomDamageTypeResidue;
		BaseDays = InfuseRandomDamageTypeBaseDays;
		Out.Gold = GoldFor(BaseGoldOf(InfuseRandomDamageTypeOperationRow, BaseGoldFor(InfuseRandomDamageTypeBaseDays)),
						   Item.Residue, DifficultyTier);
		Doing = TEXT("Infusing a random damage type");
		break;
	}
	Out.ResidueAfter = Item.Residue + Added;
	Out.Days = DaysFor(Item.Residue, BaseDays);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to craft.");
		return Out;
	}
	const FCataclysmItemBaseRow* Row = BaseTable != nullptr
		? BaseTable->FindRow<FCataclysmItemBaseRow>(Item.Base, TEXT("UCataclysmForge::DamageTypeWorkOf"),
													/*bWarnIfMissing=*/false)
		: nullptr;
	if (Row == nullptr)
	{
		Out.Refusal = TEXT("What that piece is cannot be found.");
		return Out;
	}
	// WEAPONS ONLY (Q19).
	if (!CarriesDamageTypes(Item, BaseTable))
	{
		Out.Refusal = TEXT("Only a weapon carries damage types.");
		return Out;
	}
	// THE WEAPON MADE AT CREATION, whose types every load sets back (issue #2845, option (a)).
	if (Item.bMadeAtCreation)
	{
		Out.Refusal = CreationWeaponRefusal;
		return Out;
	}
	Out.MaxTypes = MaxDamageTypesOf(Item, WeaponSkillTable, BaseTable);
	const TArray<FName> Open = DamageTypesOpenTo(Item, WeaponSkillTable, BaseTable);

	if (Craft == ECataclysmForgeDamageTypeCraft::Change)
	{
		// THE TYPE REPLACED IS THE PLAYER'S CHOICE; THE NEW ONE IS DRAWN AMONG THOSE THE WEAPON LACKS (Q22). A change
		// works at the maximum, since it leaves the count as it is.
		Out.FromType = Chosen;
		if (!Item.DamageTypes.Contains(Chosen))
		{
			Out.Refusal = FString::Printf(TEXT("That weapon does not carry %s."), *Chosen.ToString());
			return Out;
		}
	}
	else if (Item.DamageTypes.Num() >= Out.MaxTypes)
	{
		// AT THE DROP'S MAXIMUM AN INFUSION IS REFUSED, saying the maximum (Q20, Q21).
		Out.Refusal = FString::Printf(TEXT("That weapon carries its most damage types, %d."), Out.MaxTypes);
		return Out;
	}
	if (Open.IsEmpty())
	{
		Out.Refusal = TEXT("That weapon carries every damage type designed for it.");
		return Out;
	}
	if (Craft == ECataclysmForgeDamageTypeCraft::InfuseSpecific)
	{
		Out.ToType = Chosen;
		if (Item.DamageTypes.Contains(Chosen))
		{
			Out.Refusal = FString::Printf(TEXT("That weapon carries %s already."), *Chosen.ToString());
			return Out;
		}
		if (!Open.Contains(Chosen))
		{
			Out.Refusal = FString::Printf(TEXT("That weapon cannot carry %s."), *Chosen.ToString());
			return Out;
		}
	}
	else
	{
		// THE DRAW, evenly among the types the weapon can take and does not carry, as the drop draws one.
		const FRandomStream Stream(Seed);
		Out.ToType = Open[Stream.RandRange(0, Open.Num() - 1)];
	}

	if (Out.MaterialHeld < 1)
	{
		const TCHAR* Material = Craft == ECataclysmForgeDamageTypeCraft::InfuseSpecific ? TEXT("a Sovereign Ingot")
			: Craft == ECataclysmForgeDamageTypeCraft::Change							 ? TEXT("a Prismatic Catalyst")
																						 : TEXT("an Elemental Cinder");
		Out.Refusal = FString::Printf(TEXT("%s takes %s; 0 held."), *Doing, Material);
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(Doing, Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

FString UCataclysmForge::DamageTypeQuestion(const FCataclysmForgeDamageTypeWork& Plan)
{
	const FString Cost = FString::Printf(TEXT("%s, residue %d to %d, %d day%s of the run"),
										 *ACataclysmDroppedItem::GoldName(Plan.Gold), FMath::RoundToInt(Plan.ResidueBefore),
										 FMath::RoundToInt(Plan.ResidueAfter), Plan.Days,
										 Plan.Days == 1 ? TEXT("") : TEXT("s"));
	switch (Plan.Craft)
	{
	case ECataclysmForgeDamageTypeCraft::InfuseSpecific:
		return FString::Printf(TEXT("Press again to infuse %s: one Sovereign Ingot, %s."), *Plan.ToType.ToString(), *Cost);
	case ECataclysmForgeDamageTypeCraft::Change:
		return FString::Printf(TEXT("Press again to change %s to another damage type: one Prismatic Catalyst, %s; the new "
									"type is drawn among those the weapon lacks."),
							   *Plan.FromType.ToString(), *Cost);
	case ECataclysmForgeDamageTypeCraft::InfuseRandom:
	default:
		return FString::Printf(TEXT("Press again to infuse a random damage type: one Elemental Cinder, %s; the type is drawn "
									"among those the weapon can take."),
							   *Cost);
	}
}

FCataclysmForgeEnchantmentWork UCataclysmForge::ImprintOf(const FCataclysmItem& Item, int32 Affix, int32 ShardsHeld,
														  const UDataTable* GearRarityTable, int32 DifficultyTier,
														  int64 GoldHeld)
{
	FCataclysmForgeEnchantmentWork Out;
	Out.bImprint = true;
	Out.Affix = Affix;
	Out.AffixRow = Item.Affixes.IsValidIndex(Affix) ? Item.Affixes[Affix].Affix : NAME_None;
	Out.Material = FName(AetherialShardRow);
	Out.MaterialHeld = FMath::Max(0, ShardsHeld);
	Out.ResidueBefore = Item.Residue;
	Out.ResidueAfter = Item.Residue + ImprintResidue;
	Out.Days = DaysFor(Item.Residue, ImprintBaseDays);
	Out.Gold = GoldFor(BaseGoldOf(ImprintOperationRow, BaseGoldFor(ImprintBaseDays)), Item.Residue, DifficultyTier);
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to imprint.");
		return Out;
	}
	// EVERY SLOT FILLED, MASTERFUL AND UP (Q16): a piece below Masterful has a slot left empty, and an enchantment with a
	// slot left empty is no rarity (`UCataclysmItemValues::RarityOf`). The starting weapon has no rarity at all.
	if (!UCataclysmItemValues::RarityOf(Item.EnchantmentCount, Item.Affixes.Num(), Out.RarityBefore)
		|| Item.EnchantmentCount + Item.Affixes.Num() < UCataclysmItemValues::SlotsPerPiece)
	{
		Out.Refusal = TEXT("Only a piece with every slot filled, Masterful or better, takes an imprint.");
		return Out;
	}
	if (Item.Affixes.Num() == 0)
	{
		Out.Refusal = TEXT("That piece holds four enchantments; it has no affix left to give up.");
		return Out;
	}
	if (!Item.Affixes.IsValidIndex(Affix))
	{
		Out.Refusal = FString::Printf(TEXT("That piece has no affix %d."), Affix + 1);
		return Out;
	}
	if (!UCataclysmItemValues::RarityOf(Item.EnchantmentCount + 1, Item.Affixes.Num() - 1, Out.RarityAfter))
	{
		Out.Refusal = TEXT("That piece cannot take another enchantment.");
		return Out;
	}
	// THE RARITY AFTER MUST BE ONE THE PIECE'S GEAR LEVEL REACHES: "Legendary ... Requires gear level 4+", the design's
	// rarity table. A table that cannot be read refuses rather than reading as no gate.
	if (GearRarityTable == nullptr)
	{
		Out.Refusal = TEXT("The gear rarity table could not be read, so the gear level an imprint needs is not known.");
		return Out;
	}
	Out.GearLevelNeeded = UCataclysmDropRoll::GearLevelGateFor(GearRarityTable, Out.RarityAfter);
	if (Item.GearLevel < Out.GearLevelNeeded)
	{
		Out.Refusal = FString::Printf(TEXT("A %s piece needs gear level %d, and that piece is +%d. Upgrade it first."),
									  *UCataclysmGems::RarityName(Out.RarityAfter), Out.GearLevelNeeded, Item.GearLevel);
		return Out;
	}
	if (Out.MaterialHeld < 1)
	{
		Out.Refusal = TEXT("Imprinting an enchantment takes an Aetherial Shard; 0 held.");
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(TEXT("Imprinting an enchantment"), Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

FCataclysmForgeEnchantmentWork UCataclysmForge::ChangeNegativeOf(const FCataclysmItem& Item, int32 Pair,
																 int32 EssencesHeld, const UDataTable* PositiveTable,
																 int32 DifficultyTier, int64 GoldHeld)
{
	FCataclysmForgeEnchantmentWork Out;
	Out.Pair = Pair;
	Out.Material = FName(RegretEssenceRow);
	Out.MaterialHeld = FMath::Max(0, EssencesHeld);
	Out.ResidueBefore = Item.Residue;
	Out.ResidueAfter = Item.Residue + ChangeNegativeResidue;
	Out.Days = DaysFor(Item.Residue, ChangeNegativeBaseDays);
	Out.Gold = GoldFor(BaseGoldOf(ChangeNegativeOperationRow, BaseGoldFor(ChangeNegativeBaseDays)), Item.Residue,
					   DifficultyTier);
	Out.GoldHeld = FMath::Max<int64>(0, GoldHeld);

	if (Item.Base.IsNone())
	{
		Out.Refusal = TEXT("There is nothing there to change.");
		return Out;
	}
	if (!Item.Enchantments.IsValidIndex(Pair))
	{
		Out.Refusal = FString::Printf(TEXT("That piece has no enchantment %d."), Pair + 1);
		return Out;
	}
	Out.Before = Item.Enchantments[Pair];
	const FCataclysmEnchantmentRow* Positive = PositiveTable != nullptr
		? PositiveTable->FindRow<FCataclysmEnchantmentRow>(Out.Before.Positive, TEXT("UCataclysmForge::ChangeNegativeOf"),
														   /*bWarnIfMissing=*/false)
		: nullptr;
	if (Positive == nullptr)
	{
		Out.Refusal = TEXT("That enchantment is not in the enchantment table, so its drawback cannot be redrawn.");
		return Out;
	}
	// A SET'S DRAWBACK IS THE SET'S OWN, "paired and guaranteed", never drawn.
	if (UCataclysmDropRoll::EnchantmentSetId(*Positive) > 0)
	{
		Out.Refusal = TEXT("A set's drawback is part of the set; it is not changed.");
		return Out;
	}
	if (Out.MaterialHeld < 1)
	{
		Out.Refusal = TEXT("Changing an enchantment's drawback takes a Regret Essence; 0 held.");
		return Out;
	}
	if (Out.GoldHeld < Out.Gold)
	{
		Out.Refusal = GoldRefusal(TEXT("Changing an enchantment's drawback"), Out.Gold, Out.GoldHeld);
		return Out;
	}
	Out.bPossible = true;
	return Out;
}

bool UCataclysmForge::ApplyImprint(const FCataclysmItem& Item, int32 Affix, const FCataclysmRolledEnchantment& Pair,
								   const UDataTable* BaseTable, const UDataTable* WeaponSkillTable,
								   const UDataTable* EnchantmentEffectTable, FRandomStream& Stream,
								   FCataclysmItem& OutItem)
{
	if (!Item.Affixes.IsValidIndex(Affix) || Pair.Positive.IsNone() || Pair.Negative.IsNone())
	{
		return false;
	}
	FCataclysmItem Changed = Item;
	Changed.Affixes.RemoveAt(Affix);
	Changed.Enchantments.Add(Pair);
	// THE TWO "MUST AGREE" (`FCataclysmItem`), so the count is the pairs held.
	Changed.EnchantmentCount = Changed.Enchantments.Num();
	ECataclysmRarity After = ECataclysmRarity::Everyday;
	if (!UCataclysmItemValues::RarityOf(Changed.EnchantmentCount, Changed.Affixes.Num(), After))
	{
		return false;
	}
	// THE DAMAGE TYPES AN ENCHANTMENT ADDS, topped up as `UCataclysmDropRoll::RollItem` tops them up after its
	// enchantments: "This weapon has 2-4 damage types". A pair that adds none draws nothing more.
	const int32 Carried = UCataclysmDropRoll::DamageTypesCarriedBy(Changed.Enchantments, EnchantmentEffectTable);
	if (Carried > Changed.DamageTypes.Num() && WeaponSkillTable != nullptr)
	{
		const FCataclysmItemBaseRow* BaseRow = BaseTable != nullptr
			? BaseTable->FindRow<FCataclysmItemBaseRow>(Changed.Base, TEXT("UCataclysmForge::ApplyImprint"),
														/*bWarnIfMissing=*/false)
			: nullptr;
		if (BaseRow != nullptr)
		{
			UCataclysmDropRoll::TopUpDamageTypes(WeaponSkillTable, *BaseRow, Carried, Stream, Changed.DamageTypes);
		}
	}
	OutItem = MoveTemp(Changed);
	return true;
}

bool UCataclysmForge::DrawImprint(const FCataclysmItem& Item, const UDataTable* BaseTable,
								  const UDataTable* PositiveTable, const UDataTable* NegativeTable, int32 WalkSeedMask,
								  int32 Day, FCataclysmRolledEnchantment& OutPair, FRandomStream& OutStream)
{
	// SLICE F2'S SLOT AND SEED, its index the pairs the item holds before the craft.
	const FString Slot = SlotOf(Item, BaseTable);
	if (Slot.IsEmpty())
	{
		return false;
	}
	FRandomStream Stream(AffixCraftSeed(Item, ImprintSalt, Item.Enchantments.Num(), WalkSeedMask, Day));
	TArray<FCataclysmRolledEnchantment> Rolled;
	if (!UCataclysmDropRoll::RollEnchantments(PositiveTable, NegativeTable, Slot, 1, Stream, Rolled,
											  &Item.Enchantments)
		|| Rolled.Num() != 1)
	{
		return false;
	}
	OutPair = Rolled[0];
	OutStream = Stream;
	return true;
}

bool UCataclysmForge::DrawChangedNegative(const FCataclysmItem& Item, int32 Pair, const UDataTable* BaseTable,
										  const UDataTable* PositiveTable, const UDataTable* NegativeTable,
										  int32 WalkSeedMask, int32 Day, FCataclysmRolledEnchantment& OutPair)
{
	// SLICE F2'S SLOT AND SEED, its index the pair changed.
	const FString Slot = SlotOf(Item, BaseTable);
	if (Slot.IsEmpty())
	{
		return false;
	}
	FRandomStream Stream(AffixCraftSeed(Item, ChangeNegativeSalt, Pair, WalkSeedMask, Day));
	return UCataclysmDropRoll::DrawReplacementNegative(PositiveTable, NegativeTable, Slot, Item.Enchantments, Pair,
													   Stream, OutPair);
}

FString UCataclysmForge::EnchantmentWorkQuestion(const FCataclysmForgeEnchantmentWork& Plan, const FString& Named)
{
	const FString Cost = FString::Printf(TEXT("%s, residue %d to %d, %d day%s of the run"),
										 *ACataclysmDroppedItem::GoldName(Plan.Gold), FMath::RoundToInt(Plan.ResidueBefore),
										 FMath::RoundToInt(Plan.ResidueAfter), Plan.Days,
										 Plan.Days == 1 ? TEXT("") : TEXT("s"));
	if (Plan.bImprint)
	{
		return FString::Printf(TEXT("Press again to imprint an enchantment, giving up %s: one Aetherial Shard, %s; the "
									"piece becomes %s and the enchantment is drawn at random."),
							   *Named, *Cost, *UCataclysmGems::RarityName(Plan.RarityAfter));
	}
	return FString::Printf(TEXT("Press again to change the drawback of %s: one Regret Essence, %s; a different drawback "
								"is drawn at random."),
						   *Named, *Cost);
}

FString UCataclysmForge::NameOf(const FCataclysmItem& Item, const UDataTable* BaseTable, const UDataTable* AffixTable)
{
	// ITS WHOLE NAME, AND ELSE ITS BASE'S NAME: `UCataclysmItemName::NameOf` names only an item with a rarity, and a
	// piece with no affix and no enchantment -- the starting weapon -- has none, so its inventory cell calls it by its
	// base's `BaseName`, "Greataxe", and so does this. The row key only for a base the table does not hold.
	FString Name = UCataclysmItemName::NameOf(Item, BaseTable, AffixTable);
	if (Name.IsEmpty() && BaseTable)
	{
		if (const FCataclysmItemBaseRow* Row = BaseTable->FindRow<FCataclysmItemBaseRow>(
				Item.Base, TEXT("UCataclysmForge::NameOf"), /*bWarnIfMissing=*/false))
		{
			Name = Row->BaseName;
		}
	}
	return Name.IsEmpty() ? Item.Base.ToString() : Name;
}
