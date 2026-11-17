// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSavePartition.h"

const TCHAR* UCataclysmSavePartition::AccountPrefix = TEXT("Account");
const TCHAR* UCataclysmSavePartition::CharacterPrefix = TEXT("Character");
const TCHAR* UCataclysmSavePartition::RunPrefix = TEXT("Run");

namespace
{
	/**
	 * What each lethality mode is called in a slot name.
	 *
	 * WRITTEN OUT RATHER THAN TAKEN FROM THE ENUM'S OWN NAME. `UEnum::GetNameStringByValue`
	 * would answer with whatever the identifier happens to be, so renaming the
	 * enumerator in C++ would rename every existing save slot and orphan every
	 * record already written. A slot name is a filename that has to outlive the
	 * code that made it.
	 */
	const TCHAR* NameOf(ECataclysmLethality Lethality)
	{
		switch (Lethality)
		{
			case ECataclysmLethality::Standard: return TEXT("Standard");
			case ECataclysmLethality::Hardcore: return TEXT("Hardcore");
			case ECataclysmLethality::Heretic:  return TEXT("Heretic");
		}

		// A MODE THE LADDER DOES NOT HOLD. There is no fourth today and the
		// switch above covers every value the enum declares; this exists so that
		// adding one without adding a name here is a slot called "Unknown" that
		// a person notices, rather than a silent fall-through to Standard, which
		// would pour the new mode's progress into Standard's tree.
		return TEXT("Unknown");
	}

	/** The same, for whether a character was created offline or online. */
	const TCHAR* NameOf(ECataclysmPopulation Population)
	{
		switch (Population)
		{
			case ECataclysmPopulation::Offline: return TEXT("Offline");
			case ECataclysmPopulation::Online:  return TEXT("Online");
		}

		return TEXT("Unknown");
	}
}

bool UCataclysmSavePartition::UsesAnAccountRecord(
	const FCataclysmCharacterPartition& Character)
{
	// SOLO SELF-FOUND AND NOTHING ELSE. The lethality mode and the population
	// decide WHICH account record; only this decides whether there is one.
	return !Character.bSoloSelfFound;
}

bool UCataclysmSavePartition::ShareAnAccountRecord(
	const FCataclysmCharacterPartition& First, const FCataclysmCharacterPartition& Second)
{
	// EITHER ONE BEING SOLO SELF-FOUND IS ENOUGH, and that includes both of them
	// being Solo Self-Found with everything else matching. The mode means shared
	// with nobody at all, not shared with the others who chose it.
	if (!UsesAnAccountRecord(First) || !UsesAnAccountRecord(Second))
	{
		return false;
	}

	return First.Lethality == Second.Lethality
		&& First.Population == Second.Population;
}

FString UCataclysmSavePartition::AccountSlotName(
	const FCataclysmCharacterPartition& Character)
{
	if (!UsesAnAccountRecord(Character))
	{
		// NO RECORD, AND THAT IS THE ANSWER RATHER THAN A FAILURE. See the
		// header: a caller that writes to an empty name anyway would create one
		// more account record shared by every Solo Self-Found character there
		// is, which is the opposite of what the mode means.
		return FString();
	}

	// THE ORDER IS POPULATION THEN MODE, matching the names
	// `docs/Save_System_Design.md` section 4 suggests, so the six sort into two
	// groups of three in a directory listing.
	return FString::Printf(TEXT("%s_%s_%s"), AccountPrefix,
						   NameOf(Character.Population),
						   NameOf(Character.Lethality));
}

FString UCataclysmSavePartition::CharacterSlotName(const FGuid& CharacterId)
{
	if (!CharacterId.IsValid())
	{
		// AN IDENTIFIER THAT WAS NEVER SET NAMES NOTHING. Without this, every
		// character created before one was generated would be written to the
		// same slot and each would overwrite the last.
		return FString();
	}

	// EIGHT DIGITS OF EACH OF THE FOUR PARTS, WITH NO SEPARATORS, which is what
	// FGuid::ToString defaults to. It is chosen explicitly rather than left to
	// the default so that changing the engine's default cannot rename every
	// character record already written.
	return FString::Printf(TEXT("%s_%s"), CharacterPrefix,
						   *CharacterId.ToString(EGuidFormats::Digits));
}

FString UCataclysmSavePartition::RunSlotName(const FGuid& RunId)
{
	if (!RunId.IsValid())
	{
		return FString();
	}

	return FString::Printf(TEXT("%s_%s"), RunPrefix,
						   *RunId.ToString(EGuidFormats::Digits));
}
