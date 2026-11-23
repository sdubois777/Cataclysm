// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayLoopLog.h"

#include "Cataclysm.h"
#include "Character/CataclysmExperience.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "AbilitySystemGlobals.h"
#include "Character/CataclysmPlayerBuild.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Interface/CataclysmItemComparison.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
// The three affix crafts, for their line, issue #47 slice F1.
#include "Items/CataclysmForge.h"
#include "Player/CataclysmGameInstance.h"
#include "Save/CataclysmSaveRecords.h"

const TCHAR* const FCataclysmPlayLoopLog::Prefix = TEXT("PLAY LOOP: ");

namespace CataclysmPlayLoopLog
{
	/** "1 day", "3 days". */
	FString DaysText(int32 Days)
	{
		return FString::Printf(TEXT("%d day%s"), Days, Days == 1 ? TEXT("") : TEXT("s"));
	}

	/** A city's name, or its identifier when the run has no such city. */
	FString CityName(const UCataclysmEmpireRun* Run, int32 CityId)
	{
		const FCataclysmCity* City = Run && Run->Map ? Run->Map->Find(CityId) : nullptr;
		return City ? City->Name : FString::Printf(TEXT("city %d"), CityId);
	}
}

void FCataclysmPlayLoopLog::Say(const FString& Line)
{
	UE_LOG(LogCataclysm, Display, TEXT("%s%s"), Prefix, *Line);
}

FString FCataclysmPlayLoopLog::FrontEndLine(int32 SavedCharacters)
{
	const FString Listed = SavedCharacters <= 0 ? FString(TEXT("no saved character"))
		: SavedCharacters == 1					 ? FString(TEXT("1 saved character"))
												 : FString::Printf(TEXT("%d saved characters"), SavedCharacters);
	return FString::Printf(TEXT("the front end is open: %s to continue, or New character to make one"), *Listed);
}

FString FCataclysmPlayLoopLog::NewCharacterLine(const FString& Name, FName WeaponType, FName DamageType)
{
	return FString::Printf(TEXT("new character '%s' (%s, %s) begun: opening the capital"), *Name,
						   *WeaponType.ToString(), *DamageType.ToString());
}

FString FCataclysmPlayLoopLog::ContinueLine(const FString& Name, bool bResumesAWalk)
{
	return FString::Printf(TEXT("continuing '%s': opening %s"), *Name,
						   bResumesAWalk ? TEXT("the dungeon level, to resume the walk it was saved in")
										 : TEXT("the capital"));
}

FString FCataclysmPlayLoopLog::DungeonLine(const UCataclysmEmpireRun* Run, int32 DungeonId)
{
	const FCataclysmDungeon* Dungeon = Run ? Run->FindDungeon(DungeonId) : nullptr;
	if (!Dungeon)
	{
		return FString();
	}

	// THE CITY SCREEN'S OWN WORDS FOR THE FLOORS AND THE DAYS (`UCataclysmCityScreenLayout::DungeonLinesFor`), so the
	// owner can find this dungeon on that screen. The Cataclysm's own dungeon carries no timer.
	const FString Bites = Dungeon->Type == ECataclysmDungeonType::Cataclysm || !Run->Clock
		? FString(TEXT("no timer"))
		: FString::Printf(TEXT("%.0f days until it bites"), Run->Clock->DaysUntilResolveFor(DungeonId));

	TArray<FString> Modifiers;
	for (const FName Key : Dungeon->Modifiers)
	{
		Modifiers.Add(UCataclysmDungeonModifierTable::NameOf(Key));
	}
	const FString Carried = Modifiers.IsEmpty() ? FString(TEXT("no modifiers"))
												: TEXT("modifiers ") + FString::Join(Modifiers, TEXT(", "));

	return FString::Printf(TEXT("dungeon %d, %s: %d floors, %s, %s"), DungeonId, *Run->NameOfDungeon(DungeonId),
						   Dungeon->Floors, *Bites, *Carried);
}

TArray<FString> FCataclysmPlayLoopLog::StandingLines(const UCataclysmEmpireRun* Run)
{
	TArray<FString> Lines;
	if (!Run)
	{
		return Lines;
	}
	TArray<int32> Ids;
	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		Ids.Add(Dungeon.DungeonId);
	}
	Ids.Sort();
	for (const int32 Id : Ids)
	{
		Lines.Add(TEXT("standing: ") + DungeonLine(Run, Id));
	}
	return Lines;
}

