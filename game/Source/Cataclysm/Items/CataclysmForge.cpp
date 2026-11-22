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
const TCHAR* UCataclysmForge::CorruptedMoteRow = TEXT("Material_Corrupted_Mote");
const TCHAR* UCataclysmForge::PrimalSparkRow = TEXT("Material_Primal_Spark");
const TCHAR* UCataclysmForge::PotencyCrystalRow = TEXT("Material_Potency_Crystal");
const TCHAR* UCataclysmForge::RerollAffixValueOperationRow = TEXT("Operation_Reroll_Affix_Value");
const TCHAR* UCataclysmForge::PerfectRollOperationRow = TEXT("Operation_Perfect_Roll_Attempt");
const TCHAR* UCataclysmForge::UpgradeAffixTierOperationRow = TEXT("Operation_Upgrade_Affix_Tier");

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
