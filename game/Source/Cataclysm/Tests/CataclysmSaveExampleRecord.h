// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/CataclysmSaveRecord.h"
#include "CataclysmSaveExampleRecord.generated.h"

/**
 * A record that exists only for the storage tests. It holds no game state and
 * nothing in the game reads or writes it.
 *
 * WHY IT EXISTS. The storage tests write, queue and read records of a size they
 * choose (`ExampleRecordOfSize`); a record of three plain fields does that without
 * dragging a real record's defaults and nested structs into every check.
 *
 * IT HAD TWO UPGRADE STEPS, and example save files for its versions 1 and 2, to
 * exercise the migration chain. Both went with the chain on 2026-11-27, by the
 * owner's ruling "Remove; old saves reset" (issue #2928 slice 12); the record
 * stays at version 3 and reads only version 3.
 *
 * IT IS COMPILED INTO THE GAME RATHER THAN GUARDED BEHIND
 * `WITH_AUTOMATION_TESTS`, because Unreal Header Tool generates the reflection
 * for a `UCLASS` whether or not the surrounding code is compiled, and a class
 * whose reflection exists while its constructor does not fails to link. It costs
 * three fields' worth of a shipped binary and it holds nothing.
 */
UCLASS(BlueprintType)
class CATACLYSM_API UCataclysmSaveExampleRecord : public UCataclysmSaveRecord
{
	GENERATED_BODY()

public:
	static const FName TypeName;
	static constexpr int32 SchemaVersionNow = 3;

	virtual FName RecordType() const override { return TypeName; }
	virtual int32 CurrentSchemaVersion() const override { return SchemaVersionNow; }

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	FString Label;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 Gold = 0;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Cataclysm|Save")
	int32 Copper = 0;
};