TMap<int32, FString> FCataclysmPlayLoopLog::NamesStanding(const UCataclysmEmpireRun* Run)
{
	TMap<int32, FString> Names;
	if (Run)
	{
		for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			Names.Add(Dungeon.DungeonId, Run->NameOfDungeon(Dungeon.DungeonId));
		}
	}
	return Names;
}

FString FCataclysmPlayLoopLog::AwayAccount(int32 FromDay, int32 ToDay, const TArray<FCataclysmDayReport>& Days,
										   const TMap<int32, FString>& NamesBefore, const UCataclysmEmpireRun* Run)
{
	using namespace CataclysmPlayLoopLog;

	if (ToDay <= FromDay && Days.IsEmpty())
	{
		return TEXT("while away: no day passed");
	}

	// EACH DUNGEON ONCE, IN THE ORDER THE DAYS RESOLVED THEM.
	TArray<int32> Resolved;
	// EACH CITY'S LOSSES ADDED UP, IN THE ORDER IT WAS FIRST HIT; and each city fallen once.
	TArray<int32> HitCities;
	TMap<int32, TPair<float, float>> Lost;
	TArray<int32> Fallen;
	int32 Surges = 0;
	int32 SurgeDungeons = 0;
	// AND THE CITY UPGRADES FINISHED, one entry per upgrade, in the order the days finished them. Issue #2825.
	TArray<int32> Finished;
	for (const FCataclysmDayReport& Day : Days)
	{
		Finished.Append(Day.UpgradesFinishedOn);
		for (const int32 Id : Day.Resolved)
		{
			Resolved.AddUnique(Id);
		}
		for (const FCataclysmCityHit& Hit : Day.Hits)
		{
			HitCities.AddUnique(Hit.CityId);
			TPair<float, float>& Sum = Lost.FindOrAdd(Hit.CityId);
			Sum.Key += Hit.DefenceLost;
			Sum.Value += Hit.PopulationLost;
		}
		for (const int32 CityId : Day.Fallen)
		{
			Fallen.AddUnique(CityId);
		}
		Surges += Day.bSurged ? 1 : 0;
		SurgeDungeons += Day.SurgeDungeons;
	}

	TArray<FString> ResolvedParts;
	for (const int32 Id : Resolved)
	{
		const FString* Name = NamesBefore.Find(Id);
		ResolvedParts.Add(Name ? FString::Printf(TEXT("%d (%s)"), Id, **Name) : FString::Printf(TEXT("%d"), Id));
	}
	TArray<FString> HitParts;
	for (const int32 CityId : HitCities)
	{
		const TPair<float, float>& Sum = Lost.FindChecked(CityId);
		HitParts.Add(FString::Printf(TEXT("%s lost %.0f defence and %.0f people"), *CityName(Run, CityId), Sum.Key,
									 Sum.Value));
	}
	TArray<FString> FallenParts;
	for (const int32 CityId : Fallen)
	{
		FallenParts.Add(CityName(Run, CityId));
	}
	const FString SurgeText = Surges == 0
		? FString(TEXT("no surge"))
		: FString::Printf(TEXT("%d surge%s brought %d dungeon%s"), Surges, Surges == 1 ? TEXT("") : TEXT("s"),
						  SurgeDungeons, SurgeDungeons == 1 ? TEXT("") : TEXT("s"));

	// ADDED ONLY WHEN THERE ARE ANY, so a line about days that finished nothing reads as it did before issue #2825.
	TArray<FString> FinishedParts;
	for (const int32 CityId : Finished)
	{
		FinishedParts.Add(CityName(Run, CityId));
	}
	const FString FinishedText = FinishedParts.IsEmpty()
		? FString()
		: FString::Printf(TEXT("; city upgrades finished: %s"), *FString::Join(FinishedParts, TEXT(", ")));

	return FString::Printf(TEXT("while away, days %d to %d: dungeons resolved: %s; cities hit: %s; cities fallen: %s; %s%s"),
						   FromDay, ToDay, ResolvedParts.IsEmpty() ? TEXT("none") : *FString::Join(ResolvedParts, TEXT(", ")),
						   HitParts.IsEmpty() ? TEXT("none") : *FString::Join(HitParts, TEXT(", ")),
						   FallenParts.IsEmpty() ? TEXT("none") : *FString::Join(FallenParts, TEXT(", ")), *SurgeText,
						   *FinishedText);
}

