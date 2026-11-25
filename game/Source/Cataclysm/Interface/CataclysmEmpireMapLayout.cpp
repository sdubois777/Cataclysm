// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireMapLayout.h"

#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmRoster.h"
#include "Empire/CataclysmSurge.h"
#include "Interface/CataclysmWalkFromTheScreen.h"
#include "Items/CataclysmDroppedItem.h"
#include "Dungeon/CataclysmEnemyScore.h"
#include "Dungeon/CataclysmPowerGapRewards.h"
#include "Player/CataclysmGameInstance.h"
#include "Save/CataclysmSaveRecords.h"

int32 UCataclysmEmpireMapLayout::CellsAcross()
{
	// DERIVED FROM THE MAP'S OWN RADIUS AND NOT WRITTEN DOWN AGAIN. The lattice
	// runs from -3 to 3 on each axis, so it is seven cells across; a radius that
	// ever changed would change the drawing with it rather than leaving a stale
	// seven here.
	return 2 * UCataclysmEmpireMap::Radius + 1;
}

FVector2D UCataclysmEmpireMapLayout::DiamondSize()
{
	// THE CENTRES SPAN SIX CELLS, NOT SEVEN, and then half a city hangs off each
	// end. Measuring it as seven cells would be close enough to look right and
	// would leave the diamond slightly smaller than the panel it was fitted to,
	// which is a margin nobody asked for.
	const float Cells = static_cast<float>(CellsAcross() - 1);

	return FVector2D(Cells * CellWidthPx + CityWidthPx,
					 Cells * CellHeightPx + CityHeightPx);
}

float UCataclysmEmpireMapLayout::ScaleToFit(FVector2D PanelSize)
{
	const FVector2D Diamond = DiamondSize();
	if (Diamond.X <= 0.0 || Diamond.Y <= 0.0)
	{
		return SmallestScale;
	}

	const double Room = 2.0 * FitMarginPx;
	const double Wide = PanelSize.X - Room;
	const double Tall = PanelSize.Y - Room;

	if (Wide <= 0.0 || Tall <= 0.0)
	{
		// A PANEL SMALLER THAN ITS OWN MARGINS. It happens while a screen is
		// being laid out and before Slate has given it a size, and the smallest
		// scale is a poor drawing rather than a division by nothing.
		return SmallestScale;
	}

	return ClampScale(static_cast<float>(
		FMath::Min(Wide / Diamond.X, Tall / Diamond.Y)));
}

FVector2D UCataclysmEmpireMapLayout::PositionFor(int32 R, int32 C, float Scale,
												 FVector2D PanelSize)
{
	const FVector2D Centre = PanelSize * 0.5;

	// THE ROW GROWS DOWNWARDS AND THE COLUMN RIGHTWARDS, which is what turns the
	// taxicab ball into the diamond the design document draws: the Pillar at
	// (0,0) lands in the middle, and a cell at (-3,0) lands three rows above it.
	return Centre + FVector2D(C * CellWidthPx * Scale, R * CellHeightPx * Scale);
}

FVector2D UCataclysmEmpireMapLayout::CitySize(float Scale)
{
	return FVector2D(CityWidthPx * Scale, CityHeightPx * Scale);
}

float UCataclysmEmpireMapLayout::ClampScale(float Scale)
{
	return FMath::Clamp(Scale, SmallestScale, LargestScale);
}

ECataclysmCityMark UCataclysmEmpireMapLayout::MarkFor(
	const UCataclysmEmpireMap* Map, int32 CityId)
{
	if (Map == nullptr)
	{
		return ECataclysmCityMark::Sealed;
	}

	const FCataclysmCity* City = Map->Find(CityId);
	if (City == nullptr)
	{
		return ECataclysmCityMark::Sealed;
	}

	if (City->bFallen)
	{
		return ECataclysmCityMark::Fallen;
	}

	// THE MAP'S OWN ANSWER. `IsExposed` is what decides where a surge can land,
	// so asking anything else here would show the player a rule the game does
	// not obey.
	return Map->IsExposed(CityId) ? ECataclysmCityMark::Exposed
								  : ECataclysmCityMark::Sealed;
}

FString UCataclysmEmpireMapLayout::CityLabel(const UCataclysmEmpireMap* Map,
											 int32 CityId)
{
	if (Map == nullptr)
	{
		return FString();
	}

	const FCataclysmCity* City = Map->Find(CityId);
	if (City == nullptr)
	{
		return FString();
	}

	const FString Tier = UCataclysmEmpireMap::TierName(City->Tier);

	if (City->bFallen)
	{
		return FString::Printf(TEXT("%s lost"), *Tier);
	}

	// ROUNDED UP, SO A CITY WITH ANYTHING LEFT NEVER READS AS 0%. A player told
	// a city is at nothing would stop defending it, and a city at 0.4% is still
	// standing and still savable.
	//
	// OF THE MAXIMUM WITH THE EMPIRE UPGRADE TREE. Issue #2741 slice 3a. A point bought mid-run keeps the share, since
	// issue #2859: an Outpost at 80% reads 80% after a point of Foundation, at 840 of 1050.
	const int32 Percent = FMath::CeilToInt(Map->DefenceFractionOf(*City) * 100.0f);

	return FString::Printf(TEXT("%s %d%%"), *Tier, FMath::Clamp(Percent, 0, 100));
}

