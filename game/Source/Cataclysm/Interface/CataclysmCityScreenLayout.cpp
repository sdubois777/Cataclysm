// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmCityScreenLayout.h"

#include "Data/CataclysmCityUpgradeMapping.h"
#include "DayClock/CataclysmDayClock.h"
#include "Empire/CataclysmDungeonModifier.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Empire/CataclysmSurge.h"
#include "Items/CataclysmDroppedItem.h"
#include "Player/CataclysmCityUpgradePurchase.h"
#include "Player/CataclysmGameInstance.h"

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
	//
	// WITH THE EMPIRE UPGRADE TREE: what the city is read as holding, of its maximum with the tree. Issue #2741 slice 3a.
	const UCataclysmEmpireMap* Map = Run->Map;
	Parts.Add(FString::Printf(TEXT("defence %.0f of %.0f (%.0f%%)"),
							  Map->DefenceOf(*City), Map->EffectiveMaxDefence(*City),
							  Map->DefenceFractionOf(*City) * 100.0f));

	Parts.Add(FString::Printf(TEXT("%.0f of %.0f people"), Map->PopulationOf(*City),
							  Map->EffectiveMaxPopulation(*City)));

	// THE VOID'S COUNT, AND ONLY WHEN IT HAS BEGUN, so a city the Void has not
	// touched reads exactly as it did. Out of the ten that erase it.
	if (City->VoidStacks > 0)
	{
		Parts.Add(FString::Printf(TEXT("Void stacks %d / %d"), City->VoidStacks,
								  UCataclysmEmpireMap::VoidStacksToErase));
	}

	// AND THE EDICT AN ASCENDANT LEFT ON IT, AND ONLY WHILE ITS DAYS RUN, since
	// 2026-10-22. Every dungeon on the city carries it, so a player choosing
	// where to walk needs to know.
	const FName Left = City->EdictCarriedOn(Run->Day());
	if (!Left.IsNone())
	{
		const int32 DaysLeft = City->EdictDaysLeftOn(Run->Day());
		Parts.Add(FString::Printf(TEXT("%s, %d %s left"),
								  *UCataclysmDungeonModifierRules::EdictName(Left),
								  DaysLeft, DaysLeft == 1 ? TEXT("day") : TEXT("days")));
	}

	// AND ECONOMIC BAILOUT, ONCE IT HAS SAVED THE CITY, which it does once a run (issue #2741 slice 9): the player is told
	// the city will not be saved again.
	if (City->bBailedOut)
	{
		Parts.Add(TEXT("saved once by Economic Bailout"));
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

	// AN UPGRADE BEING BUILT HAS TAKEN ITS SLOT, since issue #2825.
	return FString::Printf(TEXT("%d of %d upgrade slots filled."),
						   City->Upgrades.Num() + City->Building.Num(), Run->Map->UpgradeSlots);
}

FString UCataclysmCityScreenLayout::SlotsAndGoldTextFor(const UObject* WorldContext,
													   const UCataclysmEmpireRun* Run,
													   int32 CityId)
{
	const FString Slots = SlotsTextFor(Run, CityId);
	const FString Gold = UCataclysmGameInstance::GoldBalanceText(WorldContext);
	if (Slots.IsEmpty() || Gold.IsEmpty())
	{
		return Slots;
	}
	return FString::Printf(TEXT("%s   %s"), *Slots, *Gold);
}