FString FCataclysmPlayLoopLog::CapitalLine(const UCataclysmEmpireRun* Run, int32 Level, int64 ExperienceIntoLevel,
										   const FString& Away)
{
	const FString Board = Run && Run->Map
		? FString::Printf(TEXT("on day %d, tier %d: %d dungeons standing, %d cities fallen"), Run->Day(),
						  Run->DifficultyTier, Run->DungeonCount(), Run->Map->FallenCityCount())
		: FString(TEXT("with no empire run"));
	return FString::Printf(TEXT("in the capital %s; level %d, %lld experience into it%s%s"), *Board, Level,
						   ExperienceIntoLevel, Away.IsEmpty() ? TEXT("") : TEXT("; "), *Away);
}

FString FCataclysmPlayLoopLog::WalkLine(const UCataclysmEmpireRun* Run, int32 DungeonId)
{
	const FString Named = DungeonLine(Run, DungeonId);
	return FString::Printf(TEXT("Walk pressed: %s; travelling to the dungeon level"),
						   Named.IsEmpty() ? *FString::Printf(TEXT("dungeon %d"), DungeonId) : *Named);
}

FString FCataclysmPlayLoopLog::EnteredLine(int32 DungeonId, int32 Floors, int32 DaysOwed)
{
	return FString::Printf(TEXT("entered dungeon %d, floor 1 of %d; the walk owes %s"), DungeonId, Floors,
						   *CataclysmPlayLoopLog::DaysText(DaysOwed));
}

FString FCataclysmPlayLoopLog::FloorLine(int32 DungeonId, int32 Floor, int32 Floors, int32 DaysOwed)
{
	return FString::Printf(TEXT("dungeon %d: down to floor %d of %d; the walk owes %s"), DungeonId, Floor, Floors,
						   *CataclysmPlayLoopLog::DaysText(DaysOwed));
}

FString FCataclysmPlayLoopLog::ClearLine(int32 DungeonId, int32 DayOfTheClear, double PointsAwarded, int32 Level)
{
	return FString::Printf(TEXT("cleared dungeon %d on day %d: +%s empire points; level %d: back to the capital"),
						   DungeonId, DayOfTheClear, *UCataclysmEmpireMapLayout::EmpirePointsText(PointsAwarded), Level);
}

FString FCataclysmPlayLoopLog::DeathLine(int32 DungeonId, int32 Floor, int32 Floors, int32 DaysTheDeathCost)
{
	return FString::Printf(TEXT("died in dungeon %d on floor %d of %d: the death cost %s: back to the capital"),
						   DungeonId, Floor, Floors, *CataclysmPlayLoopLog::DaysText(DaysTheDeathCost));
}

FString FCataclysmPlayLoopLog::GoldLine(int32 DungeonId, int64 PickedUp, int64 Held)
{
	if (Held < 0)
	{
		return FString::Printf(TEXT("dungeon %d's walk picked up %s gold; no gold balance to hold it"), DungeonId,
							   *ACataclysmDroppedItem::GoldAmountText(PickedUp));
	}
	return FString::Printf(TEXT("dungeon %d's walk picked up %s gold; %s gold held"), DungeonId,
						   *ACataclysmDroppedItem::GoldAmountText(PickedUp), *ACataclysmDroppedItem::GoldAmountText(Held));
}

FString FCataclysmPlayLoopLog::GoldLineIn(const UObject* WorldContext, int32 DungeonId)
{
	UCataclysmGameInstance* Instance = UCataclysmGameInstance::In(WorldContext);
	const int64 PickedUp = Instance != nullptr ? Instance->GoldPickedUpThisWalk : 0;

	// THE BALANCE OF THE CHARACTER PLAYING, as the empire map's status line reads it: a Solo Self-Found character's
	// own, or its lethality mode's.
	int64 Held = 0;
	if (!UCataclysmGameInstance::GoldHeldBy(WorldContext, Held))
	{
		Held = -1;
	}
	return GoldLine(DungeonId, PickedUp, Held);
}

FString FCataclysmPlayLoopLog::LevelGainedLine(int32 From, int32 To, int64 ExperienceIntoLevel)
{
	// AT THE MAXIMUM THE NEXT LEVEL COSTS NOTHING and the remainder is discarded (`UCataclysmExperience::Grant`), so
	// there is no figure left to say.
	if (UCataclysmExperience::IsMaxLevel(To))
	{
		return FString::Printf(TEXT("level %d reached, from level %d: the maximum level"), To, From);
	}
	return FString::Printf(TEXT("level %d reached, from level %d: %lld experience into it, %lld more to level %d"), To,
						   From, ExperienceIntoLevel, UCataclysmExperience::CostOfLevel(To + 1) - ExperienceIntoLevel,
						   To + 1);
}

