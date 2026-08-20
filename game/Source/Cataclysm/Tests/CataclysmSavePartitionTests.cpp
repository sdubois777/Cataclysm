// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Save/CataclysmSavePartition.h"

/**
 * Which characters share an empire upgrade tree and a stash, and which share
 * nothing. Issue #529.
 *
 * WHY THIS IS WORTH TESTING HARDER THAN IT LOOKS. Getting a partition wrong does
 * not fail loudly. It quietly pours a Hardcore character's empire progress into a
 * Standard character's tree, or opens one Solo Self-Found character's stash to
 * another, and the only thing that would ever notice is a player. There is no
 * crash and no log line.
 *
 * SO EVERY PAIR IS CHECKED RATHER THAN A FEW. There are twelve partitions --
 * three lethality modes times two populations, twice over for the Solo
 * Self-Found flag -- which is 144 ordered pairs, and the whole grid is walked.
 * A handful of examples would pass with the flag ignored entirely.
 *
 * WHAT THIS DOES NOT COVER. Anything reaching a file. These are static functions
 * over plain values; whether the slot names they produce are actually written and
 * read back is the storage layer's own tests.
 */
namespace CataclysmSavePartitionTest
{
	using FPartition = UCataclysmSavePartition;

	/** The three lethality modes, in the design document's order. */
	constexpr ECataclysmLethality Modes[] = {
		ECataclysmLethality::Standard,
		ECataclysmLethality::Hardcore,
		ECataclysmLethality::Heretic,
	};

	constexpr ECataclysmPopulation Populations[] = {
		ECataclysmPopulation::Offline,
		ECataclysmPopulation::Online,
	};

	/** Every partition a character can be created into. */
	static TArray<FCataclysmCharacterPartition> EveryPartition()
	{
		TArray<FCataclysmCharacterPartition> All;
		for (const ECataclysmLethality Mode : Modes)
		{
			for (const ECataclysmPopulation Population : Populations)
			{
				for (const bool bSolo : { false, true })
				{
					FCataclysmCharacterPartition One;
					One.Lethality = Mode;
					One.Population = Population;
					One.bSoloSelfFound = bSolo;
					All.Add(One);
				}
			}
		}
		return All;
	}

	/** One partition, said in words, so a failure names the case. */
	static FString Describe(const FCataclysmCharacterPartition& One)
	{
		return FString::Printf(
			TEXT("mode %d, population %d, solo self-found %s"),
			static_cast<int32>(One.Lethality),
			static_cast<int32>(One.Population),
			One.bSoloSelfFound ? TEXT("yes") : TEXT("no"));
	}

	static FCataclysmCharacterPartition Made(ECataclysmLethality Mode,
										ECataclysmPopulation Population,
										bool bSolo)
	{
		FCataclysmCharacterPartition One;
		One.Lethality = Mode;
		One.Population = Population;
		One.bSoloSelfFound = bSolo;
		return One;
	}
}

// ---------------------------------------------------------------------------
// Who shares with whom
// ---------------------------------------------------------------------------

