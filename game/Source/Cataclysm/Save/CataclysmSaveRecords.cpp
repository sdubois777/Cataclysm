// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveRecords.h"

#include "Cataclysm.h"

// THE NAMES ARE WRITTEN OUT RATHER THAN TAKEN FROM THE CLASS. Same rule as the
// slot names in UCataclysmSavePartition: these are how a record type is spoken
// about outside the code, so renaming a C++ class must not change them.
const FName UCataclysmAccountSave::TypeName = FName(TEXT("Account"));
const FName UCataclysmCharacterSave::TypeName = FName(TEXT("Character"));
const FName UCataclysmRunSave::TypeName = FName(TEXT("Run"));

int32 UCataclysmCharacterSave::CataclysmOrderSeedFor(const FGuid& Identifier)
{
	// FNV-1a, 32 BITS, ITS TWO CONSTANTS WRITTEN OUT. The header says why this and not `GetTypeHash`; changing either
	// constant, or the byte order below, moves the Cataclysms of every character it seeds.
	uint32 Hash = 2166136261u;
	const uint32 Words[4] = { Identifier.A, Identifier.B, Identifier.C, Identifier.D };
	for (const uint32 Word : Words)
	{
		// MOST SIGNIFICANT BYTE FIRST, the order the identifier's digits are written in, read by shifting so the
		// platform's byte order cannot reach it.
		for (int32 Shift = 24; Shift >= 0; Shift -= 8)
		{
			Hash ^= (Word >> Shift) & 0xFFu;
			Hash *= 16777619u;
		}
	}

	// POSITIVE, as every clock-drawn seed in the game is (`& 0x7FFFFFFF`).
	return static_cast<int32>(Hash & 0x7FFFFFFFu);
}

TArray<TSubclassOf<UCataclysmSaveRecord>> CataclysmSaveRecordClasses()
{
	return {
		UCataclysmAccountSave::StaticClass(),
		UCataclysmCharacterSave::StaticClass(),
		UCataclysmRunSave::StaticClass(),
	};
}
