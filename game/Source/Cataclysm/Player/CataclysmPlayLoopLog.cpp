// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayLoopLog.h"

#include "Cataclysm.h"
#include "Character/CataclysmExperience.h"
#include "DayClock/CataclysmDayClock.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Items/CataclysmDroppedItem.h"
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
	for (const FCataclysmDayReport& Day : Days)
	{
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

	return FString::Printf(TEXT("while away, days %d to %d: dungeons resolved: %s; cities hit: %s; cities fallen: %s; %s"),
						   FromDay, ToDay, ResolvedParts.IsEmpty() ? TEXT("none") : *FString::Join(ResolvedParts, TEXT(", ")),
						   HitParts.IsEmpty() ? TEXT("none") : *FString::Join(HitParts, TEXT(", ")),
						   FallenParts.IsEmpty() ? TEXT("none") : *FString::Join(FallenParts, TEXT(", ")), *SurgeText);
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
										   int64 Price, int64 Held)
{
	const FString City = CataclysmPlayLoopLog::CityName(Run, CityId);
	if (Held < 0)
	{
		return FString::Printf(TEXT("bought '%s' on %s for %s gold; no gold balance to hold it"), *Upgrade, *City,
							   *ACataclysmDroppedItem::GoldAmountText(Price));
	}
	return FString::Printf(TEXT("bought '%s' on %s for %s gold; %s gold held"), *Upgrade, *City,
						   *ACataclysmDroppedItem::GoldAmountText(Price), *ACataclysmDroppedItem::GoldAmountText(Held));
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