bool UCataclysmEmpireMapLayout::CityOpensItsScreen(const UCataclysmEmpireMap* Map, int32 CityId)
{
	// EVERY CITY, A FALLEN ONE INCLUDED: its screen lists the Fallen City dungeon standing on it, with the Walk that
	// retakes it, and an erased one's says it can never be retaken. Issue #2866.
	return Map != nullptr && Map->Find(CityId) != nullptr;
}

bool UCataclysmEmpireMapLayout::IsCityDimmed(const UCataclysmEmpireMap* Map, int32 CityId)
{
	return MarkFor(Map, CityId) == ECataclysmCityMark::Fallen;
}

int32 UCataclysmEmpireMapLayout::FallenCityDungeonOn(const UCataclysmEmpireRun* Run, int32 CityId)
{
	if (Run == nullptr)
	{
		return INDEX_NONE;
	}

	for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
	{
		if (Dungeon.CityId == CityId && Dungeon.Type == ECataclysmDungeonType::FallenCity)
		{
			return Dungeon.DungeonId;
		}
	}

	return INDEX_NONE;
}

FString UCataclysmEmpireMapLayout::FallenCityDetail(const UCataclysmEmpireRun* Run, int32 CityId)
{
	const FCataclysmCity* City = Run != nullptr && Run->Map != nullptr ? Run->Map->Find(CityId) : nullptr;
	if (City == nullptr || !City->bFallen)
	{
		return FString();
	}

	// AN ERASED CITY LEAVES NO FALLEN CITY DUNGEON (`UCataclysmEmpireRun::AddFallenCityDungeon`), and `Retake` refuses it.
	if (City->bErased)
	{
		return TEXT("erased by the Void, it can never be retaken");
	}

	// HOW IT COMES BACK, while the dungeon that brings it back stands on it. Issue #2866.
	return FallenCityDungeonOn(Run, CityId) != INDEX_NONE
		? FString(TEXT("fallen: walk its Fallen City dungeon to retake it (press the city, then Walk)"))
		: FString(TEXT("fallen"));
}

FString UCataclysmEmpireMapLayout::RiftMark(const UCataclysmEmpireRun* Run, int32 CityId)
{
	// THE RUN'S OWN LIST, BY `FCataclysmDungeon::IsARift`, and no new state. Issue #51 slice B2.
	const int32 Rifts = Run != nullptr ? Run->RiftsOn(CityId) : 0;
	if (Rifts <= 0)
	{
		return FString();
	}
	return Rifts == 1 ? FString(TEXT(", Rift")) : FString::Printf(TEXT(", %d Rifts"), Rifts);
}

FString UCataclysmEmpireMapLayout::StandingPart(const UCataclysmEmpireRun* Run)
{
	if (Run == nullptr || Run->Map == nullptr)
	{
		return FString();
	}

	return StandingWords(Run->DungeonCount(), QuestDungeonsStanding(Run));
}

FString UCataclysmEmpireMapLayout::StandingWords(int32 Standing, int32 QuestStanding)
{
	// NOTHING IN BRACKETS WHEN NO QUEST DUNGEON STANDS. Issue #2800, ruled 2026-11-21.
	const FString Kinds = QuestStanding > 0 ? FString::Printf(TEXT(" (%d Quest)"), QuestStanding) : FString();
	return FString::Printf(TEXT("%d dungeon%s standing%s"), Standing, Standing == 1 ? TEXT("") : TEXT("s"), *Kinds);
}

int32 UCataclysmEmpireMapLayout::QuestDungeonsStanding(const UCataclysmEmpireRun* Run)
{
	// THE RUN'S OWN LIST, BY EACH DUNGEON'S KIND, and no new state. Issue #2800, ruled 2026-11-21.
	int32 Quest = 0;
	if (Run != nullptr)
	{
		for (const FCataclysmDungeon& Dungeon : Run->Dungeons)
		{
			Quest += Dungeon.Type == ECataclysmDungeonType::Quest ? 1 : 0;
		}
	}
	return Quest;
}

FString UCataclysmEmpireMapLayout::CityDetailDungeonLine(const UCataclysmEmpireRun* Run, int32 DungeonId)
{
	const FCataclysmDungeon* Dungeon = Run != nullptr ? Run->FindDungeon(DungeonId) : nullptr;
	if (Dungeon == nullptr || Run->Clock == nullptr)
	{
		return FString();
	}

	// A RIFT IS NAMED AS ONE, WITH THE DAYS UNTIL IT NEXT SENDS A DUNGEON, since issue #51 slice B2: "Infernal Rift of 4
	// floors, 3 days left, sends a dungeon in 2 days". "Infernal Rift" is the floor panel's name for it (slice B1). Its
	// sub-type is not named: every Rift is Elite.
	if (Dungeon->IsARift())
	{
		const int32 Sends = Run->DaysUntilRiftSends(DungeonId);
		return FString::Printf(TEXT("Infernal Rift of %d floors, %.0f days left, sends a dungeon in %d day%s"),
							   Dungeon->Floors, Run->Clock->DaysUntilResolveFor(DungeonId), Sends,
							   Sends == 1 ? TEXT("") : TEXT("s"));
	}

	// THE SUB-TYPE FOR EVERY DUNGEON, AND THE KIND AFTER IT FOR A QUEST DUNGEON ONLY. Issue #2800, ruled 2026-11-21.
	// `SubTypeName` answers nothing for `None`, so a dungeon without one reads as the line always did.
	TArray<FString> Words;
	const FString SubType = UCataclysmSurgeScheduler::SubTypeName(Dungeon->SubType);
	if (!SubType.IsEmpty())
	{
		Words.Add(SubType);
	}
	if (Dungeon->Type == ECataclysmDungeonType::Quest)
	{
		Words.Add(UCataclysmSurgeScheduler::KindName(Dungeon->Type));
	}
	Words.Add(TEXT("dungeon"));

	return FString::Printf(TEXT("%s of %d floors, %.0f days left"), *FString::Join(Words, TEXT(" ")), Dungeon->Floors,
						   Run->Clock->DaysUntilResolveFor(DungeonId));
}

