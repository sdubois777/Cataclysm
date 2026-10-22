// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCityScreenLayout.h"

#include "Data/CataclysmCityUpgradeMapping.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"

namespace
{
	/** The city, or null when there is no run or no such city. */
	const FCataclysmCity* CityIn(const UCataclysmEmpireRun* Run, int32 CityId)
	{
		if (Run == nullptr || Run->Map == nullptr)
		{
			return nullptr;
		}

		return Run->Map->Find(CityId);
	}
}

FString UCataclysmCityScreenLayout::TitleTextFor(const UCataclysmEmpireRun* Run,
												 int32 CityId)
{
	const FCataclysmCity* City = CityIn(Run, CityId);
	if (City == nullptr)
	{
		// IT SAYS WHY IT IS EMPTY. A screen opened before a run started would
		// otherwise be a blank panel, which reads as broken.
		return TEXT("No city. Cataclysm.EmpireBegin starts a run.");
	}

	return FString::Printf(TEXT("%s"), *City->Name);
}

FString UCataclysmCityScreenLayout::StatusTextFor(const UCataclysmEmpireRun* Run,
												  int32 CityId)
{
	const FCataclysmCity* City = CityIn(Run, CityId);
	if (City == nullptr)
	{
		return FString();
	}

	if (City->bFallen)
	{
		// A FALLEN CITY IS NOT A DESTROYED ONE, and the screen says which,
		// because retaking it is a thing a player can plan for.
		if (City->bErased)
		{
			return TEXT("Erased by the Void. It can never be retaken.");
		}

		FString Fallen =
			TEXT("Fallen. Nothing can be built here until it is retaken.");

		// AND WHAT RETAKING CLEARS, when the Void had begun on it. A city that
		// fell with fewer than ten stacks keeps them until it is retaken, and
		// the retake is what clears them -- worth knowing before choosing which
		// city to win back.
		if (City->VoidStacks > 0)
		{
			Fallen += FString::Printf(
				TEXT(" It holds %d Void stacks; retaking it clears them."),
				City->VoidStacks);
		}

		return Fallen;
	}

	TArray<FString> Parts;

	Parts.Add(FString::Printf(TEXT("%s"),
							  *UCataclysmEmpireMap::TierName(City->Tier)));

	// THE SAME THREE FACTS THE EMPIRE OVERVIEW'S DETAIL LINE GIVES, and worded
	// the same way, so moving from the map to a city does not restate the city
	// in different words.
	Parts.Add(FString::Printf(TEXT("defence %.0f of %.0f (%.0f%%)"),
							  City->Defence, City->MaxDefence,
							  City->DefenceFraction() * 100.0f));

	Parts.Add(FString::Printf(TEXT("%.0f of %.0f people"), City->Population,
							  City->MaxPopulation));

	// THE VOID'S COUNT, AND ONLY WHEN IT HAS BEGUN, so a city the Void has not
	// touched reads exactly as it did. Out of the ten that erase it.
	if (City->VoidStacks > 0)
	{
		Parts.Add(FString::Printf(TEXT("Void stacks %d / %d"), City->VoidStacks,
								  UCataclysmEmpireMap::VoidStacksToErase));
	}

	Parts.Add(Run->Map->IsExposed(CityId) ? TEXT("exposed")
										  : TEXT("sealed behind the frontier"));

	return FString::Join(Parts, TEXT("   "));
}

FString UCataclysmCityScreenLayout::SlotsTextFor(const UCataclysmEmpireRun* Run,
												 int32 CityId)
{
	const FCataclysmCity* City = CityIn(Run, CityId);
	if (City == nullptr)
	{
		return FString();
	}

	return FString::Printf(TEXT("%d of %d upgrade slots filled."),
						   City->Upgrades.Num(), Run->Map->UpgradeSlots);
}

TArray<FString> UCataclysmCityScreenLayout::DungeonLinesFor(
	const UCataclysmEmpireRun* Run, int32 CityId)
{
	TArray<FString> Lines;

	if (CityIn(Run, CityId) == nullptr || Run->Clock == nullptr)
	{
		return Lines;
	}

	// SOONEST TO RESOLVE FIRST. The whole reason to open a city is to decide
	// whether it is about to be bitten and by what, so the most urgent threat
	// must not be buried under three that are weeks away.
	TArray<int32> Standing = Run->DungeonsOn(CityId);

	Standing.Sort([Run](const int32 Left, const int32 Right)
				  {
					  return Run->Clock->DaysUntilResolveFor(Left)
							 < Run->Clock->DaysUntilResolveFor(Right);
				  });

	for (const int32 DungeonId : Standing)
	{
		const FCataclysmDungeon* Dungeon = Run->FindDungeon(DungeonId);
		if (Dungeon == nullptr)
		{
			continue;
		}

		// WHAT IT DOES DIFFERENTLY, FIRST, WHEN IT DOES ANYTHING. A Famine Quest
		// dungeon is always Sacrificial and a Death one always a Horde since
		// 2026-10-21, and a player deciding which dungeon to walk needs to see
		// that before the floors. The words are `SubTypeName`'s, which the run's
		// own description uses, and `None` adds nothing: most dungeons have a
		// sub-type, and one without reads as it always did.
		const FString SubType =
			UCataclysmSurgeScheduler::SubTypeName(Dungeon->SubType);
		const FString Marked =
			SubType.IsEmpty() ? FString() : SubType + TEXT(", ");

		// A HOLLOW SAYS WHETHER IT IS STILL SHRINKING AND TO WHAT, since
		// 2026-10-22. It loses a floor a day down to half its natural depth,
		// and a player choosing which dungeon to walk wants to know how deep
		// it will be when they get there. The figure is the floors at which it
		// stops, `Floors` less what it has left to lose. Every other dungeon's
		// line is as it was.
		if (Dungeon->IsHollow())
		{
			const int32 LeftToLose = Dungeon->HollowFloorsLeftToLose();
			const FString Shrinking = LeftToLose > 0
				? FString::Printf(TEXT("shrinking by one a day to %d"),
								  Dungeon->Floors - LeftToLose)
				: FString(TEXT("no longer shrinking"));

			Lines.Add(FString::Printf(TEXT("%s%d floors, %s, %.0f days until it bites"),
									  *Marked, Dungeon->Floors, *Shrinking,
									  Run->Clock->DaysUntilResolveFor(DungeonId)));
			continue;
		}

		Lines.Add(FString::Printf(TEXT("%s%d floors, %.0f days until it bites"),
								  *Marked, Dungeon->Floors,
								  Run->Clock->DaysUntilResolveFor(DungeonId)));
	}

	return Lines;
}

