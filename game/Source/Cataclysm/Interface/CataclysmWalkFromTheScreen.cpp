// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmWalkFromTheScreen.h"

#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Empire/CataclysmEmpireRun.h"

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

FString FCataclysmWalkFromTheScreen::RefusalFor(const UCataclysmEmpireRun* Run, const ACataclysmDungeonGameMode* Mode,
												int32 DungeonId)
{
	if (Run == nullptr)
	{
		return TEXT("No run has been started.");
	}

	// ONLY A DUNGEON LEVEL CAN ENTER A DUNGEON. `EnterEmpireDungeon` is the dungeon game mode's, and there is no
	// travel from any other level into one (issue #48), so the screen says so rather than doing nothing.
	if (Mode == nullptr)
	{
		return NotADungeonLevelText();
	}

	if (Run->FindDungeon(DungeonId) == nullptr)
	{
		return FString::Printf(TEXT("No dungeon %d is standing on the map."), DungeonId);
	}

	// NOT THE DUNGEON BEING WALKED. Entering it again would leave it, run the days the walk owes and start it over on
	// floor 1; the console does that when asked, and a button does not.
	if (Mode->EmpireDungeonId == DungeonId)
	{
		return FString::Printf(TEXT("You are walking dungeon %d now."), DungeonId);
	}

	return FString();
}

FString FCataclysmWalkFromTheScreen::Walk(const UCataclysmEmpireRun* Run, ACataclysmDungeonGameMode* Mode,
										  int32 DungeonId)
{
	const FString Refusal = RefusalFor(Run, Mode, DungeonId);
	if (!Refusal.IsEmpty())
	{
		return Refusal;
	}

	// THE GAME MODE'S OWN REFUSAL IS SHOWN, NOT SWALLOWED. It answers only true or false, so the words are the
	// console's for the same answer.
	if (!Mode->EnterEmpireDungeon(DungeonId))
	{
		return FString::Printf(TEXT("Dungeon %d could not be entered."), DungeonId);
	}

	return FString();
}