FString UCataclysmEmpireMapLayout::ProgressLine(const UCataclysmEmpireRun* Run)
{
	// A RUN THAT HAS NOT BEGUN SAYS NOTHING, rather than "no dungeons cleared".
	// The two states look the same in the counters and are not the same thing,
	// and a screen with no run already says so in its own words.
	if (Run == nullptr || Run->Map == nullptr)
	{
		return FString();
	}

	const int32 Cleared = Run->DungeonsCleared;
	const int32 Objectives = Run->QuestObjectives;

	// THE PLURALS ARE WRITTEN OUT because this line is read by a person and
	// "1 Quest dungeons cleared" is the kind of thing that makes a screen look
	// unfinished.
	const FString ClearedPart = Cleared == 0
		? FString(TEXT("No dungeons cleared yet"))
		: FString::Printf(TEXT("%d dungeon%s cleared"), Cleared,
						  Cleared == 1 ? TEXT("") : TEXT("s"));

	// QUEST DUNGEONS CLEARED, NOT "QUEST OBJECTIVES". Issue #2800, ruled 2026-11-21: only a Quest dungeon's clear raises
	// `QuestObjectives`, and "1 quest objective" beside "2 dungeons standing" was read as one Quest dungeon standing.
	const FString ObjectivePart = Objectives == 0
		? FString(TEXT("no Quest dungeons cleared"))
		: FString::Printf(TEXT("%d Quest dungeon%s cleared"), Objectives,
						  Objectives == 1 ? TEXT("") : TEXT("s"));

	// HOW FAR THE PLAYER IS FROM THE CATACLYSM BOSS DUNGEON, since issue #1324
	// slice 6c: the Cataclysms finished of those the run faces, and then what
	// opens it or that it has opened. Nothing when the run faces none, which no
	// begun run does: `Begin` always draws at least one.
	const int32 Facing = Run->ActiveCataclysms.Num();
	if (Facing <= 0)
	{
		return FString::Printf(TEXT("%s, %s"), *ClearedPart, *ObjectivePart);
	}

	const int32 Finished = Run->CataclysmsComplete();
	const int32 Needed = Run->CataclysmDungeonRequirement();

	FString BossPart;
	if (Run->bCataclysmDungeonOpened)
	{
		// ONLY A CLEAR TAKES IT OFF THE BOARD. `RemoveDungeon`'s other callers are
		// a resolve, which it never reaches, a city's fall and a city upgrade,
		// which both ask `DungeonsOn` a real city and never list it.
		BossPart = Run->FindCataclysmDungeon() != nullptr
			? FString(TEXT("the Cataclysm boss dungeon is open"))
			: FString(TEXT("the Cataclysm boss dungeon has been cleared"));
	}
	else if (Finished >= Needed)
	{
		// MET AND NOT OPENED: a run record from before version 4. See the header.
		BossPart = TEXT("the boss dungeon opens on the next quest dungeon cleared");
	}
	else
	{
		BossPart = FString::Printf(TEXT("the boss dungeon opens when %d %s finished"),
								   Needed, Needed == 1 ? TEXT("is") : TEXT("are"));
	}

	return FString::Printf(TEXT("%s, %s, %d of %d Cataclysm%s finished; %s"),
						   *ClearedPart, *ObjectivePart, Finished, Facing,
						   Facing == 1 ? TEXT("") : TEXT("s"), *BossPart);
}

FString UCataclysmEmpireMapLayout::EmpirePointsText(double Points)
{
	// DOWN TO A TENTH, after forgiving noise below a millionth: see the header.
	const double Tenths = FMath::FloorToDouble(FMath::Max(0.0, Points) * 10.0 + 1.0e-6);
	return FString::Printf(TEXT("%.1f"), Tenths / 10.0);
}

FString UCataclysmEmpireMapLayout::EmpirePointsLine(double Banked, double LastAwarded)
{
	const FString Bank = FString::Printf(TEXT("%s empire points banked"), *EmpirePointsText(Banked));

	// AN AWARD OF NOTHING IS NOT SHOWN: no clear yet this session, or a defeat with nobody left living. Shown, "+0.0"
	// would read as a fault in the award rather than as the rule.
	if (LastAwarded <= 0.0)
	{
		return Bank;
	}

	return FString::Printf(TEXT("%s (+%s from the last dungeon defeated)"), *Bank, *EmpirePointsText(LastAwarded));
}

