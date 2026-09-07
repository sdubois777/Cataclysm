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
