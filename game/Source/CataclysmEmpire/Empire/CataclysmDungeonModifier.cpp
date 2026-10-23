// Copyright Stephen Dubois. All Rights Reserved.

#include "Empire/CataclysmDungeonModifier.h"

int32 UCataclysmDungeonModifierRules::CountFor(
	int32 DifficultyTier, ECataclysmDungeonSubType SubType)
{
	// NOT CLAMPED FROM ABOVE. Eight is the highest tier the design has, and a
	// tier above it is a caller's mistake rather than a rung; clamping here as
	// well would hide it.
	//
	// THIS USED TO SAY `Begin` WAS WHERE A RUN'S TIER IS BOUNDED. It is not, and
	// never was: `UCataclysmEmpireRun::Begin` stores what it is given. The tier a
	// run takes FROM THE GAME is bounded, because
	// `ACataclysmGameMode::DifficultyTierFor` clamps before
	// `UCataclysmGameInstance::BeginEmpireRun` hands it over, and
	// `Cataclysm.EmpireBegin` clamps an argument typed at the console. A caller
	// that calls `Begin` directly can still pass anything, which is what the
	// clamp from below on the next line answers.
	const int32 Tier = FMath::Max(0, DifficultyTier);
	const int32 Base = Tier * ModifiersPerTier;

	return SubType == ECataclysmDungeonSubType::Sacrificial
		? Base * SacrificialMultiplier
		: Base;
}

TArray<FCataclysmDungeonModifier> UCataclysmDungeonModifierRules::PoolFor(
	const TArray<FCataclysmDungeonModifier>& All,
	const TArray<ECataclysmType>& Active)
{
	TArray<FCataclysmDungeonModifier> Pool;

	for (const FCataclysmDungeonModifier& Modifier : All)
	{
		// THE `Generic` COLUMN IS EXCLUDED BY THIS TEST AND NOT BY A SECOND ONE.
		// Its rows carry `None`, which is never in an active set. See the header.
		if (Active.Contains(Modifier.Cataclysm))
		{
			Pool.Add(Modifier);
		}
	}

	// SORTED, BECAUSE A `UDataTable` IS A MAP and the caller built this by
	// walking one. See the header.
	Pool.Sort([](const FCataclysmDungeonModifier& A,
				 const FCataclysmDungeonModifier& B)
	{
		return A.RowKey.LexicalLess(B.RowKey);
	});

	return Pool;
}

TArray<FCataclysmDungeonModifier> UCataclysmDungeonModifierRules::Draw(
	const TArray<FCataclysmDungeonModifier>& Pool, int32 Count,
	FRandomStream& Stream)
{
	TArray<FCataclysmDungeonModifier> Drawn;
	if (Count <= 0)
	{
		return Drawn;
	}

	// A COPY, SO THE CALLER'S POOL IS NOT REORDERED. The run holds one pool for
	// its whole length and draws from it once per dungeon; a draw that emptied
	// it would leave the second dungeon with nothing.
	TArray<FCataclysmDungeonModifier> Remaining = Pool;

	while (Drawn.Num() < Count && Remaining.Num() > 0)
	{
		const int32 Index = Stream.RandRange(0, Remaining.Num() - 1);
		Drawn.Add(Remaining[Index]);

		// `RemoveAt` AND NOT `RemoveAtSwap`. Swapping is cheaper and reorders
		// what is left, which makes the draw depend on the order things were
		// removed in as well as on the stream. `UCataclysmEnemyModifiers::Draw`
		// says the same thing about the same choice.
		Remaining.RemoveAt(Index);
	}

	return Drawn;
}

float UCataclysmDungeonModifierRules::DangerOf(
	const TArray<FCataclysmDungeonModifier>& Drawn)
{
	float Total = 0.0f;
	for (const FCataclysmDungeonModifier& Modifier : Drawn)
	{
		Total += Modifier.Danger;
	}

	return Total;
}

TArray<FName> UCataclysmDungeonModifierRules::KeysOf(
	const TArray<FCataclysmDungeonModifier>& Drawn)
{
	TArray<FName> Keys;
	Keys.Reserve(Drawn.Num());
	for (const FCataclysmDungeonModifier& Modifier : Drawn)
	{
		Keys.Add(Modifier.RowKey);
	}

	return Keys;
}

// ---------------------------------------------------------------------------
// The Divine Edicts and the Ascendant -- 2026-10-22
// ---------------------------------------------------------------------------