FString UCataclysmEmpireMapLayout::EmpirePointsLineIn(const UObject* WorldContext, const UCataclysmEmpireRun* Run)
{
	// THE BANK `UCataclysmGameInstance::EmpireTreeHeldBy` NAMES: the partition's, or a Solo Self-Found character's own
	// since issue #2741 slice 1c. Until then a Solo Self-Found character's line read "Solo Self-Found: empire points are
	// not banked yet".
	FCataclysmHeldEmpireTree Held;
	if (!UCataclysmGameInstance::EmpireTreeHeldBy(WorldContext, Held))
	{
		return FString();
	}

	return EmpirePointsLine(*Held.Bank, Run != nullptr ? Run->LastEmpirePointsAwarded : 0.0);
}

FString UCataclysmEmpireMapLayout::GoldLine(int64 Held)
{
	return ACataclysmDroppedItem::GoldName(FMath::Max<int64>(0, Held));
}

FString UCataclysmEmpireMapLayout::GoldLineIn(const UObject* WorldContext)
{
	return UCataclysmGameInstance::GoldBalanceText(WorldContext);
}

FString UCataclysmEmpireMapLayout::DaysOwedLine(const UCataclysmEmpireRun* Run)
{
	const int32 Owed = Run != nullptr ? Run->DaysOwed() : 0;
	if (Owed <= 0)
	{
		return FString();
	}

	return Owed == 1
		? FString(TEXT("1 day passes when you leave"))
		: FString::Printf(TEXT("%d days pass when you leave"), Owed);
}

FString UCataclysmEmpireMapLayout::LastHitLine(const UCataclysmEmpireRun* Run)
{
	// NOTHING TO SAY: no run, a run that has not begun, or no hit and no move
	// yet. Day 0 is what `UCataclysmEmpireRun::Begin` writes, and no day of a
	// run is day 0.
	if (Run == nullptr || Run->Map == nullptr
		|| (Run->LastHitDay <= 0 && Run->LastMoveDay <= 0))
	{
		return FString();
	}

	// A FALL'S MOVE OF A QUEST DUNGEON, WHEN IT IS AT LEAST AS RECENT AS THE
	// LAST HIT. Since 2026-10-21 a Quest dungeon on a city that falls moves
	// rather than being absorbed, and without this line a player sees the city
	// lost and a Quest dungeon standing somewhere it was not. The later of the
	// two is shown; ON THE SAME DAY THE MOVE IS SHOWN, because a fall that
	// moved a dungeon is the larger event and the hit that felled the city is
	// usually the same story. The line the console prints for the day names
	// both.
	if (Run->LastMoveDay > 0 && Run->LastMoveDay >= Run->LastHitDay)
	{
		const FCataclysmCity* From = Run->Map->Find(Run->LastMoveFromCityId);
		const FCataclysmCity* To = Run->Map->Find(Run->LastMoveToCityId);
		const FString FromName = From != nullptr
			? From->Name : FString(TEXT("a city"));
		const FString ToName = To != nullptr
			? To->Name : FString(TEXT("a city"));

		// THE CITY THAT FELL IS THE CITY IT MOVED FROM, named twice so the
		// sentence says both where it was and why it left.
		return FString::Printf(
			TEXT("Day %d: a Quest dungeon moved from %s to %s because %s fell."),
			Run->LastMoveDay, *FromName, *ToName, *FromName);
	}

	// THE CITY IS NAMED AS THE MAP NAMES IT. A number nobody can look up says
	// nothing, and a city that cannot be found is still a hit worth a line.
	const FCataclysmCity* HitCity = Run->Map->Find(Run->LastHitCityId);
	const FString HitCityName = HitCity != nullptr
		? HitCity->Name : FString(TEXT("a city"));

	return Run->bLastHitWasAQuestTimer
		? FString::Printf(TEXT("Day %d: a Quest dungeon's timer hit %s."),
						  Run->LastHitDay, *HitCityName)
		: FString::Printf(TEXT("Day %d: a dungeon hit %s and left."),
						  Run->LastHitDay, *HitCityName);
}

FString UCataclysmEmpireMapLayout::LastDeathLine(const UCataclysmEmpireRun* Run)
{
	// NOTHING TO SAY: no run, a run that has not begun, or no death yet. ASKED OF THE DAYS THE DEATH COST AND NOT OF
	// ITS DAY: `Begin` writes 0 to both, and every death costs at least `DeathDayCostStandard`, 5 days, while a death
	// can be on day 0 since issue #2733 -- the first wave lands then, so a dungeon stands to be walked before any day
	// has passed, and a walk that owes no whole day leaves the death on the run's own day. Until #2733 this asked
	// `LastDeathDay <= 0`, because the first wave landed on day 1.
	if (Run == nullptr || Run->Map == nullptr || Run->LastDeathDaysCost <= 0)
	{
		return FString();
	}

	const FString Where = Run->LastDeathDungeonName.IsEmpty()
		? FString(TEXT("a dungeon")) : Run->LastDeathDungeonName;

	// THE WALK'S DAYS, AND THEN THE DEATH'S ON TOP OF THEM. "More" only when the walk ran some, so a walk that owed
	// no whole day does not read as if the death's days were added to something.
	const int32 Walk = Run->LastDeathWalkDays;
	const int32 Cost = Run->LastDeathDaysCost;
	const FString WalkPart = Walk <= 0
		? FString(TEXT("The walk owed no whole day"))
		: FString::Printf(TEXT("The walk's %d day%s ran"), Walk, Walk == 1 ? TEXT("") : TEXT("s"));
	const FString CostPart = FString::Printf(TEXT("the death cost %d %sday%s"), Cost,
											 Walk > 0 ? TEXT("more ") : TEXT(""), Cost == 1 ? TEXT("") : TEXT("s"));

	// EVERY CITY THE DEATH'S RESOLVE FELLED, BY THE MAP'S NAME FOR IT, joined as a sentence joins a list. A city that
	// cannot be found is still a city that fell.
	TArray<FString> Names;
	for (const int32 CityId : Run->LastDeathFallenCityIds)
	{
		const FCataclysmCity* City = Run->Map->Find(CityId);
		Names.Add(City != nullptr ? City->Name : FString(TEXT("a city")));
	}
	FString FellPart;
	if (Names.IsEmpty())
	{
		FellPart = TEXT("no city fell");
	}
	else if (Names.Num() == 1)
	{
		FellPart = FString::Printf(TEXT("%s fell"), *Names[0]);
	}
	else
	{
		const FString Last = Names.Pop();
		FellPart = FString::Printf(TEXT("%s and %s fell"), *FString::Join(Names, TEXT(", ")), *Last);
	}

	return FString::Printf(TEXT("Day %d: you died in %s. %s, %s, and %s."),
						   Run->LastDeathDay, *Where, *WalkPart, *CostPart, *FellPart);
}

