// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * What the version check found in a record's schema version field.
 *
 * ONE ACCEPTED ANSWER. A record is read only when it carries exactly the version this build writes. There is no
 * upgrade step: the owner ruled on 2026-11-27, choosing "Remove; old saves reset", that a save older than the current
 * version is not loaded, so a future change to a record's shape needs no step to bring old files forward. Issue #2928,
 * slice 12, which removed the 45 steps that existed and the tests and example files that held them up.
 */
enum class ECataclysmSaveVersionResult : uint8
{
	/** The record is at the version this build writes. */
	Current,

	/** There is no whole-number version field, so nothing says what shape the record is. */
	NoVersionField,

	/** The field holds a number below the first real version. */
	VersionIsNotReal,

	/** A newer build wrote it. Refused rather than read with whatever that build added left out. */
	NewerThanThisBuild,

	/** An older build wrote it. Refused, and the file is left as it is: old saves are not kept (2026-11-27). */
	OlderThanThisBuild,
};

/**
 * The one rule every saved record comes in by: its version must be this build's.
 *
 * READ BEFORE THE RECORD IS INTERPRETED. `docs/Save_System_Design.md` section 5 rule 1 puts the version first in the
 * file so it can be read without the rest; a record of another version may have fields this build has no property
 * for, or lack ones it needs, and refusing it whole is what keeps a half-read character from being played and then
 * written back over the file.
 */
class CATACLYSM_API FCataclysmSaveVersion
{
public:
	/**
	 * Whether `Record` is at `CurrentVersion`, and if not, why not.
	 *
	 * @param OutMessage  a sentence for the log and the character list whenever the answer is not `Current`.
	 */
	static ECataclysmSaveVersionResult Check(int32 CurrentVersion, const TSharedRef<FJsonObject>& Record,
											 FString& OutMessage);

	/**
	 * The version field as a whole number, or false when it is missing or not a whole number.
	 *
	 * A STRING OF DIGITS COUNTS. A hand-edited file may quote the number, and the field is whole either way.
	 */
	static bool ReadVersion(const TSharedRef<FJsonObject>& Record, int32& OutVersion);

	/** The result in a few words, for a log line or a test failure. */
	static const TCHAR* Describe(ECataclysmSaveVersionResult Result);
};