FString FCataclysmPlayLoopLog::UpgradeLine(const FString& Upgrade, const UCataclysmEmpireRun* Run, int32 CityId,
										   int64 Price, int64 Held, int32 ReadyDay, bool bRushed)
{
	const FString City = CataclysmPlayLoopLog::CityName(Run, CityId);
	// THE DAY IT WILL BE FINISHED, since issue #2825; nothing for one finished at once. AND WHETHER IT WAS RUSHED, since
	// issue #2832.
	const FString Rushed = bRushed ? FString(TEXT("; rushed")) : FString();
	const FString Ready = Rushed + (ReadyDay == INDEX_NONE ? FString()
														   : FString::Printf(TEXT("; being built, ready on day %d"), ReadyDay));
	if (Held < 0)
	{
		return FString::Printf(TEXT("bought '%s' on %s for %s gold; no gold balance to hold it%s"), *Upgrade, *City,
							   *ACataclysmDroppedItem::GoldAmountText(Price), *Ready);
	}
	return FString::Printf(TEXT("bought '%s' on %s for %s gold; %s gold held%s"), *Upgrade, *City,
						   *ACataclysmDroppedItem::GoldAmountText(Price), *ACataclysmDroppedItem::GoldAmountText(Held),
						   *Ready);
}

FString FCataclysmPlayLoopLog::BailoutLine(const UCataclysmEmpireRun* Run, int32 CityId, int64 Fee, int64 Held,
										   bool bPaid)
{
	const FString City = CataclysmPlayLoopLog::CityName(Run, CityId);
	if (bPaid)
	{
		return FString::Printf(TEXT("Economic Bailout saved %s from falling for %s gold; %s gold held"), *City,
							   *ACataclysmDroppedItem::GoldAmountText(Fee), *ACataclysmDroppedItem::GoldAmountText(Held));
	}
	if (Held < 0)
	{
		return FString::Printf(TEXT("Economic Bailout did not save %s: its fee is %s gold and nothing holds a balance to "
									"pay it from"),
							   *City, *ACataclysmDroppedItem::GoldAmountText(Fee));
	}
	return FString::Printf(TEXT("Economic Bailout did not save %s: its fee is %s gold and %s is held"), *City,
						   *ACataclysmDroppedItem::GoldAmountText(Fee), *ACataclysmDroppedItem::GoldAmountText(Held));
}

FString FCataclysmPlayLoopLog::BribeLine(int32 DungeonId, int64 Fee, int64 Held)
{
	return FString::Printf(TEXT("Bribery reset dungeon %d's timer for %s gold; %s gold held"), DungeonId,
						   *ACataclysmDroppedItem::GoldAmountText(Fee), *ACataclysmDroppedItem::GoldAmountText(Held));
}

FString FCataclysmPlayLoopLog::BribeRefusedLine(int32 DungeonId, const FString& Why)
{
	return FString::Printf(TEXT("Bribery refused for dungeon %d: %s"), DungeonId, *Why);
}

FString FCataclysmPlayLoopLog::TreePointLine(const FString& Node, const FString& Branch, int32 Points, int32 MaxPoints,
											 double Banked)
{
	return FString::Printf(TEXT("bought a point in %s (%s): %d of %d; %s empire points banked"), *Node, *Branch, Points,
						   MaxPoints, *UCataclysmEmpireMapLayout::EmpirePointsText(Banked));
}

FString FCataclysmPlayLoopLog::TreeOptionLine(const FString& Option, const FString& Capstone)
{
	return FString::Printf(TEXT("chose %s for %s"), *Option, *Capstone);
}