TArray<int32> UCataclysmCityScreenLayout::DungeonIdsFor(
	const UCataclysmEmpireRun* Run, int32 CityId)
{
	TArray<int32> Ids;

	if (CityIn(Run, CityId) == nullptr || Run->Clock == nullptr)
	{
		return Ids;
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

	// ONLY WHAT THE RUN CAN FIND, so every identifier here has a line and the
	// line and the Walk button under it stand for the same dungeon.
	for (const int32 DungeonId : Standing)
	{
		if (Run->FindDungeon(DungeonId) != nullptr)
		{
			Ids.Add(DungeonId);
		}
	}

	return Ids;
}

TArray<FString> UCataclysmCityScreenLayout::DungeonLinesFor(
	const UCataclysmEmpireRun* Run, int32 CityId)
{
	TArray<FString> Lines;

	// THE ORDER IS `DungeonIdsFor`'s, soonest to resolve first, and it is that
	// list rather than a second sort so that the Walk button under each line
	// walks the dungeon the line describes. Issue #2715.
	for (const int32 DungeonId : DungeonIdsFor(Run, CityId))
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
		//
		// AND A QUEST DUNGEON IS NAMED ONE, AFTER ITS SUB-TYPE, since issue #2800,
		// ruled 2026-11-21: "Elite Quest, 22 floors, 7 days until it bites". No
		// other kind is named, so a Basic dungeon's line is as it was.
		TArray<FString> Words;
		const FString SubType =
			UCataclysmSurgeScheduler::SubTypeName(Dungeon->SubType);
		if (!SubType.IsEmpty())
		{
			Words.Add(SubType);
		}
		if (Dungeon->Type == ECataclysmDungeonType::Quest)
		{
			Words.Add(UCataclysmSurgeScheduler::KindName(Dungeon->Type));
		}
		// A SCOUT PARTIES BONUS DUNGEON SAYS SO, and that it threatens nothing (issue #2741 slice 9, V16).
		if (Dungeon->bBonus)
		{
			Words.Add(TEXT("bonus (threatens nothing)"));
		}
		const FString Marked =
			Words.IsEmpty() ? FString() : FString::Join(Words, TEXT(" ")) + TEXT(", ");

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

		// AN ASCENDANT SAYS WHAT IT HOLDS, since 2026-10-22: how many Edicts it
		// adds by its last floor, and its Heavenly Core. Every other dungeon's
		// line is as it was.
		if (Dungeon->IsAscendant())
		{
			const int32 Edicts = UCataclysmDungeonModifierRules::EdictsOnFloor(
				Dungeon->WalkSeed(Run->WalkSeedMask), Dungeon->Modifiers, Dungeon->Floors).Num();
			const FString Held = Edicts == 1
				? FString(TEXT("1 Edict"))
				: FString::Printf(TEXT("%d Edicts"), Edicts);

			Lines.Add(FString::Printf(
				TEXT("%s%d floors, %s by its last floor and a Heavenly Core, %.0f days until it bites"),
				*Marked, Dungeon->Floors, *Held,
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

	// AND THE ONE IT IS BUILDING, LAST, since issue #2825: its slot is taken, so it belongs with what the city has,
	// and the line says it does nothing yet.
	const FString Building = BuildLineFor(Run, CityId);
	if (!Building.IsEmpty())
	{
		Lines.Add(Building);
	}

	return Lines;
}

FString UCataclysmCityScreenLayout::BuildLineFor(const UCataclysmEmpireRun* Run, int32 CityId)
{
	const FCataclysmCityUpgrade* Build = Run != nullptr ? Run->BuildOn(CityId) : nullptr;
	const FCataclysmCity* City = CityIn(Run, CityId);
	if (Build == nullptr || City == nullptr)
	{
		return FString();
	}

	const FString Effect = UCataclysmCityUpgradeMapping::EffectTextFor(Build->RowName);
	const FString Name = Effect.IsEmpty() ? Build->RowName.ToString() : Effect;
	const FString Left = Build->BuildDaysLeft == 1 ? FString(TEXT("1 day left"))
												   : FString::Printf(TEXT("%d days left"), Build->BuildDaysLeft);

	// PAUSED, AND WHY: a fallen city builds nothing and neither does a besieged one (ruled 2026-11-21).
	if (City->bFallen)
	{
		return FString::Printf(TEXT("%s  --  being built, %s, paused while this city is fallen"), *Name, *Left);
	}
	if (Run->IsBesieged(CityId))
	{
		return FString::Printf(TEXT("%s  --  being built, %s, paused while a Siege dungeon stands on this city"),
							   *Name, *Left);
	}
	return FString::Printf(TEXT("%s  --  being built, %s, ready on day %d"), *Name, *Left, Run->BuildReadyDay(CityId));
}

TArray<FCataclysmCityUpgradeOffer> UCataclysmCityScreenLayout::OffersFor(
	const UObject* WorldContext, const UCataclysmEmpireRun* Run, int32 CityId,
	int64 GoldHeld)
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
		// duplicates, fallen cities, unbuilt effects and, since issue #2742's
		// second slice, the price against the gold held lives there. Asked with
		// the price `UCataclysmCityUpgradePurchase::Buy` charges, on this city
		// and with the empire upgrade tree's price nodes (#2741 slice 6a).
		Offer.Price = UCataclysmCityUpgradePurchase::PriceIn(
			WorldContext, Run, RowName, CityId);

		// AND THE CREDIT THE TREE GIVES, counted as gold held as `Buy` counts
		// it (#2741 slice 6e, D2), so an offer the credit covers is buyable.
		const int64 Credit = GoldHeld < Offer.Price
			? UCataclysmCityUpgradePurchase::CreditIn(WorldContext, Offer.Price)
			: 0;
		const ECataclysmCityUpgradeResult Result = Run->WouldBuyCityUpgrade(
			CityId, Upgrade, Offer.Price, GoldHeld + Credit);

		Offer.bCanBuy = Result == ECataclysmCityUpgradeResult::Bought;
		Offer.bShortOfGold = Result == ECataclysmCityUpgradeResult::NotEnoughGold;
		Offer.bBesieged = Result == ECataclysmCityUpgradeResult::CityIsBesieged;
		// AND THE DAYS, AND WHETHER ANOTHER BUILD IS WHAT STOPS IT, since issue #2825.
		Offer.BuildDays = UCataclysmCityUpgradePurchase::BuildDaysIn(WorldContext, Run, RowName, CityId);
		Offer.bWaitsOnABuild = Result == ECataclysmCityUpgradeResult::BuildUnderWay;
		Offer.OwedIfBought = Offer.bCanBuy && Offer.Price > GoldHeld
			? Offer.Price - GoldHeld
			: 0;
		Offer.Refusal = UCataclysmCityUpgradePurchase::RefusalTextIn(
			WorldContext, Result, Offer.Price, GoldHeld);

		// AND A RUSH, WITH RAPID FUNDING HELD (issue #2832): asked of the run as the purchase asks it, at the rush's
		// price and with the credit the tree gives on that price (slice 6e, D2), so the button and `Buy` cannot
		// disagree.
		Offer.bRushOffered = UCataclysmCityUpgradePurchase::CanRushIn(WorldContext);
		if (Offer.bRushOffered)
		{
			Offer.RushPrice = UCataclysmCityUpgradePurchase::RushPriceIn(WorldContext, Run, RowName, CityId);
			Offer.RushDays = UCataclysmCityUpgradePurchase::BuildDaysIn(WorldContext, Run, RowName, CityId,
																		/*bRush=*/true);
			const int64 RushCredit = GoldHeld < Offer.RushPrice
				? UCataclysmCityUpgradePurchase::CreditIn(WorldContext, Offer.RushPrice)
				: 0;
			const ECataclysmCityUpgradeResult Rush =
				Run->WouldBuyCityUpgrade(CityId, Upgrade, Offer.RushPrice, GoldHeld + RushCredit);
			Offer.bCanRush = Rush == ECataclysmCityUpgradeResult::Bought;
			Offer.bRushShortOfGold = Rush == ECataclysmCityUpgradeResult::NotEnoughGold;
			Offer.RushOwedIfBought = Offer.bCanRush && Offer.RushPrice > GoldHeld ? Offer.RushPrice - GoldHeld : 0;
			Offer.RushRefusal =
				UCataclysmCityUpgradePurchase::RefusalTextIn(WorldContext, Rush, Offer.RushPrice, GoldHeld);
		}

		// AND RAISING IT, ON AN UPGRADE THE CITY HOLDS (issue #1265): asked of the run as
		// `UCataclysmCityUpgradePurchase::Raise` asks it, at the next tier's price and days and with the tree's credit.
		if (Offer.bHeld)
		{
			AddTheRaise(WorldContext, Run, CityId, GoldHeld, Offer);
		}

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
	const FString Text =
		Offer.Effect.IsEmpty() ? Offer.RowName.ToString() : Offer.Effect;

	// THE PRICE ON THE BUTTON, since issue #2742's second slice, and when gold,
	// a Siege or another build is what stops it, why instead.
	if (Offer.bShortOfGold || Offer.bBesieged || Offer.bWaitsOnABuild)
	{
		return FString::Printf(TEXT("%s  --  %s"), *Text, *Offer.Refusal);
	}
	// AND THE DAYS IT TAKES TO BUILD, since issue #2825: "10,000 gold, 6 days to build".
	const FString Days = Offer.BuildDays > 0
		? UCataclysmCityUpgradePurchase::BuildDaysText(Offer.BuildDays)
		: FString();

	// BOUGHT ON CREDIT, and what it would leave owed (#2741 slice 6e, D6), then the days to build (issue #2825):
	// "10,000 gold on credit, 1,000 owed, 6 days to build".
	if (Offer.OwedIfBought > 0)
	{
		const FString OnCredit = FString::Printf(
			TEXT("%s  --  %s on credit, %s owed"), *Text,
			*UCataclysmCityUpgradePurchase::PriceText(Offer.Price),
			*ACataclysmDroppedItem::GoldAmountText(Offer.OwedIfBought));
		return Days.IsEmpty() ? OnCredit : FString::Printf(TEXT("%s, %s"), *OnCredit, *Days);
	}
	if (Offer.Price > 0)
	{
		const FString Price = UCataclysmCityUpgradePurchase::PriceText(Offer.Price);
		return Days.IsEmpty()
			? FString::Printf(TEXT("%s  --  %s"), *Text, *Price)
			: FString::Printf(TEXT("%s  --  %s, %s"), *Text, *Price, *Days);
	}
	return Days.IsEmpty() ? Text : FString::Printf(TEXT("%s  --  %s"), *Text, *Days);
}

FString UCataclysmCityScreenLayout::RushButtonTextFor(
	const FCataclysmCityUpgradeOffer& Offer)
{
	if (Offer.bRushShortOfGold)
	{
		return FString::Printf(TEXT("Rush  --  %s"), *Offer.RushRefusal);
	}
	// ON CREDIT, as the offer's own button says it (slice 6e, D6): "Rush  --  15,000 gold on credit, 1,000 owed, 4 days
	// to build".
	if (Offer.RushOwedIfBought > 0)
	{
		return FString::Printf(TEXT("Rush  --  %s on credit, %s owed, %s"),
							   *UCataclysmCityUpgradePurchase::PriceText(Offer.RushPrice),
							   *ACataclysmDroppedItem::GoldAmountText(Offer.RushOwedIfBought),
							   *UCataclysmCityUpgradePurchase::BuildDaysText(Offer.RushDays));
	}
	return FString::Printf(TEXT("Rush  --  %s, %s"),
						   *UCataclysmCityUpgradePurchase::PriceText(Offer.RushPrice),
						   *UCataclysmCityUpgradePurchase::BuildDaysText(Offer.RushDays));
}

void UCataclysmCityScreenLayout::AddTheRaise(const UObject* WorldContext, const UCataclysmEmpireRun* Run, int32 CityId,
											  int64 GoldHeld, FCataclysmCityUpgradeOffer& Offer)
{
	Offer.bRaiseOffered = true;
	Offer.RaiseTier = UCataclysmCityUpgradePurchase::RaiseTierIn(Run, CityId, Offer.RowName);
	const FCataclysmCityUpgrade Raised = Offer.RaiseTier >= 1 && Offer.RaiseTier <= UCataclysmCityUpgradeRules::HighestTier
		? UCataclysmCityUpgradeMapping::MakeAtTier(Offer.RowName, Offer.RaiseTier)
		: FCataclysmCityUpgrade();

	// AT THE TOP, OR NO LADDER: greyed, saying so, as `Raise` refuses.
	if (Offer.RaiseTier > UCataclysmCityUpgradeRules::HighestTier || !Raised.IsValid())
	{
		Offer.RaiseRefusal = UCataclysmCityUpgradeRules::ResultText(
			Offer.RaiseTier > UCataclysmCityUpgradeRules::HighestTier ? ECataclysmCityUpgradeResult::AtTheTopTier
																	   : ECataclysmCityUpgradeResult::NoTierToRaiseTo);
		return;
	}

	Offer.RaiseWhat = RaiseWhatFor(Raised.Effect, Raised.Value, Raised.IntervalDays);
	Offer.RaisePrice = UCataclysmCityUpgradePurchase::PriceIn(WorldContext, Run, Offer.RowName, CityId, Offer.RaiseTier);
	Offer.RaiseDays = UCataclysmCityUpgradePurchase::BuildDaysIn(WorldContext, Run, Offer.RowName, CityId,
																 /*bRush=*/false, Offer.RaiseTier);
	const int64 Credit = GoldHeld < Offer.RaisePrice ? UCataclysmCityUpgradePurchase::CreditIn(WorldContext, Offer.RaisePrice)
													 : 0;
	const ECataclysmCityUpgradeResult Result =
		Run->WouldRaiseCityUpgrade(CityId, Raised, Offer.RaisePrice, GoldHeld + Credit);
	Offer.bCanRaise = Result == ECataclysmCityUpgradeResult::Bought;
	Offer.bRaiseShortOfGold = Result == ECataclysmCityUpgradeResult::NotEnoughGold;
	Offer.RaiseOwedIfBought = Offer.bCanRaise && Offer.RaisePrice > GoldHeld ? Offer.RaisePrice - GoldHeld : 0;
	Offer.RaiseRefusal = UCataclysmCityUpgradePurchase::RefusalTextIn(WorldContext, Result, Offer.RaisePrice, GoldHeld);

	if (Offer.bRushOffered)
	{
		Offer.RaiseRushPrice =
			UCataclysmCityUpgradePurchase::RushPriceIn(WorldContext, Run, Offer.RowName, CityId, Offer.RaiseTier);
		Offer.RaiseRushDays = UCataclysmCityUpgradePurchase::BuildDaysIn(WorldContext, Run, Offer.RowName, CityId,
																		 /*bRush=*/true, Offer.RaiseTier);
		const int64 RushCredit = GoldHeld < Offer.RaiseRushPrice
			? UCataclysmCityUpgradePurchase::CreditIn(WorldContext, Offer.RaiseRushPrice)
			: 0;
		const ECataclysmCityUpgradeResult Rush =
			Run->WouldRaiseCityUpgrade(CityId, Raised, Offer.RaiseRushPrice, GoldHeld + RushCredit);
		Offer.bCanRaiseRush = Rush == ECataclysmCityUpgradeResult::Bought;
		Offer.bRaiseRushShortOfGold = Rush == ECataclysmCityUpgradeResult::NotEnoughGold;
		Offer.RaiseRushOwedIfBought =
			Offer.bCanRaiseRush && Offer.RaiseRushPrice > GoldHeld ? Offer.RaiseRushPrice - GoldHeld : 0;
		Offer.RaiseRushRefusal =
			UCataclysmCityUpgradePurchase::RefusalTextIn(WorldContext, Rush, Offer.RaiseRushPrice, GoldHeld);
	}
}

FString UCataclysmCityScreenLayout::RaiseButtonTextFor(const FCataclysmCityUpgradeOffer& Offer)
{
	if (Offer.RaiseWhat.IsEmpty() || (!Offer.bCanRaise && !Offer.bRaiseShortOfGold))
	{
		return FString::Printf(TEXT("Raise  --  %s"), *Offer.RaiseRefusal);
	}
	const FString What = FString::Printf(TEXT("Raise to tier %d: %s"), Offer.RaiseTier, *Offer.RaiseWhat);
	if (Offer.bRaiseShortOfGold)
	{
		return FString::Printf(TEXT("%s  --  %s"), *What, *Offer.RaiseRefusal);
	}
	const FString Days = UCataclysmCityUpgradePurchase::BuildDaysText(Offer.RaiseDays);
	if (Offer.RaiseOwedIfBought > 0)
	{
		return FString::Printf(TEXT("%s  --  %s on credit, %s owed, %s"), *What,
							   *UCataclysmCityUpgradePurchase::PriceText(Offer.RaisePrice),
							   *ACataclysmDroppedItem::GoldAmountText(Offer.RaiseOwedIfBought), *Days);
	}
	return FString::Printf(TEXT("%s  --  %s, %s"), *What, *UCataclysmCityUpgradePurchase::PriceText(Offer.RaisePrice),
						   *Days);
}

FString UCataclysmCityScreenLayout::RaiseRushButtonTextFor(const FCataclysmCityUpgradeOffer& Offer)
{
	if (Offer.bRaiseRushShortOfGold || !Offer.bCanRaiseRush)
	{
		return FString::Printf(TEXT("Rush the raise  --  %s"), *Offer.RaiseRushRefusal);
	}
	if (Offer.RaiseRushOwedIfBought > 0)
	{
		return FString::Printf(TEXT("Rush the raise  --  %s on credit, %s owed, %s"),
							   *UCataclysmCityUpgradePurchase::PriceText(Offer.RaiseRushPrice),
							   *ACataclysmDroppedItem::GoldAmountText(Offer.RaiseRushOwedIfBought),
							   *UCataclysmCityUpgradePurchase::BuildDaysText(Offer.RaiseRushDays));
	}
	return FString::Printf(TEXT("Rush the raise  --  %s, %s"),
						   *UCataclysmCityUpgradePurchase::PriceText(Offer.RaiseRushPrice),
						   *UCataclysmCityUpgradePurchase::BuildDaysText(Offer.RaiseRushDays));
}

FString UCataclysmCityScreenLayout::RaiseWhatFor(ECataclysmCityUpgradeEffect Effect, float Value, float IntervalDays)
{
	// PERCENTAGES ARE FRACTIONS IN THE TABLE, 0.5 for 50%; FLAT FIGURES ARE COUNTS; THE ONE MULTIPLIER IS A FACTOR.
	const int32 Percent = FMath::RoundToInt(Value * 100.0f);
	const int32 Count = FMath::RoundToInt(Value);
	const int32 Days = FMath::RoundToInt(IntervalDays);
	switch (Effect)
	{
	case ECataclysmCityUpgradeEffect::MaxDefence:
		return FString::Printf(TEXT("raises max defence by %d%%"), Percent);
	case ECataclysmCityUpgradeEffect::MaxPopulation:
		return FString::Printf(TEXT("raises max population by %d%%"), Percent);
	case ECataclysmCityUpgradeEffect::RemoveDungeons:
		return FString::Printf(TEXT("removes %d%% of this city's dungeons when built"), Percent);
	case ECataclysmCityUpgradeEffect::RestoreDefence:
		return FString::Printf(TEXT("restores %d%% of this city's defence when built"), Percent);
	case ECataclysmCityUpgradeEffect::RestorePopulation:
		return FString::Printf(TEXT("restores %d%% of this city's population when built"), Percent);
	case ECataclysmCityUpgradeEffect::ResistDefenceLoss:
		return FString::Printf(TEXT("this city resists %d%% of damage"), Percent);
	case ECataclysmCityUpgradeEffect::ResistPopulationLoss:
		return FString::Printf(TEXT("this city resists %d%% of population loss"), Percent);
	case ECataclysmCityUpgradeEffect::HealDefenceEvery:
		return FString::Printf(TEXT("every %d days this city's defences heal %d%%"), Days, Percent);
	case ECataclysmCityUpgradeEffect::RecoverPopulationEvery:
		return FString::Printf(TEXT("every %d days this city's population recovers %d%%"), Days, Percent);
	case ECataclysmCityUpgradeEffect::RestoreDefenceOnClear:
		return FString::Printf(TEXT("a dungeon cleared here restores %d%% of this city's defence"), Percent);
	case ECataclysmCityUpgradeEffect::DungeonCap:
		return FString::Printf(TEXT("no more than %d dungeons on this city"), Count);
	case ECataclysmCityUpgradeEffect::DungeonWalkDaysFewer:
		return FString::Printf(TEXT("dungeons here take %d fewer days to beat, to a minimum of 1"), Count);
	case ECataclysmCityUpgradeEffect::DungeonFloorsMore:
		return FString::Printf(TEXT("dungeons here have %d more floors"), Count);
	case ECataclysmCityUpgradeEffect::DungeonFloorsFewer:
		return FString::Printf(TEXT("dungeons here have %d fewer floors, to a minimum of 1"), Count);
	case ECataclysmCityUpgradeEffect::DungeonExperience:
		return FString::Printf(TEXT("dungeons here give %dx the experience"), Count);
	case ECataclysmCityUpgradeEffect::DungeonMagicFind:
		return FString::Printf(TEXT("dungeons here have %d%% increased magic find"), Percent);
	case ECataclysmCityUpgradeEffect::DungeonGold:
		return FString::Printf(TEXT("dungeons here drop %d%% more gold"), Percent);
	case ECataclysmCityUpgradeEffect::DungeonLoot:
		return FString::Printf(TEXT("dungeons here drop %d%% more loot"), Percent);
	case ECataclysmCityUpgradeEffect::DungeonCraftingMaterials:
		return FString::Printf(TEXT("dungeons here drop %d%% more crafting materials"), Percent);
	default:
		return FString();
	}
}

FString UCataclysmCityScreenLayout::NotBuiltLineFor(
	const FCataclysmCityUpgradeOffer& Offer)
{
	// THE SENTENCE ALONE, without a price: nothing in this section can be
	// bought, so a price beside it would read as an offer.
	const FString Text =
		Offer.Effect.IsEmpty() ? Offer.RowName.ToString() : Offer.Effect;

	if (Offer.Refusal.IsEmpty())
	{
		return Text;
	}

	return FString::Printf(TEXT("%s  --  %s"), *Text, *Offer.Refusal);
}