FString UCataclysmEmpireMapLayout::LastClearLine(const UCataclysmEmpireRun* Run)
{
	// NOTHING TO SAY: no run, a run that has not begun, or no clear yet, which `INDEX_NONE` marks because a clear can be
	// on day 0. See `UCataclysmEmpireRun::LastClearDay`.
	if (Run == nullptr || Run->Map == nullptr || Run->LastClearDay == INDEX_NONE)
	{
		return FString();
	}

	const FString Where = Run->LastClearDungeonName.IsEmpty()
		? FString(TEXT("a dungeon")) : Run->LastClearDungeonName;
	const int32 Walk = Run->LastClearWalkDays;
	const FString WalkPart = Walk <= 0
		? FString(TEXT("The walk owed no whole day."))
		: FString::Printf(TEXT("The walk's %d day%s ran."), Walk, Walk == 1 ? TEXT("") : TEXT("s"));

	return FString::Printf(TEXT("Day %d: you cleared %s. %s"), Run->LastClearDay, *Where, *WalkPart);
}

namespace
{
	/** A city by the map's name for it, or "a city" when the map cannot find it. */
	FString CityNameIn(const UCataclysmEmpireRun& Run, int32 CityId)
	{
		const FCataclysmCity* City = Run.Map != nullptr ? Run.Map->Find(CityId) : nullptr;
		return City != nullptr ? City->Name : FString(TEXT("a city"));
	}

	/** "A", "A and B", "A, B and C", and past `DaysAwayCitiesNamed` names, "A, B, C and 2 more". */
	FString NamesJoined(const TArray<FString>& Names, int32 More)
	{
		TArray<FString> Shown = Names;
		if (More > 0)
		{
			Shown.Add(FString::Printf(TEXT("%d more"), More));
		}
		if (Shown.Num() <= 1)
		{
			return Shown.IsEmpty() ? FString() : Shown[0];
		}
		const FString Last = Shown.Pop();
		return FString::Printf(TEXT("%s and %s"), *FString::Join(Shown, TEXT(", ")), *Last);
	}
}

FString UCataclysmEmpireMapLayout::DaysAwayLine(const UCataclysmEmpireRun* Run)
{
	if (Run == nullptr || Run->Map == nullptr || !Run->LastDaysAway.HasEnded())
	{
		return FString();
	}
	const FCataclysmDaysAway& Away = Run->LastDaysAway;
	if (Away.ToDay <= Away.FromDay)
	{
		return TEXT("While you were away no day passed.");
	}

	TArray<FString> Parts;

	// THE CITIES HIT, at most `DaysAwayCitiesNamed` by name, in the order each was first hit, and the rest counted.
	TArray<FString> Hit;
	for (int32 Index = 0; Index < Away.Cities.Num() && Index < DaysAwayCitiesNamed; ++Index)
	{
		const FCataclysmCityLosses& City = Away.Cities[Index];
		Hit.Add(FString::Printf(TEXT("%s lost %.0f defence and %.0f people"), *CityNameIn(*Run, City.CityId),
								City.DefenceLost, City.PopulationLost));
	}
	const int32 MoreHit = Away.Cities.Num() - Hit.Num();
	if (Hit.IsEmpty())
	{
		Parts.Add(TEXT("nothing was hit"));
	}
	else
	{
		FString Cities = FString::Join(Hit, TEXT(", "));
		if (MoreHit > 0)
		{
			Cities += FString::Printf(TEXT(", and %d more cit%s %s hit"), MoreHit, MoreHit == 1 ? TEXT("y") : TEXT("ies"),
									  MoreHit == 1 ? TEXT("was") : TEXT("were"));
		}
		Parts.Add(Cities);
	}

	// THE CITIES THAT FELL, named the same way.
	TArray<FString> Fell;
	for (int32 Index = 0; Index < Away.Fallen.Num() && Index < DaysAwayCitiesNamed; ++Index)
	{
		Fell.Add(CityNameIn(*Run, Away.Fallen[Index]));
	}
	if (!Fell.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("%s fell"), *NamesJoined(Fell, Away.Fallen.Num() - Fell.Num())));
	}

	// AND THE SURGES, with the dungeons they landed.
	if (Away.Surges > 0)
	{
		Parts.Add(Away.Surges == 1
			? FString::Printf(TEXT("a surge brought %d dungeon%s"), Away.SurgeDungeons,
							  Away.SurgeDungeons == 1 ? TEXT("") : TEXT("s"))
			: FString::Printf(TEXT("%d surges brought %d dungeon%s"), Away.Surges, Away.SurgeDungeons,
							  Away.SurgeDungeons == 1 ? TEXT("") : TEXT("s")));
	}

	return FString::Printf(TEXT("While you were away, from day %d to day %d: %s."), Away.FromDay, Away.ToDay,
						   *FString::Join(Parts, TEXT("; ")));
}