TArray<FString> UCataclysmCityScreenLayout::HeldLinesFor(
	const UCataclysmEmpireRun* Run, int32 CityId)
{
	TArray<FString> Lines;

	const FCataclysmCity* City = CityIn(Run, CityId);
	if (City == nullptr)
	{
		return Lines;
	}

	for (const FCataclysmCityUpgrade& Held : City->Upgrades)
	{
		// THE SENTENCE THE DESIGNER WROTE, read out of the table rather than
		// rebuilt from the effect and its number. A screen that described an
		// upgrade in its own words would be a second description of it.
		const FString Effect =
			UCataclysmCityUpgradeMapping::EffectTextFor(Held.RowName);

		Lines.Add(Effect.IsEmpty() ? Held.RowName.ToString() : Effect);
	}

	return Lines;
}

TArray<FCataclysmCityUpgradeOffer> UCataclysmCityScreenLayout::OffersFor(
	const UCataclysmEmpireRun* Run, int32 CityId)
{
	TArray<FCataclysmCityUpgradeOffer> Offers;

	const FCataclysmCity* City = CityIn(Run, CityId);
	if (City == nullptr)
	{
		return Offers;
	}

	for (const FName RowName : UCataclysmCityUpgradeMapping::AllRowNames())
	{
		const FCataclysmCityUpgrade Upgrade =
			UCataclysmCityUpgradeMapping::MakeFromTable(RowName);

		FCataclysmCityUpgradeOffer Offer;
		Offer.RowName = RowName;
		Offer.Branch = UCataclysmCityUpgradeMapping::BranchFor(RowName);
		Offer.Effect = UCataclysmCityUpgradeMapping::EffectTextFor(RowName);
		Offer.EffectKind = Upgrade.Effect;
		Offer.bHeld = City->HasUpgrade(RowName);

		// THE RUN ANSWERS, NOT THIS. Asking `WouldBuyCityUpgrade` is what stops
		// the screen and the purchase drifting apart: every rule about slots,
		// duplicates, fallen cities and unbuilt effects lives there.
		const ECataclysmCityUpgradeResult Result =
			Run->WouldBuyCityUpgrade(CityId, Upgrade);

		Offer.bCanBuy = Result == ECataclysmCityUpgradeResult::Bought;
		Offer.Refusal = Offer.bCanBuy
							? FString()
							: UCataclysmCityUpgradeRules::ResultText(Result);

		Offers.Add(Offer);
	}

	return Offers;
}

FString UCataclysmCityScreenLayout::BuyableHeading()
{
	return TEXT("What this city can build");
}

FString UCataclysmCityScreenLayout::NotBuiltHeading(int32 Count)
{
	// THE COUNT AND THE REASON, because a section of greyed rows with no
	// explanation reads as a broken screen rather than as unfinished work.
	return FString::Printf(
		TEXT("%d more are designed and do nothing yet"), Count);
}

FString UCataclysmCityScreenLayout::HeldHeading()
{
	return TEXT("What this city has");
}

FString UCataclysmCityScreenLayout::DungeonHeading(int32 Count)
{
	if (Count == 0)
	{
		return TEXT("Nothing is standing on this city");
	}

	return Count == 1 ? TEXT("1 dungeon is standing on this city")
					  : FString::Printf(
							TEXT("%d dungeons are standing on this city"),
							Count);
}

FString UCataclysmCityScreenLayout::ButtonTextFor(
	const FCataclysmCityUpgradeOffer& Offer)
{
	return Offer.Effect.IsEmpty() ? Offer.RowName.ToString() : Offer.Effect;
}

FString UCataclysmCityScreenLayout::NotBuiltLineFor(
	const FCataclysmCityUpgradeOffer& Offer)
{
	const FString Text = ButtonTextFor(Offer);

	if (Offer.Refusal.IsEmpty())
	{
		return Text;
	}

	return FString::Printf(TEXT("%s  --  %s"), *Text, *Offer.Refusal);
}
