// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmWalkFromTheScreen.h"

#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Player/CataclysmCapitalGameMode.h"

namespace
{
	/** What every Walk button's name begins with. */
	const TCHAR* WalkButtonPrefix()
	{
		return TEXT("WalkDungeon");
	}
}

FString FCataclysmWalkFromTheScreen::ButtonText()
{
	return TEXT("Walk");
}

FName FCataclysmWalkFromTheScreen::ButtonNameFor(int32 DungeonId)
{
	return FName(*FString::Printf(TEXT("%s%d"), WalkButtonPrefix(), DungeonId));
}

int32 FCataclysmWalkFromTheScreen::DungeonForButton(FName Value)
{
	const FString Name = Value.ToString();
	const FString Prefix = WalkButtonPrefix();
	if (!Name.StartsWith(Prefix, ESearchCase::CaseSensitive))
	{
		return INDEX_NONE;
	}

	// DIGITS AND NOTHING ELSE AFTER THE PREFIX. A dungeon identifier is never negative, and a name with nothing after
	// the prefix stands for no dungeon rather than for dungeon 0.
	const FString Digits = Name.RightChop(Prefix.Len());
	if (Digits.IsEmpty())
	{
		return INDEX_NONE;
	}
	for (const TCHAR Character : Digits)
	{
		if (!FChar::IsDigit(Character))
		{
			return INDEX_NONE;
		}
	}
	return FCString::Atoi(*Digits);
}

FString FCataclysmWalkFromTheScreen::NotADungeonLevelText()
{
	return TEXT("This level is not a dungeon, so there is no floor to walk. Open L_Dungeon and press Play.");
}

FString FCataclysmWalkFromTheScreen::NoLeavingEarlyText(int32 WalkingDungeonId)
{
	return FString::Printf(TEXT("You are walking dungeon %d. There is no leaving a dungeon early: clear its last floor, or "
								"die in it."), WalkingDungeonId);
}

FString FCataclysmWalkFromTheScreen::RefusalFor(const UCataclysmEmpireRun* Run, const ACataclysmDungeonGameMode* Mode,
												int32 DungeonId, const ACataclysmCapitalGameMode* Capital)
{
	if (Run == nullptr)
	{
		return TEXT("No run has been started.");
	}

	// ONLY A DUNGEON LEVEL CAN ENTER A DUNGEON, AND ONLY THE CAPITAL CAN TRAVEL TO ONE (issue #2733 slice 3). Anywhere
	// else the screen says so rather than doing nothing.
	if (Mode == nullptr && Capital == nullptr)
	{
		return NotADungeonLevelText();
	}

	// NO LEAVING A WALK EARLY, WHICHEVER DUNGEON WAS PRESSED. The design: "There is no town portal and no way out except
	// clearing the dungeon or dying." Issue #2733 slice 3, ruled 2026-11-17; until then only the dungeon being walked
	// was refused here, and a press under another left the walk.
	if (Mode != nullptr && Mode->EmpireDungeonId != INDEX_NONE)
	{
		return NoLeavingEarlyText(Mode->EmpireDungeonId);
	}

	if (Run->FindDungeon(DungeonId) == nullptr)
	{
		return FString::Printf(TEXT("No dungeon %d is standing on the map."), DungeonId);
	}

	return FString();
}

FString FCataclysmWalkFromTheScreen::Walk(const UCataclysmEmpireRun* Run, ACataclysmDungeonGameMode* Mode,
										  int32 DungeonId, ACataclysmCapitalGameMode* Capital)
{
	const FString Refusal = RefusalFor(Run, Mode, DungeonId, Capital);
	if (!Refusal.IsEmpty())
	{
		return Refusal;
	}

	// IN THE CAPITAL, THE TRIP. The dungeon level enters the dungeon once the character arrives.
	if (Mode == nullptr)
	{
		return Capital->TravelToWalk(DungeonId);
	}

	// THE GAME MODE'S OWN REFUSAL IS SHOWN, NOT SWALLOWED. It answers only true or false, so the words are the
	// console's for the same answer.
	if (!Mode->EnterEmpireDungeon(DungeonId))
	{
		return FString::Printf(TEXT("Dungeon %d could not be entered."), DungeonId);
	}

	return FString();
}