FString UCataclysmEmpireMapLayout::ReturnLine(const UCataclysmEmpireRun* Run)
{
	const FString Away = DaysAwayLine(Run);
	if (Away.IsEmpty())
	{
		return FString();
	}
	const FString Clear = Run->LastDaysAway.bEndedInAClear ? LastClearLine(Run) : FString();
	return Clear.IsEmpty() ? Away : FString::Printf(TEXT("%s %s"), *Clear, *Away);
}

FString UCataclysmEmpireMapLayout::LatestEventLine(const UCataclysmEmpireRun* Run)
{
	// THE DAY THE BOSS DUNGEON OPENED, or 0 while it has not, so a run without one compares as it did before issue
	// #1324 slice 6c: no death or hit is on a day below 0.
	const int32 OpenedDay = Run != nullptr && Run->bCataclysmDungeonOpened ? Run->CataclysmDungeonOpenedDay : 0;

	// THE DEATH WHILE NOTHING ELSE WITH A LINE IS MORE RECENT; on the same day, the death. See the header.
	//
	// WHETHER THERE WAS A DEATH IS ASKED OF THE DAYS IT COST AND NOT OF ITS DAY, as `LastDeathLine` asks it since
	// issue #2733 slice 1: the opening wave stands on day 0 since then, and a death in a walk that owes no whole day
	// is on the run's own day, so a death can be on day 0. At the starting rate entering a dungeon owes floor 1's day,
	// so that takes a walk made shorter than its floors, by a city upgrade or the empire tree. Until slice 3 this asked
	// `LastDeathDay > 0` and never showed such a death. Every death costs at least `DeathDayCostStandard` days.
	if (Run != nullptr && Run->LastDeathDaysCost > 0
		&& Run->LastDeathDay >= Run->LastHitDay && Run->LastDeathDay >= Run->LastMoveDay
		&& Run->LastDeathDay >= OpenedDay)
	{
		const FString Death = LastDeathLine(Run);
		if (!Death.IsEmpty())
		{
			return Death;
		}
	}

	// THEN THE OPENING WHILE NO HIT OR MOVE IS MORE RECENT; on the same day, the opening, which came after them.
	if (Run != nullptr && Run->bCataclysmDungeonOpened
		&& OpenedDay >= Run->LastHitDay && OpenedDay >= Run->LastMoveDay)
	{
		const FString Opened = CataclysmDungeonOpenedLine(Run);
		if (!Opened.IsEmpty())
		{
			return Opened;
		}
	}
	return LastHitLine(Run);
}

// ---------------------------------------------------------------------------
// The Cataclysm boss dungeon, which stands on no city -- issue #1324 slice 6c
// ---------------------------------------------------------------------------

FVector2D UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(float Scale)
{
	// THE CENTRE, A MARGIN IN FROM THE TOP AND THE LEFT. The margin is not scaled, as `ScaleToFit`'s is not, so the
	// marker and the diamond keep the same clear edge. See the header for why no city can be under it.
	const FVector2D Size = CataclysmDungeonMarkerSize(Scale);
	return FVector2D(FitMarginPx, FitMarginPx) + Size * 0.5;
}

FVector2D UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(float Scale)
{
	return FVector2D(CataclysmDungeonMarkerWidthPx * Scale, CataclysmDungeonMarkerHeightPx * Scale);
}

FVector2D UCataclysmEmpireMapLayout::CataclysmDungeonWalkPosition(float Scale)
{
	// THE MARKER'S LEFT EDGE, AND THE GAP BELOW ITS BOTTOM EDGE. The margin is not scaled, as the marker's is not; the
	// gap is, so the two boxes keep their proportion as the map shrinks. See the header for why no city is under it.
	const FVector2D Marker = CataclysmDungeonMarkerSize(Scale);
	const FVector2D Size = CataclysmDungeonWalkSize(Scale);
	return FVector2D(FitMarginPx + Size.X * 0.5,
					 FitMarginPx + Marker.Y + CataclysmDungeonWalkGapPx * Scale + Size.Y * 0.5);
}

FVector2D UCataclysmEmpireMapLayout::CataclysmDungeonWalkSize(float Scale)
{
	return CitySize(Scale);
}

FName UCataclysmEmpireMapLayout::CataclysmDungeonWalkButtonName(const UCataclysmEmpireRun* Run)
{
	const FCataclysmDungeon* Boss = Run != nullptr && Run->Map != nullptr ? Run->FindCataclysmDungeon() : nullptr;
	return Boss != nullptr ? FCataclysmWalkFromTheScreen::ButtonNameFor(Boss->DungeonId) : FName(NAME_None);
}