FString FCataclysmPlayLoopLog::TreeRespecLine(int32 PointsBack, double Banked, int32 Days, int32 DayFrom, int32 DayTo)
{
	return FString::Printf(
		TEXT("respecced the empire upgrade tree: %d point%s back, %s points banked; it cost %d days, day %d to day %d"),
		PointsBack, PointsBack == 1 ? TEXT("") : TEXT("s"),
		*UCataclysmEmpireMapLayout::EmpirePointsText(Banked), Days, DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::PassivePointLine(const FString& Node, const FString& Tree, int32 Points,
												int32 MaxPoints, int32 Spent, int32 Available)
{
	return FString::Printf(TEXT("spent a passive point in %s (%s): %d of %d; %d of %d passive points spent"), *Node,
						   *Tree, Points, MaxPoints, Spent, Available);
}

FString FCataclysmPlayLoopLog::PassiveOptionLine(const FString& Option, const FString& Capstone, int32 Spent,
												 int32 Available)
{
	return FString::Printf(TEXT("took the option %s at the capstone %s; %d of %d passive points spent"), *Option,
						   *Capstone, Spent, Available);
}

FString FCataclysmPlayLoopLog::PassiveResetLine(int32 PointsBack, int32 ToSpend)
{
	return FString::Printf(TEXT("reset the passive tree: %d point%s back; %d to spend"), PointsBack,
						   PointsBack == 1 ? TEXT("") : TEXT("s"), ToSpend);
}

FString FCataclysmPlayLoopLog::AttributePointLine(int32 Count, const FString& Attribute, int32 InIt, int32 Unspent)
{
	return FString::Printf(TEXT("spent %d attribute point%s on %s: %d in it; %d unspent"), Count,
						   Count == 1 ? TEXT("") : TEXT("s"), *Attribute, InIt, Unspent);
}

FString FCataclysmPlayLoopLog::AttributeResetLine(int32 PointsBack, int32 Unspent)
{
	return FString::Printf(TEXT("reset the attribute points: %d point%s back; %d unspent"), PointsBack,
						   PointsBack == 1 ? TEXT("") : TEXT("s"), Unspent);
}

FString FCataclysmPlayLoopLog::SkillChosenLine(const FString& Skill, const FString& Slot)
{
	return FString::Printf(TEXT("put %s in the %s slot"), *Skill, *Slot);
}

FString FCataclysmPlayLoopLog::SkillForgottenLine(const FString& Slot)
{
	return FString::Printf(TEXT("forgot the %s slot's skill choice: it shows its default again"), *Slot);
}

FString FCataclysmPlayLoopLog::ForgeUpgradeLine(const FString& ItemName, int32 FromLevel, int32 ToLevel,
												int32 StonesLeft, int64 Gold, int32 ResidueBefore, int32 ResidueAfter,
												int32 Days, int32 DayFrom, int32 DayTo)
{
	return FString::Printf(
		TEXT("upgraded %s from +%d to +%d with two +%d stones, %d left, and %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
		*ItemName, FromLevel, ToLevel, ToLevel, StonesLeft, *ACataclysmDroppedItem::GoldName(Gold), ResidueBefore,
		ResidueAfter, Days, Days == 1 ? TEXT("") : TEXT("s"), DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::ForgeCombineLine(int32 FromLevel, int32 IntoLevel, int32 FromHeld, int32 IntoHeld,
												int64 Gold)
{
	return FString::Printf(TEXT("combined two +%d upgrade stones into one +%d for %s: %d +%d and %d +%d held"), FromLevel,
						   IntoLevel, *ACataclysmDroppedItem::GoldName(Gold), FromHeld, FromLevel, IntoHeld, IntoLevel);
}

FString FCataclysmPlayLoopLog::ForgeGemCombineLine(const FString& GemName, const FString& FromRarity,
												  const FString& IntoRarity, int32 FromHeld, int32 IntoHeld, int64 Gold)
{
	return FString::Printf(TEXT("combined two %s (%s) into one %s (%s) for %s: %d %s and %d %s held"), *GemName,
						   *FromRarity, *GemName, *IntoRarity, *ACataclysmDroppedItem::GoldName(Gold), FromHeld,
						   *FromRarity, IntoHeld, *IntoRarity);
}

FString FCataclysmPlayLoopLog::ForgeSocketLine(bool bUnsocket, const FString& GemName, const FString& Rarity,
											  const FString& SocketType, const FString& ItemName, int64 Gold,
											  int32 ResidueBefore, int32 ResidueAfter, int32 Days, int32 DayFrom,
											  int32 DayTo)
{
	// TWO PLAIN RETURNS, so `tools/tests/test_the_play_loop_is_logged_at_each_step.py` can read both formats.
	const TCHAR* Plural = Days == 1 ? TEXT("") : TEXT("s");
	if (bUnsocket)
	{
		return FString::Printf(TEXT("unsocketed %s (%s) from %s for %s, back to the bag; residue %d to %d; it cost %d day%s, day %d to day %d"),
							   *GemName, *Rarity, *ItemName, *ACataclysmDroppedItem::GoldName(Gold), ResidueBefore,
							   ResidueAfter, Days, Plural, DayFrom, DayTo);
	}
	return FString::Printf(TEXT("socketed %s (%s) into the %s socket of %s for %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
						   *GemName, *Rarity, *SocketType, *ItemName, *ACataclysmDroppedItem::GoldName(Gold), ResidueBefore,
						   ResidueAfter, Days, Plural, DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::ForgeSocketWorkLine(bool bAdd, int32 SocketNumber, const FString& FromType,
												   const FString& ToType, const FString& ItemName, int64 Gold,
												   int32 ResidueBefore, int32 ResidueAfter, int32 Days, int32 DayFrom,
												   int32 DayTo)
{
	// TWO PLAIN RETURNS, so `tools/tests/test_the_play_loop_is_logged_at_each_step.py` can read both formats.
	const TCHAR* Plural = Days == 1 ? TEXT("") : TEXT("s");
	if (bAdd)
	{
		return FString::Printf(TEXT("added socket %d to %s for %s, rolled %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
							   SocketNumber, *ItemName, *ACataclysmDroppedItem::GoldName(Gold), *ToType, ResidueBefore,
							   ResidueAfter, Days, Plural, DayFrom, DayTo);
	}
	return FString::Printf(TEXT("made socket %d of %s %s, from %s, for %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
						   SocketNumber, *ItemName, *ToType, *FromType, *ACataclysmDroppedItem::GoldName(Gold),
						   ResidueBefore, ResidueAfter, Days, Plural, DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::ForgeAffixLine(ECataclysmForgeAffixCraft Craft, int32 AffixNumber, const FString& ItemName,
											  int64 Gold, int32 FromTier, int32 ToTier, int32 FromPercent,
											  int32 ToPercent, int32 ResidueBefore, int32 ResidueAfter, int32 Days,
											  int32 DayFrom, int32 DayTo)
{
	// THREE PLAIN RETURNS, EACH FORMAT ONE LITERAL, so `tools/tests/test_the_play_loop_is_logged_at_each_step.py` can
	// read each one.
	const TCHAR* Plural = Days == 1 ? TEXT("") : TEXT("s");
	const FString Paid = ACataclysmDroppedItem::GoldName(Gold);
	if (Craft == ECataclysmForgeAffixCraft::PerfectRoll)
	{
		return FString::Printf(TEXT("perfected affix %d of %s for %s, its roll %d to %d percent of tier %d; residue %d to %d; it cost %d day%s, day %d to day %d"),
							   AffixNumber, *ItemName, *Paid, FromPercent, ToPercent, FromTier, ResidueBefore,
							   ResidueAfter, Days, Plural, DayFrom, DayTo);
	}
	if (Craft == ECataclysmForgeAffixCraft::UpgradeTier)
	{
		return FString::Printf(TEXT("raised affix %d of %s for %s from tier %d to tier %d, its roll kept at %d percent; residue %d to %d; it cost %d day%s, day %d to day %d"),
							   AffixNumber, *ItemName, *Paid, FromTier, ToTier, ToPercent, ResidueBefore, ResidueAfter,
							   Days, Plural, DayFrom, DayTo);
	}
	return FString::Printf(TEXT("rerolled affix %d of %s for %s, its roll %d to %d percent of tier %d; residue %d to %d; it cost %d day%s, day %d to day %d"),
						   AffixNumber, *ItemName, *Paid, FromPercent, ToPercent, FromTier, ResidueBefore, ResidueAfter,
						   Days, Plural, DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::ForgeAffixChangeLine(bool bShift, int32 AffixNumber, const FString& FromName,
													const FString& ToName, const FString& ItemName, int64 Gold,
													int32 Tier, const FString& Rarity, int32 ResidueBefore,
													int32 ResidueAfter, int32 Days, int32 DayFrom, int32 DayTo)
{
	// TWO PLAIN RETURNS, EACH FORMAT ONE LITERAL, so `tools/tests/test_the_play_loop_is_logged_at_each_step.py` can
	// read each one.
	const TCHAR* Plural = Days == 1 ? TEXT("") : TEXT("s");
	const FString Paid = ACataclysmDroppedItem::GoldName(Gold);
	if (bShift)
	{
		return FString::Printf(TEXT("shifted affix %d of %s for %s from %s to %s, tier %d kept; residue %d to %d; it cost %d day%s, day %d to day %d"),
							   AffixNumber, *ItemName, *Paid, *FromName, *ToName, Tier, ResidueBefore, ResidueAfter,
							   Days, Plural, DayFrom, DayTo);
	}
	return FString::Printf(TEXT("added %s at tier %d to %s for %s as affix %d, making it %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
						   *ToName, Tier, *ItemName, *Paid, AffixNumber, *Rarity, ResidueBefore, ResidueAfter, Days, Plural,
						   DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::ForgeOptionsLine(const FString& ItemName, bool bStabilizer, int32 Shards,
												float ResidueSaved, int32 DaysSaved, int64 Gold)
{
	// THREE PLAIN RETURNS, EACH FORMAT ONE LITERAL, so `tools/tests/test_the_play_loop_is_logged_at_each_step.py` can
	// read each one. A halved residue can save a half, so the residue is written whole when whole and to one place else.
	const FString Saved = FMath::IsNearlyEqual(ResidueSaved, FMath::RoundToFloat(ResidueSaved), 0.01f)
		? FString::Printf(TEXT("%d"), FMath::RoundToInt(ResidueSaved))
		: FString::Printf(TEXT("%.1f"), ResidueSaved);
	const FString Paid = ACataclysmDroppedItem::GoldName(Gold);
	const TCHAR* ShardWord = Shards == 1 ? TEXT("") : TEXT("s");
	const TCHAR* DayWord = DaysSaved == 1 ? TEXT("") : TEXT("s");
	if (bStabilizer && Shards > 0)
	{
		return FString::Printf(TEXT("used a Chaos Stabilizer and %d Tainted Shard%s on %s for %s: %s residue and %d day%s saved"),
							   Shards, ShardWord, *ItemName, *Paid, *Saved, DaysSaved, DayWord);
	}
	if (bStabilizer)
	{
		return FString::Printf(TEXT("used a Chaos Stabilizer on %s for %s: %s residue saved"), *ItemName, *Paid, *Saved);
	}
	return FString::Printf(TEXT("used %d Tainted Shard%s on %s for %s: %d day%s saved"), Shards, ShardWord, *ItemName, *Paid,
						   DaysSaved, DayWord);
}

FString FCataclysmPlayLoopLog::ForgeCleanseLine(const FString& ItemName, int64 Gold, int32 ResidueBefore,
												int32 ResidueAfter, int32 Days, int32 DayFrom, int32 DayTo)
{
	return FString::Printf(TEXT("cleansed %s for %s: residue %d to %d; it cost %d day%s, day %d to day %d"), *ItemName,
						   *ACataclysmDroppedItem::GoldName(Gold), ResidueBefore, ResidueAfter, Days,
						   Days == 1 ? TEXT("") : TEXT("s"), DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::ForgeDismantleLine(const FString& ItemName, const FString& Given, int32 Day)
{
	return FString::Printf(TEXT("dismantled %s into %s; no gold and no days, day %d"), *ItemName, *Given, Day);
}

FString FCataclysmPlayLoopLog::ForgeDamageTypeLine(ECataclysmForgeDamageTypeCraft Craft, const FString& ItemName,
												   const FString& FromType, const FString& ToType, int64 Gold,
												   int32 ResidueBefore, int32 ResidueAfter, int32 Days, int32 DayFrom,
												   int32 DayTo)
{
	// THREE PLAIN RETURNS, EACH FORMAT ONE LITERAL, so `tools/tests/test_the_play_loop_is_logged_at_each_step.py` can
	// read each one.
	const TCHAR* Plural = Days == 1 ? TEXT("") : TEXT("s");
	const FString Paid = ACataclysmDroppedItem::GoldName(Gold);
	if (Craft == ECataclysmForgeDamageTypeCraft::Change)
	{
		return FString::Printf(TEXT("changed %s on %s to %s for %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
							   *FromType, *ItemName, *ToType, *Paid, ResidueBefore, ResidueAfter, Days, Plural, DayFrom,
							   DayTo);
	}
	if (Craft == ECataclysmForgeDamageTypeCraft::InfuseSpecific)
	{
		return FString::Printf(TEXT("infused %s with %s, chosen, for %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
							   *ItemName, *ToType, *Paid, ResidueBefore, ResidueAfter, Days, Plural, DayFrom, DayTo);
	}
	return FString::Printf(TEXT("infused %s with %s, drawn, for %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
						   *ItemName, *ToType, *Paid, ResidueBefore, ResidueAfter, Days, Plural, DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::ForgeEnchantmentWorkLine(bool bImprint, const FString& ItemName, const FString& Positive,
													   const FString& Drawback, const FString& GivenUpOrReplaced,
													   int64 Gold, int32 ResidueBefore, int32 ResidueAfter, int32 Days,
													   int32 DayFrom, int32 DayTo)
{
	// TWO PLAIN RETURNS, so `tools/tests/test_the_play_loop_is_logged_at_each_step.py` can read both formats.
	const TCHAR* Plural = Days == 1 ? TEXT("") : TEXT("s");
	if (bImprint)
	{
		return FString::Printf(TEXT("imprinted %s, drawback %s, on %s, giving up %s, for %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
							   *Positive, *Drawback, *ItemName, *GivenUpOrReplaced, *ACataclysmDroppedItem::GoldName(Gold),
							   ResidueBefore, ResidueAfter, Days, Plural, DayFrom, DayTo);
	}
	return FString::Printf(TEXT("changed the drawback of %s on %s from %s to %s for %s; residue %d to %d; it cost %d day%s, day %d to day %d"),
						   *Positive, *ItemName, *GivenUpOrReplaced, *Drawback, *ACataclysmDroppedItem::GoldName(Gold),
						   ResidueBefore, ResidueAfter, Days, Plural, DayFrom, DayTo);
}

FString FCataclysmPlayLoopLog::PickedUpLine(const FString& ItemName, int32 GearLevel)
{
	return FString::Printf(TEXT("picked up %s, +%d"), *ItemName, GearLevel);
}

FCataclysmPlayLoopLog::FHits FCataclysmPlayLoopLog::HitsOf(const UCataclysmEquipmentComponent& Worn)
{
	FHits Hits;
	const UAbilitySystemComponent* Abilities =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Worn.GetOwner());
	if (!Abilities)
	{
		return Hits;
	}

	// THE CREATION DAMAGE TYPE AS THE COMPARISON TAKES IT, from the player's numbers.
	FCataclysmPlayerBuild Numbers;
	const FName Creation = Worn.PlayerNumbers(Numbers) ? Numbers.CreationDamageType : FName();
	Hits.Plain = UCataclysmItemComparison::BasicHitOf(Abilities, Worn, Creation);
	Hits.Families = UCataclysmItemComparison::RunFamiliesIn(&Worn);
	for (const FName Family : Hits.Families)
	{
		Hits.OnFamilies.Add(UCataclysmItemComparison::BasicHitOnAFamilyOf(Abilities, Worn, Creation, Family));
	}
	return Hits;
}

FString FCataclysmPlayLoopLog::WoreLine(const FString& ItemName, const FString& SlotName, const FHits& Before,
										const FHits& After)
{
	// EACH FAMILY BOTH SIDES PRICED, in the order the first gives them.
	FString OnFamilies;
	for (int32 Index = 0; Index < Before.Families.Num(); ++Index)
	{
		const int32 InAfter = After.Families.IndexOfByKey(Before.Families[Index]);
		if (!Before.OnFamilies.IsValidIndex(Index) || !After.OnFamilies.IsValidIndex(InAfter))
		{
			continue;
		}
		OnFamilies += FString::Printf(TEXT("; on a %s creature's health %s to %s"), *Before.Families[Index].ToString(),
									  *UCataclysmCharacterSheetLayout::Number(Before.OnFamilies[Index]),
									  *UCataclysmCharacterSheetLayout::Number(After.OnFamilies[InAfter]));
	}

	// ONE RETURNED FORMAT, which `tools/tests/test_the_play_loop_is_logged_at_each_step.py` reads to match the
	// checklist's quotation against.
	return FString::Printf(TEXT("wore %s in %s: basic attack hit %s to %s%s"), *ItemName, *SlotName,
						   *UCataclysmCharacterSheetLayout::Number(Before.Plain),
						   *UCataclysmCharacterSheetLayout::Number(After.Plain), *OnFamilies);
}

FString FCataclysmPlayLoopLog::SkillLevelLine(const FString& Skill, int32 From, int32 To)
{
	return FString::Printf(TEXT("skill %s reached level %d, from level %d"), *Skill, To, From);
}