const TArray<FName>& UCataclysmDungeonModifierRules::EdictKeys()
{
	// ALL EIGHT THE DESIGN NAMES, in the design's order: its table runs Silence, Temperance, Patience,
	// Abstinence, Stillness, Vigil, Humility, Tithe. Temperance, Patience and Stillness became rows on 2026-10-22
	// (G7b); Abstinence, Vigil and Humility on 2026-10-23 (G7c).
	//
	// THE ORDER IS READ BY THE DRAW, so adding a key moves every Ascendant's order: `EdictOrderFor` shuffles this
	// list on the walk seed's stream, and the same seed shuffles a longer list differently.
	static const TArray<FName> Keys = {
		FName(TEXT("Celestial_Edict_of_Silence")),
		FName(TEXT("Celestial_Edict_of_Temperance")),
		FName(TEXT("Celestial_Edict_of_Patience")),
		FName(TEXT("Celestial_Edict_of_Abstinence")),
		FName(TEXT("Celestial_Edict_of_Stillness")),
		FName(TEXT("Celestial_Edict_of_Vigil")),
		FName(TEXT("Celestial_Edict_of_Humility")),
		FName(TEXT("Celestial_Forced_Tithes")),
	};
	return Keys;
}

bool UCataclysmDungeonModifierRules::IsEdict(FName RowKey)
{
	return EdictKeys().Contains(RowKey);
}

FString UCataclysmDungeonModifierRules::EdictName(FName RowKey)
{
	if (RowKey == FName(TEXT("Celestial_Edict_of_Silence")))
	{
		return TEXT("Edict of Silence");
	}
	if (RowKey == FName(TEXT("Celestial_Edict_of_Temperance")))
	{
		return TEXT("Edict of Temperance");
	}
	if (RowKey == FName(TEXT("Celestial_Edict_of_Patience")))
	{
		return TEXT("Edict of Patience");
	}
	if (RowKey == FName(TEXT("Celestial_Edict_of_Abstinence")))
	{
		return TEXT("Edict of Abstinence");
	}
	if (RowKey == FName(TEXT("Celestial_Edict_of_Stillness")))
	{
		return TEXT("Edict of Stillness");
	}
	if (RowKey == FName(TEXT("Celestial_Edict_of_Vigil")))
	{
		return TEXT("Edict of Vigil");
	}
	if (RowKey == FName(TEXT("Celestial_Edict_of_Humility")))
	{
		return TEXT("Edict of Humility");
	}
	if (RowKey == FName(TEXT("Celestial_Forced_Tithes")))
	{
		return TEXT("Edict of Tithe");
	}
	return FString();
}

TArray<FName> UCataclysmDungeonModifierRules::EdictOrderFor(int32 WalkSeed)
{
	// EVERY EDICT AS A ROW, and the draw reads nothing of a row but its key.
	TArray<FCataclysmDungeonModifier> All;
	for (const FName& Key : EdictKeys())
	{
		FCataclysmDungeonModifier Edict;
		Edict.RowKey = Key;
		Edict.Cataclysm = ECataclysmType::Celestial;
		All.Add(Edict);
	}

	// THE DUNGEON'S OWN STREAM, from its walk seed alone, so the order is the
	// same whoever asks and nothing the run draws moves it.
	FRandomStream Stream(UCataclysmRoster::MixedSeed(WalkSeed, EdictSalt));
	return KeysOf(Draw(All, All.Num(), Stream));
}

TArray<FName> UCataclysmDungeonModifierRules::EdictsOnFloor(
	int32 WalkSeed, const TArray<FName>& AlreadyActive, int32 FloorNumber)
{
	const int32 Wanted = FMath::Clamp(FloorNumber, 0, AscendantMostEdicts);

	TArray<FName> Added;
	for (const FName& Edict : EdictOrderFor(WalkSeed))
	{
		if (Added.Num() >= Wanted)
		{
			break;
		}

		// ALREADY IN FORCE IS ALREADY ACTIVE, AND NOT DRAWN AGAIN.
		if (!AlreadyActive.Contains(Edict))
		{
			Added.Add(Edict);
		}
	}

	return Added;
}

FName UCataclysmDungeonModifierRules::EdictLeftBy(
	int32 WalkSeed, const TArray<FName>& AlreadyActive, int32 Day)
{
	const TArray<FName> Held =
		EdictsOnFloor(WalkSeed, AlreadyActive, AscendantMostEdicts);
	if (Held.IsEmpty())
	{
		return NAME_None;
	}

	FRandomStream Stream(UCataclysmRoster::MixedSeed(
		UCataclysmRoster::MixedSeed(WalkSeed, LeftEdictSalt), Day));
	return Held[Stream.RandRange(0, Held.Num() - 1)];
}

float UCataclysmDungeonModifierRules::EdictStrengthOn(int32 FloorNumber,
													  bool bTheCoreStandsOnIt)
{
	const int32 Floor = FMath::Max(1, FloorNumber);
	const float Deeper = 1.0f + EdictStrongerPerFloor * (Floor - 1);
	return bTheCoreStandsOnIt ? Deeper * EdictStrongerWhileTheCoreStands : Deeper;
}