FString UCataclysmEmpireMapLayout::CataclysmDungeonMarkerLabel(const UCataclysmEmpireRun* Run)
{
	const FCataclysmDungeon* Boss = Run != nullptr && Run->Map != nullptr ? Run->FindCataclysmDungeon() : nullptr;
	if (Boss == nullptr)
	{
		return FString();
	}

	// THE DEPTH ON THE BOX, because it is the one figure about it that changes: every ordinary dungeon defeated adds
	// a floor. One floor is not a depth a boss rolls, but a dungeon built by hand can have it.
	return FString::Printf(TEXT("Cataclysm boss: %d floor%s"), Boss->Floors, Boss->Floors == 1 ? TEXT("") : TEXT("s"));
}

FString UCataclysmEmpireMapLayout::CataclysmDungeonBrief(const UCataclysmEmpireRun* Run)
{
	const FCataclysmDungeon* Boss = Run != nullptr && Run->Map != nullptr ? Run->FindCataclysmDungeon() : nullptr;
	if (Boss == nullptr)
	{
		return FString();
	}

	// WHO SENT IT AND WHAT KIND IT IS, each left out when it says nothing: `None` has no Cataclysm to name and a
	// dungeon with no sub-type has none to show.
	TArray<FString> Marks;
	if (Boss->Cataclysm != ECataclysmType::None)
	{
		Marks.Add(UCataclysmRoster::NameFor(Boss->Cataclysm).ToString());
	}
	const FString SubType = UCataclysmSurgeScheduler::SubTypeName(Boss->SubType);
	if (!SubType.IsEmpty())
	{
		Marks.Add(SubType);
	}
	const FString Marked = Marks.IsEmpty()
		? FString()
		: FString::Printf(TEXT(" (%s)"), *FString::Join(Marks, TEXT(", ")));

	// THE FLOORS THE ORDINARY DUNGEONS ADDED, which are the dungeon's floors less the ones it rolled. Never below
	// none, whatever a hand-built run holds.
	const int32 Added = FMath::Max(0, Boss->Floors - Run->CataclysmDungeonBaseFloors);
	FString AddedPart;
	if (Added == 0)
	{
		AddedPart = TEXT("as it rolled");
	}
	else if (Added == 1)
	{
		AddedPart = TEXT("1 of them added by an ordinary dungeon defeated");
	}
	else
	{
		AddedPart = FString::Printf(TEXT("%d of them added by ordinary dungeons defeated"), Added);
	}

	// AND WHAT ENTERS IT: the Walk box under this one, since issue #2715. Until then nothing entered a dungeon from this
	// map, and the line named the console command that did.
	return FString::Printf(
		TEXT("The Cataclysm boss dungeon%s: %d floor%s, %s. Every ordinary dungeon defeated adds a floor. It stands on "
			 "no city and never resolves. Walk, under this box, enters it."),
		*Marked, Boss->Floors, Boss->Floors == 1 ? TEXT("") : TEXT("s"), *AddedPart);
}

FString UCataclysmEmpireMapLayout::CataclysmDungeonOpenedLine(const UCataclysmEmpireRun* Run)
{
	if (Run == nullptr || Run->Map == nullptr || !Run->bCataclysmDungeonOpened)
	{
		return FString();
	}

	return FString::Printf(TEXT("Day %d: the Cataclysm boss dungeon opened."), Run->CataclysmDungeonOpenedDay);
}

// ---------------------------------------------------------------------------
// The button that opens the empire upgrade tree -- issue #2741 slice 2
// ---------------------------------------------------------------------------

FVector2D UCataclysmEmpireMapLayout::EmpireTreeButtonPosition(float Scale, FVector2D PanelSize)
{
	// THE CENTRE, A MARGIN IN FROM THE TOP AND THE RIGHT: the boss marker's corner, mirrored. The margin is not
	// scaled, as the marker's is not. See the header for why no city can be under it.
	const FVector2D Size = EmpireTreeButtonSize(Scale);
	return FVector2D(PanelSize.X - FitMarginPx - Size.X * 0.5, FitMarginPx + Size.Y * 0.5);
}

FVector2D UCataclysmEmpireMapLayout::EmpireTreeButtonSize(float Scale)
{
	return FVector2D(EmpireTreeButtonWidthPx * Scale, EmpireTreeButtonHeightPx * Scale);
}

FString UCataclysmEmpireMapLayout::EmpireTreeButtonText()
{
	return TEXT("Empire upgrade tree");
}

FString UCataclysmEmpireMapLayout::EmpireTreeButtonBrief()
{
	return TEXT("The empire upgrade tree: empire upgrade points are spent there. It can be read at any time and "
				"changed in the capital, not during a walk.");
}

FVector2D UCataclysmEmpireMapLayout::ForgeButtonPosition(float Scale, FVector2D PanelSize)
{
	// THE WALK BOX'S ARITHMETIC, MIRRORED: against the right margin, the tree's button's height and the Walk box's gap
	// below the top margin.
	const FVector2D Tree = EmpireTreeButtonSize(Scale);
	const FVector2D Size = ForgeButtonSize(Scale);
	return FVector2D(PanelSize.X - FitMarginPx - Size.X * 0.5,
					 FitMarginPx + Tree.Y + CataclysmDungeonWalkGapPx * Scale + Size.Y * 0.5);
}

FVector2D UCataclysmEmpireMapLayout::ForgeButtonSize(float Scale)
{
	return CitySize(Scale);
}

FString UCataclysmEmpireMapLayout::ForgeButtonText()
{
	return TEXT("Forge");
}