/**
 * Two characters share an account record exactly when the design says they do.
 *
 * THE RULE, FROM `docs/Cataclysm_GDD_v2.md` SECTION "Difficulty Options": a
 * character's empire meta-progression is shared with every other character in the
 * same lethality mode and with no character in another one, the stash is
 * partitioned the same way, the offline and online populations are separate, and
 * Solo Self-Found shares with nobody at all.
 *
 * CHECKED OVER THE WHOLE GRID, because the mistakes this catches are the ones
 * that look right in one example: ignoring the population, ignoring the mode, or
 * letting two Solo Self-Found characters share with each other.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveSharingIsTheDesignedRule,
	"Cataclysm.SavePartition.CharactersShareExactlyWhatTheDesignSaysTheyDo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveSharingIsTheDesignedRule::RunTest(const FString&)
{
	using namespace CataclysmSavePartitionTest;

	const TArray<FCataclysmCharacterPartition> All = EveryPartition();
	TestEqual(TEXT("there are twelve partitions"), All.Num(), 12);

	for (const FCataclysmCharacterPartition& First : All)
	{
		for (const FCataclysmCharacterPartition& Second : All)
		{
			// WHAT THE DESIGN SAYS, WORKED OUT HERE RATHER THAN ASKED OF THE
			// CODE UNDER TEST. Restating it is the point: a test that called
			// ShareAnAccountRecord to decide what it expects would agree with
			// any answer at all.
			const bool bExpected =
				!First.bSoloSelfFound && !Second.bSoloSelfFound
				&& First.Lethality == Second.Lethality
				&& First.Population == Second.Population;

			const bool bAnswered = FPartition::ShareAnAccountRecord(First, Second);

			if (bAnswered != bExpected)
			{
				AddError(FString::Printf(
					TEXT("sharing between [%s] and [%s] answered %s, expected %s"),
					*Describe(First), *Describe(Second),
					bAnswered ? TEXT("yes") : TEXT("no"),
					bExpected ? TEXT("yes") : TEXT("no")));
				return false;
			}
		}
	}

	return true;
}

/**
 * A Solo Self-Found character shares with nobody, including its own kind.
 *
 * SEPARATED OUT OF THE GRID ABOVE ON PURPOSE. It is the one case a reader is
 * likely to get wrong from the words alone -- "shared with no other character at
 * all" reads to some people as "shared with the others who chose it" -- and the
 * grid would report it as one failure among many rather than as this.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveSoloSelfFoundSharesWithNobody,
	"Cataclysm.SavePartition.SoloSelfFoundSharesWithNobodyIncludingItsOwnKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveSoloSelfFoundSharesWithNobody::RunTest(const FString&)
{
	using namespace CataclysmSavePartitionTest;

	const FCataclysmCharacterPartition Solo = Made(
		ECataclysmLethality::Hardcore, ECataclysmPopulation::Offline, true);
	const FCataclysmCharacterPartition AlsoSolo = Made(
		ECataclysmLethality::Hardcore, ECataclysmPopulation::Offline, true);
	const FCataclysmCharacterPartition Ordinary = Made(
		ECataclysmLethality::Hardcore, ECataclysmPopulation::Offline, false);

	TestFalse(TEXT("two Solo Self-Found characters alike in every other way "
				   "still share nothing"),
		FPartition::ShareAnAccountRecord(Solo, AlsoSolo));

	TestFalse(TEXT("and it shares nothing with an ordinary character in the "
				   "same mode and population"),
		FPartition::ShareAnAccountRecord(Solo, Ordinary));

	TestFalse(TEXT("in either direction"),
		FPartition::ShareAnAccountRecord(Ordinary, Solo));

	// THE ORDINARY PAIR DOES SHARE, which is what makes the three above mean
	// something rather than the function answering no to everything.
	TestTrue(TEXT("while two ordinary characters in that mode and population do"),
		FPartition::ShareAnAccountRecord(Ordinary, Ordinary));

	TestFalse(TEXT("a Solo Self-Found character uses no account record"),
		FPartition::UsesAnAccountRecord(Solo));
	TestTrue(TEXT("and an ordinary one does"),
		FPartition::UsesAnAccountRecord(Ordinary));

	return true;
}

// ---------------------------------------------------------------------------
// What the records are called
// ---------------------------------------------------------------------------

/**
 * Six account records, one per partition, and every one of them distinct.
 *
 * WHAT A COLLISION WOULD DO. Two partitions answering the same slot name is the
 * whole fault this class exists to prevent, expressed in a filename: the two
 * would read and write each other's empire tree and stash.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveAccountSlotsAreDistinct,
	"Cataclysm.SavePartition.ThereAreSixAccountRecordsAndNoTwoShareASlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveAccountSlotsAreDistinct::RunTest(const FString&)
{
	using namespace CataclysmSavePartitionTest;

	TSet<FString> Seen;
	for (const FCataclysmCharacterPartition& One : EveryPartition())
	{
		const FString Slot = FPartition::AccountSlotName(One);

		if (One.bSoloSelfFound)
		{
			// NO ACCOUNT RECORD AT ALL. Answering a name here would give every
			// Solo Self-Found character in the game one shared record.
			TestTrue(FString::Printf(
				TEXT("[%s] names no account record"), *Describe(One)),
				Slot.IsEmpty());
			continue;
		}

		if (!TestFalse(FString::Printf(
				TEXT("[%s] names an account record"), *Describe(One)),
				Slot.IsEmpty()))
		{
			return false;
		}

		if (Seen.Contains(Slot))
		{
			AddError(FString::Printf(
				TEXT("[%s] answers the slot '%s', which another partition "
					 "already uses. The two would share an empire tree and a "
					 "stash."), *Describe(One), *Slot));
			return false;
		}
		Seen.Add(Slot);
	}

	TestEqual(TEXT("there are six account records and no more"),
		Seen.Num(), FPartition::AccountRecordCount);

	// AND EACH SAYS WHICH PARTITION IT IS. Names that are distinct but opaque --
	// Account_1 through Account_6 -- would satisfy everything above and be
	// unreadable in a directory listing, which is half of why JSON was chosen.
	TestTrue(TEXT("an offline Hardcore character's record says so"),
		FPartition::AccountSlotName(
			Made(ECataclysmLethality::Hardcore, ECataclysmPopulation::Offline,
				 false)) == TEXT("Account_Offline_Hardcore"));

	TestTrue(TEXT("and an online Heretic character's does"),
		FPartition::AccountSlotName(
			Made(ECataclysmLethality::Heretic, ECataclysmPopulation::Online,
				 false)) == TEXT("Account_Online_Heretic"));

	return true;
}

/**
 * A character and a run are named by an identifier, and an unset one names
 * nothing.
 *
 * THE UNSET CASE IS THE ONE THAT MATTERS. Without it, every character created
 * before an identifier was generated would be written to one slot and each would
 * overwrite the last -- which looks exactly like saving not working, rather than
 * like a missing identifier.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSaveRecordsAreNamedByIdentifier,
	"Cataclysm.SavePartition.ACharacterIsNamedByItsIdentifierAndNotItsName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSaveRecordsAreNamedByIdentifier::RunTest(const FString&)
{
	using namespace CataclysmSavePartitionTest;

	const FGuid One(0x11111111, 0x22222222, 0x33333333, 0x44444444);
	const FGuid Two(0x55555555, 0x66666666, 0x77777777, 0x88888888);

	const FString First = FPartition::CharacterSlotName(One);
	const FString Second = FPartition::CharacterSlotName(Two);

	TestFalse(TEXT("a character with an identifier is named"), First.IsEmpty());
	TestTrue(TEXT("and the name says it is a character"),
		First.StartsWith(FPartition::CharacterPrefix));
	TestTrue(TEXT("two characters are named differently"), First != Second);

	// NO SEPARATORS INSIDE THE IDENTIFIER. A slot name becomes a filename, and
	// the engine's default identifier format contains no characters a filesystem
	// objects to -- but that is the default rather than a guarantee, so the
	// format is asked for rather than assumed.
	TestFalse(TEXT("the name contains no braces or dashes from the identifier"),
		First.Contains(TEXT("{")) || First.Contains(TEXT("-")));

	// AN UNSET IDENTIFIER NAMES NOTHING.
	TestTrue(TEXT("a character with no identifier names no slot"),
		FPartition::CharacterSlotName(FGuid()).IsEmpty());
	TestTrue(TEXT("and neither does a run"),
		FPartition::RunSlotName(FGuid()).IsEmpty());

	// A RUN AND A CHARACTER WITH THE SAME IDENTIFIER ARE STILL DIFFERENT SLOTS,
	// which nothing generates today and which costs one prefix to guarantee.
	TestTrue(TEXT("a run and a character never share a slot"),
		FPartition::RunSlotName(One) != FPartition::CharacterSlotName(One));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
