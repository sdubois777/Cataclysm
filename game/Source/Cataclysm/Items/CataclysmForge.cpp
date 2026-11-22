// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmForge.h"

#include "Cataclysm.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Items/CataclysmDropRoll.h"
// A gold amount as a player reads it, "2,055 gold", issue #2830.
#include "Items/CataclysmDroppedItem.h"
// A socket's type and a gem's name, issue #46 slice G4.
#include "Items/CataclysmGems.h"
#include "Items/CataclysmWearing.h"

const TCHAR* UCataclysmForge::SettingAgentRow = TEXT("Material_Jeweler_s_Setting_Agent");
const TCHAR* UCataclysmForge::OperationsTableAssetPath = TEXT("/Game/Data/DT_ForgeOperations.DT_ForgeOperations");
const TCHAR* UCataclysmForge::UpgradeOperationRow = TEXT("Operation_Upgrade_Item_Level_1_Tier");
const TCHAR* UCataclysmForge::SocketGemOperationRow = TEXT("Operation_Socket_Gem");
const TCHAR* UCataclysmForge::UnsocketGemOperationRow = TEXT("Operation_Unsocket_Gem");
const TCHAR* UCataclysmForge::CombineOperationRow = TEXT("Operation_Combine");
const TCHAR* UCataclysmForge::DismantlingDustRow = TEXT("Material_Dismantling_Dust");

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