FString UCataclysmEmpireMapLayout::ForgeButtonBrief()
{
	return TEXT("The Cataclysmic Forge: raise a piece of gear a level with two upgrade stones, or combine two stones "
				"into one of the next level. It can be read at any time and used in the capital, not during a walk.");
}

FVector2D UCataclysmEmpireMapLayout::MerchantButtonPosition(float Scale, FVector2D PanelSize)
{
	// THE FORGE'S PLACE, ONE BUTTON AND ONE GAP LOWER, as the forge's is one button and one gap under the tree's.
	const FVector2D Forge = ForgeButtonPosition(Scale, PanelSize);
	const FVector2D Size = MerchantButtonSize(Scale);
	return FVector2D(Forge.X, Forge.Y + ForgeButtonSize(Scale).Y * 0.5 + CataclysmDungeonWalkGapPx * Scale + Size.Y * 0.5);
}

FVector2D UCataclysmEmpireMapLayout::MerchantButtonSize(float Scale)
{
	return ForgeButtonSize(Scale);
}

FString UCataclysmEmpireMapLayout::MerchantButtonText()
{
	return TEXT("Merchant");
}

FString UCataclysmEmpireMapLayout::MerchantButtonBrief()
{
	return TEXT("The merchant: sell a carried piece of gear for gold, by its rarity and upgrade level. It can be read at "
				"any time and used in the capital, not during a walk. A sale costs no day.");
}

FString UCataclysmEmpireMapLayout::LastRunWonLine(const UCataclysmEmpireRun* Run)
{
	// NOTHING TO SAY: no run, a run that has not begun, or no win before it. Day 0 is what `Begin` writes, and no run
	// is won on day 0 in play: a win is the boss dungeon's clear, the win's day adds the whole days its walk owed
	// (`WonDay` in `UCataclysmEmpireRun::ClearDungeon`), and that walk is one day or more
	// (`UCataclysmEmpireRun::DeepenCataclysmDungeon`). Issue #1324 slice 6e. Until issue #2733 this gave the reason as
	// "the first wave lands on day 1", which stopped being true when the first wave moved to day 0.
	if (Run == nullptr || Run->Clock == nullptr || Run->LastRunWonDay <= 0)
	{
		return FString();
	}

	// THE CATACLYSM AS THE DESIGN NAMES IT: "The Void" with its article, the other seven by their names.
	const FString Against = Run->LastRunWonCataclysm == ECataclysmType::Void
		? FString(TEXT("The Void"))
		: UCataclysmRoster::NameFor(Run->LastRunWonCataclysm).ToString();

	// THE POINTS, OR WHY THERE WERE NONE: a boss defeated before pays nothing (ruling R4).
	const FString PointsPart = Run->LastRunWonPassivePoints > 0
		? FString::Printf(TEXT("+%d passive points."), Run->LastRunWonPassivePoints)
		: FString::Printf(TEXT("%s's boss was defeated before, so no passive points."), *Against);

	// WHAT THIS RUN FACES. One more than the won run, except at the top of the ladder, where it is the same eight.
	const int32 Faces = Run->ActiveCataclysms.Num();
	const FString FacesPart = Faces <= Run->LastRunWonTier
		? FString::Printf(TEXT("This run faces the same %d Cataclysms: tier %d is the highest."), Faces,
						  Run->LastRunWonTier)
		: FString::Printf(TEXT("This run faces %d Cataclysm%s."), Faces, Faces == 1 ? TEXT("") : TEXT("s"));

	return FString::Printf(TEXT("Run won on day %d at tier %d against %s. %s %s"), Run->LastRunWonDay,
						   Run->LastRunWonTier, *Against, *PointsPart, *FacesPart);
}

FString UCataclysmEmpireMapLayout::ScoreText(int32 Score)
{
	return ACataclysmDroppedItem::GoldAmountText(FMath::Max(0, Score));
}

FString UCataclysmEmpireMapLayout::DungeonScoreText(int32 DungeonScore, int32 PlayerScore)
{
	return PlayerScore == INDEX_NONE
		? FString::Printf(TEXT("Dungeon Score %s."), *ScoreText(DungeonScore))
		: FString::Printf(TEXT("Dungeon Score %s; your Power Score %s."), *ScoreText(DungeonScore),
						  *ScoreText(PlayerScore));
}

FString UCataclysmEmpireMapLayout::DungeonScoreLine(const UCataclysmEmpireRun* Run, int32 DungeonId, int32 PlayerScore)
{
	const int32 DungeonScore = UCataclysmEnemyScore::DungeonScoreOf(Run, DungeonId);
	return DungeonScore == INDEX_NONE ? FString() : DungeonScoreText(DungeonScore, PlayerScore);
}

FString UCataclysmEmpireMapLayout::DungeonScoreAndRewardsLine(const UCataclysmEmpireRun* Run, int32 DungeonId,
															   int32 PlayerScore)
{
	const FString Line = DungeonScoreLine(Run, DungeonId, PlayerScore);
	if (Line.IsEmpty())
	{
		return Line;
	}
	const FString Rewards = FCataclysmPowerGapRule::RewardsText(FCataclysmPowerGapRule::RewardsFor(
		UCataclysmEnemyScore::DungeonScoreOf(Run, DungeonId), PlayerScore, Run->DifficultyTier));
	return Rewards.IsEmpty() ? Line : Line + TEXT(" ") + Rewards;
}
