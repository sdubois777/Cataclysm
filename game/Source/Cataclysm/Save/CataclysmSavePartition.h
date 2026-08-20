// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmLethality.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmSavePartition.generated.h"

/**
 * The three facts about a character that decide what it shares with any other
 * character, and with which it shares nothing at all.
 *
 * ALL THREE ARE SET AT CREATION AND NONE OF THEM EVER CHANGES. That is the whole
 * reason they can decide a storage layout: a character cannot move between
 * partitions, so a record can be named after them.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmCharacterPartition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	ECataclysmLethality Lethality = ECataclysmLethality::Standard;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	ECataclysmPopulation Population = ECataclysmPopulation::Offline;

	/**
	 * Whether this character was created Solo Self-Found.
	 *
	 * IT NEVER COMES OFF, and it is stricter than a lethality mode: a Solo
	 * Self-Found character shares an empire upgrade tree and a stash with **no
	 * other character at all**, not even another Solo Self-Found one in the same
	 * mode and population.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Cataclysm|Save")
	bool bSoloSelfFound = false;
};

/**
 * Which save record a character reads and writes, and what it shares.
 *
 * WHY THIS IS CODE AT ALL RATHER THAN A NAMING CONVENTION. The partition is a
 * design rule and the save layout is the only place it can be enforced.
 * `docs/Cataclysm_GDD_v2.md`, section "Difficulty Options": "Each lethality mode
 * has its own empire upgrade tree. A character's empire meta-progression is
 * shared with every other character in the same mode and with no character in
 * another one", and "The shared stash and the auction house are partitioned the
 * same way". Getting it wrong does not fail loudly. It quietly pours a Hardcore
 * character's progress into a Standard character's tree, and the only thing that
 * would ever notice is a player.
 *
 * EVERY JUDGEMENT HERE IS A STATIC OVER PLAIN VALUES, so all of it is testable
 * without a world, a save file or a filesystem. `docs/Save_System_Design.md`
 * section 3 is what it implements, and it is a table there.
 */
UCLASS()
class CATACLYSM_API UCataclysmSavePartition : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Whether a character keeps its empire tree and stash in an account record
	 * at all.
	 *
	 * A SOLO SELF-FOUND CHARACTER TOUCHES NO ACCOUNT RECORD. That is the
	 * cleanest expression of the design rule, and it means the strictest mode is
	 * also the simplest to store: one self-contained file. Its empire upgrade
	 * points, its tree, and its own 600-slot stash all live in its character
	 * record, because it shares them with nobody.
	 */
	static bool UsesAnAccountRecord(const FCataclysmCharacterPartition& Character);

	/**
	 * Whether two characters share an empire upgrade tree and a stash.
	 *
	 * THIS IS THE RULE, AND THE SLOT NAME BELOW IS A CONSEQUENCE OF IT. Written
	 * as a question about two characters rather than as a string, because that
	 * is the form the design states it in and the form a mistake is visible in:
	 * a test can ask it about every pair of partitions there is.
	 *
	 * FALSE WHEN EITHER IS SOLO SELF-FOUND, including when both are and
	 * everything else about them matches. A Solo Self-Found character shares
	 * with nobody at all.
	 */
	static bool ShareAnAccountRecord(const FCataclysmCharacterPartition& First,
									 const FCataclysmCharacterPartition& Second);

	/**
	 * The slot the account record for this partition is stored in.
	 *
	 * @return an empty string for a Solo Self-Found character, which has no
	 *         account record. **An empty slot name is the answer and not a
	 *         failure**, and a caller that writes to it anyway would create a
	 *         seventh account record shared by every Solo Self-Found character
	 *         in the game, which is the exact opposite of what the mode means.
	 *
	 * SIX SLOTS AND NO MORE: three lethality modes times two populations. The
	 * names are the ones `docs/Save_System_Design.md` section 4 suggests.
	 */
	static FString AccountSlotName(const FCataclysmCharacterPartition& Character);

	/**
	 * The slot one character's own record is stored in.
	 *
	 * KEYED BY A GENERATED IDENTIFIER AND NOT BY THE CHARACTER'S NAME, so that
	 * renaming is free and two characters may share a name.
	 *
	 * @return an empty string for an identifier that was never set, because a
	 *         record named after nothing would be overwritten by the next
	 *         character to be created with an unset one.
	 */
	static FString CharacterSlotName(const FGuid& CharacterId);

	/** The slot one run's record is stored in. Keyed the same way. */
	static FString RunSlotName(const FGuid& RunId);

	//~ The pieces the names above are built from. Public so a test can check the
	//~ shape of a name without restating the name itself, which would be a
	//~ second copy of the thing under test.

	/** What every account record's slot name starts with. */
	static const TCHAR* AccountPrefix;

	/** What every character record's slot name starts with. */
	static const TCHAR* CharacterPrefix;

	/** What every run record's slot name starts with. */
	static const TCHAR* RunPrefix;

	/** How many account records there can ever be: three modes, two populations. */
	static constexpr int32 AccountRecordCount = 6;
};
