// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveRecords.h"

#include "Cataclysm.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"

// THE NAMES ARE WRITTEN OUT RATHER THAN TAKEN FROM THE CLASS. Same rule as the
// slot names in UCataclysmSavePartition: these are how a record type is spoken
// about outside the code, so renaming a C++ class must not change them.
const FName UCataclysmAccountSave::TypeName = FName(TEXT("Account"));
const FName UCataclysmCharacterSave::TypeName = FName(TEXT("Character"));
const FName UCataclysmRunSave::TypeName = FName(TEXT("Run"));

/**
 * A NAMED NAMESPACE AND NOT AN ANONYMOUS ONE, WHICH IS THE ORDINARY CHOICE.
 *
 * Three rules apply to a migration step and an anonymous namespace cannot
 * satisfy all three at once, because `CataclysmSaveExampleRecord.cpp` already
 * has a `Migrate_1_to_2` and both files are in the Cataclysm module:
 *
 *   1. The registered label reads `Migrate_N_to_N+1`. Section 5 rule 3 of
 *      `docs/Save_System_Design.md`, checked by
 *      `tools/tests/test_save_migrations_are_single_steps.py`.
 *   2. The label matches the function it names, so a failure message points at
 *      the right step. Same test.
 *   3. A helper in an anonymous namespace is unique across its module, because
 *      Unreal merges a module's .cpp files into one translation unit. Checked
 *      by `tools/tests/test_no_two_files_share_an_anonymous_helper.py`.
 *
 * Naming the namespace keeps rules 1 and 2 -- the function really is called
 * `Migrate_1_to_2` -- and answers rule 3's actual concern, which is a symbol
 * collision in the unity blob, by qualifying the symbol instead of hiding it.
 * That collision is not hypothetical: it compiles locally and fails on
 * `development`, because UnrealBuildTool keeps modified files out of the blob.
 */
namespace CataclysmCharacterSaveMigration
{
	/**
	 * A character written before attribute allocation existed has spent nothing.
	 *
	 * THE FIELD IS WRITTEN OUT RATHER THAN LEFT TO DEFAULT. Deserialisation
	 * leaves a missing struct zeroed anyway, so this step could be empty and
	 * still be correct. It is not empty because a migrated record ought to
	 * describe itself completely: somebody reading the file should not have to
	 * know what a C++ default is to know what the character spent.
	 *
	 * IT MUST NOT WRITE THE VERSION FIELD. `FCataclysmSaveMigration::Migrate`
	 * owns that, and a step doing it as well would be two places disagreeing.
	 */
	bool Migrate_1_to_2(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		const TSharedRef<FJsonObject> Nothing = MakeShared<FJsonObject>();
		for (const FString& Name : FCataclysmAttributePoints::Names())
		{
			// CAPITALISED, BECAUSE THAT IS HOW THE STRUCT SPELLS ITS FIELDS and
			// the JSON is written from the struct's own property names.
			// FCataclysmAttributePoints::Names answers the data table's
			// spelling, which is lower case.
			FString Field = Name;
			Field[0] = FChar::ToUpper(Field[0]);
			Nothing->SetNumberField(Field, 0);
		}

		Record->SetObjectField(TEXT("SpentAttributePoints"), Nothing);
		OutError.Empty();
		return true;
	}

	/**
	 * The 24 classes as they stood on 2026-09-24, three to a damage type, in the
	 * order `UCataclysmCharacterCreation::ClassesFor` gave them that day.
	 *
	 * A FROZEN COPY AND NOT A CALL. A step describes how one version of the file
	 * became the next, and what it did must not change when the class list does.
	 *
	 * A NODE'S TREE IS READ FROM ITS NAME. Every passive node is named for its
	 * tree followed by an underscore -- `Masochist_basic_spine_000` -- and a step
	 * may not look nodes up in the game's own tables. That every node is named
	 * this way, for a tree in this list, is checked by
	 * `tools/tests/test_passive_nodes_are_named_for_a_frozen_class.py`, which
	 * reads this list.
	 */
	const TCHAR* const ClassesOn20260924[8][3] = {
		{ TEXT("Bulwark"), TEXT("Berserker"), TEXT("Saboteur") },
		{ TEXT("Ravager"), TEXT("Ritualist"), TEXT("Masochist") },
		{ TEXT("Soul Collector"), TEXT("Necromancer"), TEXT("Shadow") },
		{ TEXT("Plague Lord"), TEXT("Virion"), TEXT("Poison Master") },
		{ TEXT("Vampire"), TEXT("Energy Leech"), TEXT("Shield Breaker") },
		{ TEXT("Nephilim"), TEXT("Zealous Inquisitor"), TEXT("Dawnbringer") },
		{ TEXT("Agent of Chaos"), TEXT("Chaos Shaper"), TEXT("Discordant Trickster") },
		{ TEXT("Singularity"), TEXT("Avatar of Madness"), TEXT("The Maw") },
	};

	/** Which damage type and which of its classes a node belongs to, or false
	 *  for a name that starts with none of them. */
	bool ClassOfNode(const FString& Node, int32& OutType, int32& OutClass)
	{
		for (int32 Type = 0; Type < 8; ++Type)
		{
			for (int32 Class = 0; Class < 3; ++Class)
			{
				const FString Prefix =
					FString(ClassesOn20260924[Type][Class]).Replace(TEXT(" "), TEXT(""))
					+ TEXT("_");
				if (Node.StartsWith(Prefix, ESearchCase::CaseSensitive))
				{
					OutType = Type;
					OutClass = Class;
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * A character may spend in only one class tree for each damage type.
	 *
	 * ISSUE #2064. The project owner decided it on 2026-09-24, and a character
	 * written before the rule was enforced may hold points in two or three trees
	 * of one damage type. For each such damage type this keeps the tree with the
	 * most points and removes the other trees' nodes, which returns their points:
	 * the points left to spend are the level's less the allocation's total. The
	 * player is not asked. This is a one-time change to development saves, ruled
	 * by the coordinating session under the owner's delegation.
	 *
	 * A TIE KEEPS THE TREE THE FIRST POINT WENT INTO, which the file always
	 * answers: a node is appended to the list the first time it is bought, so
	 * the first node of a tree in the list is that tree's first purchase. The
	 * ruling's further fallback, for when that is unknown, is therefore never
	 * reached and is not written.
	 *
	 * A NODE NAMED FOR NO CLASS IN THE LIST IS KEPT, since this step has no
	 * grounds to call it anything's rival.
	 */
	bool Migrate_2_to_3(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();

		const TSharedPtr<FJsonObject>* Allocation = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!Record->TryGetObjectField(TEXT("PassiveAllocation"), Allocation)
			|| !Allocation || !Allocation->IsValid()
			|| !(*Allocation)->TryGetArrayField(TEXT("Nodes"), Nodes) || !Nodes)
		{
			// NOTHING SPENT, so nothing to choose between.
			return true;
		}

		int32 Points[8][3] = {};
		int32 FirstAt[8][3];
		for (int32 Type = 0; Type < 8; ++Type)
		{
			for (int32 Class = 0; Class < 3; ++Class)
			{
				FirstAt[Type][Class] = MAX_int32;
			}
		}

		for (int32 Index = 0; Index < Nodes->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Spent = (*Nodes)[Index]->AsObject();
			int32 Type = 0;
			int32 Class = 0;
			if (!Spent.IsValid()
				|| !ClassOfNode(Spent->GetStringField(TEXT("Node")), Type, Class))
			{
				continue;
			}
			const int32 Count = static_cast<int32>(Spent->GetNumberField(TEXT("Points")));
			if (Count <= 0)
			{
				continue;
			}
			Points[Type][Class] += Count;
			FirstAt[Type][Class] = FMath::Min(FirstAt[Type][Class], Index);
		}

		int32 Kept[8];
		for (int32 Type = 0; Type < 8; ++Type)
		{
			Kept[Type] = INDEX_NONE;
			for (int32 Class = 0; Class < 3; ++Class)
			{
				if (Points[Type][Class] <= 0)
				{
					continue;
				}
				const int32 Best = Kept[Type];
				if (Best == INDEX_NONE
					|| Points[Type][Class] > Points[Type][Best]
					|| (Points[Type][Class] == Points[Type][Best]
						&& FirstAt[Type][Class] < FirstAt[Type][Best]))
				{
					Kept[Type] = Class;
				}
			}
			for (int32 Class = 0; Class < 3; ++Class)
			{
				if (Class != Kept[Type] && Points[Type][Class] > 0)
				{
					UE_LOG(LogCataclysm, Log,
						TEXT("Save migration 2 to 3: %d points in %s refunded; %s is kept "
							 "as this character's class for its damage type (issue #2064)."),
						Points[Type][Class], ClassesOn20260924[Type][Class],
						ClassesOn20260924[Type][Kept[Type]]);
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> Remaining;
		for (const TSharedPtr<FJsonValue>& Value : *Nodes)
		{
			const TSharedPtr<FJsonObject> Spent = Value->AsObject();
			int32 Type = 0;
			int32 Class = 0;
			if (Spent.IsValid()
				&& ClassOfNode(Spent->GetStringField(TEXT("Node")), Type, Class)
				&& Kept[Type] != INDEX_NONE && Class != Kept[Type])
			{
				continue;
			}
			Remaining.Add(Value);
		}
		(*Allocation)->SetArrayField(TEXT("Nodes"), Remaining);
		return true;
	}

	/**
	 * A character written before it carried a difficulty tier is at tier 1, where the design starts every character.
	 * Issue #1472, ruled 2026-11-01.
	 *
	 * WRITTEN OUT RATHER THAN LEFT TO DEFAULT, for `Migrate_1_to_2`'s reason: a migrated record ought to describe itself
	 * completely. A version 3 file has no tier key, so nothing is overwritten.
	 */
	bool Migrate_3_to_4(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetNumberField(TEXT("DifficultyTier"), 1); // A migration's: tier 1, written out.
		return true;
	}

	/**
	 * A character written before it carried the Consumption mark is not marked: nothing could mark one. Issue #2688,
	 * ruled 2026-11-02. Written out, for `Migrate_3_to_4`'s reason.
	 */
	bool Migrate_4_to_5(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetBoolField(TEXT("bMarkedByConsumption"), false); // A migration's: not marked, written out.
		return true;
	}

	/**
	 * A character written before it carried its crossing's build holds none: nothing captured one. Issue #2688, the
	 * owner's decision of 2026-11-02 (O4). Written out, for `Migrate_3_to_4`'s reason, as the whole empty build in the
	 * shape the record writes it, so the migrated file describes itself completely. A character marked at version 5
	 * therefore has no build, and its double is built from the gear worn when it arrives.
	 *
	 * THE EMPTY BUILD IS WRITTEN FROM THE STRUCT'S DEFAULTS, NOT FROM `game/Data/`: section 5, rule 4.
	 */
	bool Migrate_5_to_6(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		const FCataclysmPlayerBuild None;
		const TSharedRef<FJsonObject> Empty = MakeShared<FJsonObject>();
		if (!FJsonObjectConverter::UStructToJsonObject(FCataclysmPlayerBuild::StaticStruct(), &None, Empty,
													   CPF_SaveGame, 0, nullptr,
											   EJsonObjectConversionFlags::SkipStandardizeCase))
		{
			OutError = TEXT("could not write an empty build");
			return false;
		}
		Record->SetObjectField(TEXT("ConsumptionBuild"), Empty); // A migration's: no build, written out.
		return true;
	}

	/**
	 * A character written before it carried a Cataclysm order seed is given the one its identifier gives, so it keeps one
	 * fixed draw from then on. Issue #1324 slice 6a, ruled 2026-11-02. Written out, for `Migrate_3_to_4`'s reason.
	 *
	 * THE IDENTIFIER IS READ FROM THE RECORD, AS THE SAVE WRITES IT (32 hex digits). A record with no identifier, or one
	 * that does not parse, gets the seed of the zero identifier: still one fixed seed, and no refusal, since a character
	 * that cannot be named by its identifier is not one this step should stop loading.
	 */
	bool Migrate_6_to_7(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		FString IdentifierText;
		FGuid Identifier;
		if (!Record->TryGetStringField(TEXT("CharacterId"), IdentifierText) || !FGuid::Parse(IdentifierText, Identifier))
		{
			Identifier = FGuid(); // A migration's: an unreadable identifier is the zero one.
		}
		Record->SetNumberField(TEXT("CataclysmOrderSeed"),
							   UCataclysmCharacterSave::CataclysmOrderSeedFor(Identifier)); // A migration's: the identifier's seed.
		return true;
	}

	/**
	 * A character written before gold existed holds none of its own. Issue #2742, 2026-11-18: a Solo Self-Found
	 * character's gold is on its character record, and no character could hold any before the field. Written out, for
	 * `Migrate_3_to_4`'s reason. A key already holding a number is kept, and one holding anything else is refused, as
	 * the account record's `Migrate_2_to_3` does.
	 */
	bool Migrate_7_to_8(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		double Held = 0.0;
		if (Record->HasField(TEXT("Gold")) && !Record->TryGetNumberField(TEXT("Gold"), Held))
		{
			OutError = TEXT("Gold holds something that is not a number");
			return false;
		}
		Record->SetNumberField(TEXT("Gold"), Held); // A migration's: the character's own gold, written out.
		return true;
	}

	/**
	 * A character written before a Solo Self-Found character could bank empire upgrade points keeps the points it held
	 * and gets an empty tree of its own. Issue #2741 slice 1c, 2026-11-18. `PrivateEmpireUpgradePoints` became a double,
	 * and a whole number is already a number the double reads, so it is written back unchanged -- 0 where the key is
	 * absent -- for `Migrate_3_to_4`'s reason; and `PrivateEmpireTree` arrived with the tree, so nothing was bought.
	 *
	 * A KEY HOLDING THE WRONG KIND OF VALUE IS REFUSED rather than replaced, as the account record's `Migrate_3_to_4`
	 * refuses: points that are not a number, or a tree that is not an object, would otherwise be thrown away without a
	 * word. A tree object already there is kept.
	 */
	bool Migrate_8_to_9(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		double Banked = 0.0;
		if (Record->HasField(TEXT("PrivateEmpireUpgradePoints"))
			&& !Record->TryGetNumberField(TEXT("PrivateEmpireUpgradePoints"), Banked))
		{
			OutError = TEXT("PrivateEmpireUpgradePoints holds something that is not a number");
			return false;
		}
		if (Record->HasField(TEXT("PrivateEmpireTree")))
		{
			const TSharedPtr<FJsonObject>* Held = nullptr;
			if (!Record->TryGetObjectField(TEXT("PrivateEmpireTree"), Held) || Held == nullptr || !Held->IsValid())
			{
				OutError = TEXT("PrivateEmpireTree holds something that is not an object");
				return false;
			}
		}
		else
		{
			const TSharedRef<FJsonObject> Empty = MakeShared<FJsonObject>();
			Empty->SetArrayField(TEXT("Nodes"), TArray<TSharedPtr<FJsonValue>>());
			Record->SetObjectField(TEXT("PrivateEmpireTree"), Empty); // A migration's: nothing bought, written out.
		}
		Record->SetNumberField(TEXT("PrivateEmpireUpgradePoints"), Banked); // A migration's: the same points, written out.
		return true;
	}

	/**
	 * A character written before skills levelled has earned no skill experience. Issue #2764, 2026-11-19: an empty
	 * `SkillExperience`, so every skill stands at level 1, as it did. A LIST ALREADY THERE IS KEPT, and a key holding
	 * anything but a list is refused rather than replaced, for `Migrate_8_to_9`'s reason: a value thrown away without a
	 * word is a loss nobody sees.
	 */
	bool Migrate_9_to_10(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		if (Record->HasField(TEXT("SkillExperience")))
		{
			const TArray<TSharedPtr<FJsonValue>>* Held = nullptr;
			if (!Record->TryGetArrayField(TEXT("SkillExperience"), Held) || Held == nullptr)
			{
				OutError = TEXT("SkillExperience holds something that is not a list");
				return false;
			}
			return true;
		}
		Record->SetArrayField(TEXT("SkillExperience"), TArray<TSharedPtr<FJsonValue>>()); // A migration's: none earned.
		return true;
	}

	/**
	 * A record written before a debt could be owed owes nothing. Issue #2741 slice 6e, 2026-11-20: `GoldOwed` and
	 * `GoldOwedEachDay` arrived with Global Credit and Sovereign Loans, the only way to owe gold, so the migrated file
	 * states a debt of 0 -- written out, for the character record's `Migrate_3_to_4` reason. A KEY ALREADY HOLDING A
	 * NUMBER IS KEPT, and one holding anything else is refused rather than read as 0, as the gold's own step refuses.
	 */
	bool Migrate_10_to_11(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		for (const TCHAR* Key : { TEXT("GoldOwed"), TEXT("GoldOwedEachDay") })
		{
			double Held = 0.0;
			if (Record->HasField(Key) && !Record->TryGetNumberField(Key, Held))
			{
				OutError = FString::Printf(TEXT("%s holds something that is not a number"), Key);
				return false;
			}
			Record->SetNumberField(Key, Held); // A migration's: the debt, written out.
		}
		return true;
	}

	/**
	 * The chain, newest step last.
	 *
	 * The order is for a reader rather than for the machinery, which looks a
	 * step up by the version it leads out of. Writing it in order anyway means a
	 * reader can see a gap.
	 */
	const FCataclysmSaveMigrationStep CharacterSteps[] = {
		{ 1, TEXT("Migrate_1_to_2"), &Migrate_1_to_2 },
		{ 2, TEXT("Migrate_2_to_3"), &Migrate_2_to_3 },
		{ 3, TEXT("Migrate_3_to_4"), &Migrate_3_to_4 },
		{ 4, TEXT("Migrate_4_to_5"), &Migrate_4_to_5 },
		{ 5, TEXT("Migrate_5_to_6"), &Migrate_5_to_6 },
		{ 6, TEXT("Migrate_6_to_7"), &Migrate_6_to_7 },
		{ 7, TEXT("Migrate_7_to_8"), &Migrate_7_to_8 },
		{ 8, TEXT("Migrate_8_to_9"), &Migrate_8_to_9 },
		{ 9, TEXT("Migrate_9_to_10"), &Migrate_9_to_10 },
		{ 10, TEXT("Migrate_10_to_11"), &Migrate_10_to_11 },
	};
}

TArrayView<const FCataclysmSaveMigrationStep>
UCataclysmCharacterSave::MigrationSteps() const
{
	return CataclysmCharacterSaveMigration::CharacterSteps;
}

int32 UCataclysmCharacterSave::CataclysmOrderSeedFor(const FGuid& Identifier)
{
	// FNV-1a, 32 BITS, ITS TWO CONSTANTS WRITTEN OUT. The header says why this and not `GetTypeHash`; changing either
	// constant, or the byte order below, moves every migrated character's Cataclysms.
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

namespace CataclysmRunSaveMigration
{
	/**
	 * A run written before it carried the corrupted double's hunt holds none: no double could hunt. Issue #2688, ruled
	 * 2026-11-02. Written out, for the character record's `Migrate_3_to_4` reason: a migrated record ought to describe
	 * itself completely. Each half goes into its own object only where the file has one; a version 1 file always has
	 * both, because the record has written `Walk` and `Floor.RuleState` since before saves loaded.
	 */
	bool Migrate_1_to_2(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		const TSharedPtr<FJsonObject>* Walk = nullptr;
		if (Record->TryGetObjectField(TEXT("Walk"), Walk) && Walk && Walk->IsValid())
		{
			(*Walk)->SetBoolField(TEXT("bConsumptionDoubleHasHunted"), false); // A migration's: no hunt, written out.
			(*Walk)->SetNumberField(TEXT("ConsumptionDoubleHealthShare"), 1.0);
			(*Walk)->SetArrayField(TEXT("ConsumptionDoublePotionCharges"), TArray<TSharedPtr<FJsonValue>>());
		}
		const TSharedPtr<FJsonObject>* Floor = nullptr;
		const TSharedPtr<FJsonObject>* RuleState = nullptr;
		if (Record->TryGetObjectField(TEXT("Floor"), Floor) && Floor && Floor->IsValid()
			&& (*Floor)->TryGetObjectField(TEXT("RuleState"), RuleState) && RuleState && RuleState->IsValid())
		{
			(*RuleState)->SetBoolField(TEXT("bConsumptionDoubleHuntsThisFloor"), false); // A migration's: none.
			(*RuleState)->SetBoolField(TEXT("bConsumptionDoubleStanding"), false);
			const TSharedRef<FJsonObject> Origin = MakeShared<FJsonObject>();
			Origin->SetNumberField(TEXT("X"), 0.0);
			Origin->SetNumberField(TEXT("Y"), 0.0);
			Origin->SetNumberField(TEXT("Z"), 0.0);
			(*RuleState)->SetObjectField(TEXT("ConsumptionDoubleLocation"), Origin);
		}
		return true;
	}

	/**
	 * A run written before it kept what the last death cost holds no death: none was kept. Issue #41, ruled by the
	 * coordinating session on 2026-11-02. Written out, for `Migrate_1_to_2`'s reason, as the five fields' defaults: day
	 * 0, which the empire screen reads as no death, no name, no days and no city.
	 */
	bool Migrate_2_to_3(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetNumberField(TEXT("LastDeathDay"), 0); // A migration's: no death, written out.
		Record->SetStringField(TEXT("LastDeathDungeonName"), FString());
		Record->SetNumberField(TEXT("LastDeathWalkDays"), 0);
		Record->SetNumberField(TEXT("LastDeathDaysCost"), 0);
		Record->SetArrayField(TEXT("LastDeathFallenCityIds"), TArray<TSharedPtr<FJsonValue>>());
		return true;
	}

	/**
	 * A run written before it kept its Cataclysm boss dungeon had not opened one: nothing in the game opened one until
	 * issue #1324 slice 6b, 2026-11-02. Written out, for `Migrate_1_to_2`'s reason, as the three fields' defaults: not
	 * opened, day 0 and no floors.
	 */
	bool Migrate_3_to_4(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetBoolField(TEXT("bCataclysmDungeonOpened"), false); // A migration's: no boss opened, written out.
		Record->SetNumberField(TEXT("CataclysmDungeonOpenedDay"), 0);
		Record->SetNumberField(TEXT("CataclysmDungeonBaseFloors"), 0);
		return true;
	}

	/**
	 * A run written before it carried the win of the run before it carries none: no run could be won until issue #1324
	 * slice 6e, 2026-11-03. Written out, for `Migrate_1_to_2`'s reason, as the four fields' defaults: day 0, tier 0,
	 * no Cataclysm and no points, which the empire screen reads as no win line.
	 */
	bool Migrate_4_to_5(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetNumberField(TEXT("LastRunWonDay"), 0); // A migration's: the run before was not won, written out.
		Record->SetNumberField(TEXT("LastRunWonTier"), 0);
		Record->SetStringField(TEXT("LastRunWonCataclysm"), TEXT("None"));
		Record->SetNumberField(TEXT("LastRunWonPassivePoints"), 0);
		return true;
	}

	/**
	 * A run written before a trip between levels was a save and a load had saved no award and no clear: issue #2733
	 * slice 3, 2026-11-17. Written out, for `Migrate_1_to_2`'s reason: no award, 0; no clear, `INDEX_NONE` with an
	 * empty name and no days; and no walk ended, the summary's defaults. The empire screen reads them as no "+x", no
	 * clear line and no summary of days away.
	 */
	bool Migrate_5_to_6(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetNumberField(TEXT("LastEmpirePointsAwarded"), 0.0); // A migration's: no award, written out.
		Record->SetNumberField(TEXT("LastClearDay"), INDEX_NONE);
		Record->SetStringField(TEXT("LastClearDungeonName"), FString());
		Record->SetNumberField(TEXT("LastClearWalkDays"), 0);

		// AND NO WALK HAS ENDED: `FCataclysmDaysAway`'s defaults, every field written out.
		const TSharedRef<FJsonObject> NoneAway = MakeShared<FJsonObject>();
		NoneAway->SetNumberField(TEXT("FromDay"), INDEX_NONE);
		NoneAway->SetNumberField(TEXT("ToDay"), INDEX_NONE);
		NoneAway->SetBoolField(TEXT("bEndedInAClear"), false);
		NoneAway->SetArrayField(TEXT("Cities"), TArray<TSharedPtr<FJsonValue>>());
		NoneAway->SetArrayField(TEXT("Fallen"), TArray<TSharedPtr<FJsonValue>>());
		NoneAway->SetNumberField(TEXT("Surges"), 0);
		NoneAway->SetNumberField(TEXT("SurgeDungeons"), 0);
		Record->SetObjectField(TEXT("LastDaysAway"), NoneAway);
		return true;
	}

	/**
	 * A run written before gold existed has none lying on its floor: no kill could drop any. Issue #2742, 2026-11-18.
	 * Written out, for `Migrate_1_to_2`'s reason, as a `Gold` of 0 on every item the floor's `GroundItems` holds. A
	 * floor with no array, or an entry that is not an object, is left as it is: the read that follows refuses a
	 * malformed record, and this step is not the place to decide what it meant.
	 */
	bool Migrate_6_to_7(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		const TSharedPtr<FJsonObject>* Floor = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* GroundItems = nullptr;
		if (Record->TryGetObjectField(TEXT("Floor"), Floor) && Floor && Floor->IsValid()
			&& (*Floor)->TryGetArrayField(TEXT("GroundItems"), GroundItems) && GroundItems)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *GroundItems)
			{
				const TSharedPtr<FJsonObject>* Item = nullptr;
				if (Entry.IsValid() && Entry->TryGetObject(Item) && Item && Item->IsValid())
				{
					(*Item)->SetNumberField(TEXT("Gold"), 0); // A migration's: no gold lay there, written out.
				}
			}
		}
		return true;
	}

	/**
	 * A run written before each run had its own mask on its dungeons' walk seeds is given the mask 0. Issue #2749,
	 * B10, ruled by the coordinating session on 2026-11-18: "the migration should write the value that reproduces
	 * today's WalkSeed exactly, so a walk saved mid-dungeon loads the same floors it was on". `FCataclysmDungeon::
	 * WalkSeed` XORs the mask with a dungeon's identifier plus one, and XOR with 0 changes nothing, so every dungeon of
	 * such a run keeps the seed it had at version 7: its floors, the Edicts an Ascendant draws on it, and its Corrupted
	 * Stalker roll. Written out, for `Migrate_3_to_4`'s reason, and written whatever the key held: a run written at 7
	 * never wrote it.
	 */
	bool Migrate_7_to_8(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetNumberField(TEXT("WalkSeedMask"), 0); // A migration's: the mask that keeps version 7's seeds.
		return true;
	}

	/**
	 * A run written before the empire upgrade tree's event rules counted anything has counted nothing, and its city
	 * upgrades are held since its run began. Issue #2741 slice 3b, ruled by the coordinating session on 2026-11-19.
	 * Written out, for `Migrate_3_to_4`'s reason: no clear since the last surge, no city reclaimed, the Last Stand not
	 * spent, and a `BoughtDay` of 0 on every upgrade of every city, so Enduring Foundations counts each as held from day
	 * 0. A `Cities` that is not an array, or an entry or upgrade that is not an object, is left as it is, for
	 * `Migrate_6_to_7`'s reason.
	 */
	bool Migrate_8_to_9(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetNumberField(TEXT("ClearsSinceLastSurge"), 0); // A migration's: nothing counted.
		Record->SetNumberField(TEXT("CitiesReclaimed"), 0); // A migration's: nothing counted.
		Record->SetBoolField(TEXT("bSanctuaryLastStandSpent"), false); // A migration's: not spent.
		const TArray<TSharedPtr<FJsonValue>>* Cities = nullptr;
		if (Record->TryGetArrayField(TEXT("Cities"), Cities) && Cities)
		{
			for (const TSharedPtr<FJsonValue>& CityEntry : *Cities)
			{
				const TSharedPtr<FJsonObject>* City = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Upgrades = nullptr;
				if (!CityEntry.IsValid() || !CityEntry->TryGetObject(City) || !City || !City->IsValid()
					|| !(*City)->TryGetArrayField(TEXT("Upgrades"), Upgrades) || !Upgrades)
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& UpgradeEntry : *Upgrades)
				{
					const TSharedPtr<FJsonObject>* Upgrade = nullptr;
					if (UpgradeEntry.IsValid() && UpgradeEntry->TryGetObject(Upgrade) && Upgrade && Upgrade->IsValid())
					{
						(*Upgrade)->SetNumberField(TEXT("BoughtDay"), 0); // A migration's: held since the run began.
					}
				}
			}
		}
		return true;
	}

	/**
	 * A run written before a saved creature said whether it is a Vengeful Wraith says of none of its creatures that it
	 * is one. Issue #2725, approved by the coordinating session on 2026-11-19. Written out, for `Migrate_1_to_2`'s
	 * reason, as a `bIsVengefulWraith` of false on every creature the floor's `Creatures` holds: nothing in the older
	 * file tells a wraith from another creature a rule raised, so such a wraith comes back as one did before the field.
	 * A floor with no array, or an entry that is not an object, is left as it is, for `Migrate_6_to_7`'s reason.
	 */
	bool Migrate_9_to_10(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		const TSharedPtr<FJsonObject>* Floor = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Creatures = nullptr;
		if (Record->TryGetObjectField(TEXT("Floor"), Floor) && Floor && Floor->IsValid()
			&& (*Floor)->TryGetArrayField(TEXT("Creatures"), Creatures) && Creatures)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Creatures)
			{
				const TSharedPtr<FJsonObject>* Creature = nullptr;
				if (Entry.IsValid() && Entry->TryGetObject(Creature) && Creature && Creature->IsValid())
				{
					(*Creature)->SetBoolField(TEXT("bIsVengefulWraith"), false); // A migration's: no wraith, written out.
				}
			}
		}
		return true;
	}

	/**
	 * A run written before it counted the dungeons cleared since the last death gets the count it can prove. Issue #2741
	 * slice 5b, the coordinating session's D5 of 2026-11-19: with no death recorded -- an empty or missing
	 * `LastDeathDungeonName`, which a new run empties -- every clear of the run was made without dying, so the count is
	 * the run's `DungeonsCleared`; with one recorded, the clears after it cannot be told from those before, so it is 0.
	 * Written out, for `Migrate_1_to_2`'s reason. A `DungeonsCleared` that is not a number is read as 0, for
	 * `Migrate_6_to_7`'s reason: the read that follows refuses a malformed record.
	 */
	bool Migrate_10_to_11(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		FString DeathDungeon;
		const bool bDiedThisRun = Record->TryGetStringField(TEXT("LastDeathDungeonName"), DeathDungeon)
			&& !DeathDungeon.IsEmpty();
		double Cleared = 0.0;
		if (bDiedThisRun || !Record->TryGetNumberField(TEXT("DungeonsCleared"), Cleared))
		{
			Cleared = 0.0;
		}
		Record->SetNumberField(TEXT("ClearsSinceLastDeath"), Cleared); // A migration's: the clears it can prove.
		return true;
	}

	/**
	 * Version 11 to 12, issue #2741 slice 6b: no city marked as damaged, the run's population not fallen below half, and
	 * no gold counted in the walk. THE RECORD CANNOT PROVE A CITY'S PAST DAMAGE (B1): a city below its saved maximum may
	 * be one whose maximum the tree raised (S1), so every city starts unmarked and the next bite marks it. The run's
	 * flag compares against an effective maximum that depends on the tree, which the record does not hold, so the next
	 * day's check sets it (B2). A walk resumed from the record counts its gold from the load (B3).
	 */
	bool Migrate_11_to_12(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		Record->SetBoolField(TEXT("bPopulationFellBelowHalf"), false); // A migration's: not proven.
		const TArray<TSharedPtr<FJsonValue>>* Cities = nullptr;
		if (Record->TryGetArrayField(TEXT("Cities"), Cities) && Cities)
		{
			for (const TSharedPtr<FJsonValue>& CityEntry : *Cities)
			{
				const TSharedPtr<FJsonObject>* City = nullptr;
				if (CityEntry.IsValid() && CityEntry->TryGetObject(City) && City && City->IsValid())
				{
					(*City)->SetBoolField(TEXT("bTookSurgeDamage"), false); // A migration's: not proven.
				}
			}
		}
		const TSharedPtr<FJsonObject>* Walk = nullptr;
		if (Record->TryGetObjectField(TEXT("Walk"), Walk) && Walk && Walk->IsValid())
		{
			(*Walk)->SetNumberField(TEXT("GoldDroppedThisWalk"), 0); // A migration's: counted from the load.
			(*Walk)->SetNumberField(TEXT("GoldPickedUpThisWalk"), 0);
		}
		return true;
	}

	/** The chain, newest step last. */
	const FCataclysmSaveMigrationStep RunSteps[] = {
		{ 1, TEXT("Migrate_1_to_2"), &Migrate_1_to_2 },
		{ 2, TEXT("Migrate_2_to_3"), &Migrate_2_to_3 },
		{ 3, TEXT("Migrate_3_to_4"), &Migrate_3_to_4 },
		{ 4, TEXT("Migrate_4_to_5"), &Migrate_4_to_5 },
		{ 5, TEXT("Migrate_5_to_6"), &Migrate_5_to_6 },
		{ 6, TEXT("Migrate_6_to_7"), &Migrate_6_to_7 },
		{ 7, TEXT("Migrate_7_to_8"), &Migrate_7_to_8 },
		{ 8, TEXT("Migrate_8_to_9"), &Migrate_8_to_9 },
		{ 9, TEXT("Migrate_9_to_10"), &Migrate_9_to_10 },
		{ 10, TEXT("Migrate_10_to_11"), &Migrate_10_to_11 },
		{ 11, TEXT("Migrate_11_to_12"), &Migrate_11_to_12 },
	};
}

TArrayView<const FCataclysmSaveMigrationStep>
UCataclysmRunSave::MigrationSteps() const
{
	return CataclysmRunSaveMigration::RunSteps;
}

namespace CataclysmAccountSaveMigration
{
	/**
	 * An account written when the banked points were a whole number keeps the same number of points. Issue #52,
	 * 2026-11-03: `EmpireUpgradePoints` became a double so that a fraction of a point is banked rather than rounded
	 * away. A whole number is already a number the double reads, so the value is written back unchanged; written out,
	 * for the character record's `Migrate_3_to_4` reason, so the migrated file states its bank. A version 1 file
	 * without the key banked nothing, and gets 0.
	 *
	 * A KEY HOLDING SOMETHING THAT IS NOT A NUMBER IS REFUSED rather than read as 0: that would empty a bank somebody
	 * edited by hand without saying so.
	 */
	bool Migrate_1_to_2(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		double Banked = 0.0;
		if (Record->HasField(TEXT("EmpireUpgradePoints"))
			&& !Record->TryGetNumberField(TEXT("EmpireUpgradePoints"), Banked))
		{
			OutError = TEXT("EmpireUpgradePoints holds something that is not a number");
			return false;
		}
		Record->SetNumberField(TEXT("EmpireUpgradePoints"), Banked); // A migration's: the same points, written out.
		return true;
	}

	/**
	 * An account written before gold existed holds none. Issue #2742, 2026-11-18: `Gold` arrived with gold drops, so
	 * no account written at 2 can have held any, and the migrated file states its balance of 0 -- written out, for
	 * the character record's `Migrate_3_to_4` reason.
	 *
	 * A KEY ALREADY HOLDING A NUMBER IS KEPT, and one holding anything else is refused rather than read as 0, as
	 * `Migrate_1_to_2` refuses: that would empty a balance somebody edited by hand without saying so.
	 */
	bool Migrate_2_to_3(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		double Held = 0.0;
		if (Record->HasField(TEXT("Gold")) && !Record->TryGetNumberField(TEXT("Gold"), Held))
		{
			OutError = TEXT("Gold holds something that is not a number");
			return false;
		}
		Record->SetNumberField(TEXT("Gold"), Held); // A migration's: the balance, written out.
		return true;
	}

	/**
	 * An account written before the empire upgrade tree could be bought holds an empty tree. Issue #2741 slice 1b,
	 * 2026-11-18: `EmpireTree` arrived with the rules for buying it, so no account written at 3 can have bought a node,
	 * and the migrated file states its empty tree -- written out, for the character record's `Migrate_3_to_4` reason.
	 *
	 * AN OBJECT ALREADY THERE IS KEPT, and anything else under the key is refused rather than replaced by an empty tree,
	 * as `Migrate_2_to_3` refuses a balance that is not a number: that would throw away a tree somebody edited by hand
	 * without saying so. What the object holds is the record's read to judge, not this step's.
	 */
	bool Migrate_3_to_4(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		if (Record->HasField(TEXT("EmpireTree")))
		{
			const TSharedPtr<FJsonObject>* Held = nullptr;
			if (!Record->TryGetObjectField(TEXT("EmpireTree"), Held) || Held == nullptr || !Held->IsValid())
			{
				OutError = TEXT("EmpireTree holds something that is not an object");
				return false;
			}
			return true;
		}

		const TSharedRef<FJsonObject> Empty = MakeShared<FJsonObject>();
		Empty->SetArrayField(TEXT("Nodes"), TArray<TSharedPtr<FJsonValue>>());
		Record->SetObjectField(TEXT("EmpireTree"), Empty); // A migration's: nothing bought, written out.
		return true;
	}

	/**
	 * A record written before a debt could be owed owes nothing. Issue #2741 slice 6e, 2026-11-20: `GoldOwed` and
	 * `GoldOwedEachDay` arrived with Global Credit and Sovereign Loans, the only way to owe gold, so the migrated file
	 * states a debt of 0 -- written out, for the character record's `Migrate_3_to_4` reason. A KEY ALREADY HOLDING A
	 * NUMBER IS KEPT, and one holding anything else is refused rather than read as 0, as the gold's own step refuses.
	 */
	bool Migrate_4_to_5(const TSharedRef<FJsonObject>& Record, FString& OutError)
	{
		OutError.Empty();
		for (const TCHAR* Key : { TEXT("GoldOwed"), TEXT("GoldOwedEachDay") })
		{
			double Held = 0.0;
			if (Record->HasField(Key) && !Record->TryGetNumberField(Key, Held))
			{
				OutError = FString::Printf(TEXT("%s holds something that is not a number"), Key);
				return false;
			}
			Record->SetNumberField(Key, Held); // A migration's: the debt, written out.
		}
		return true;
	}

	/** The chain, newest step last. */
	const FCataclysmSaveMigrationStep AccountSteps[] = {
		{ 1, TEXT("Migrate_1_to_2"), &Migrate_1_to_2 },
		{ 2, TEXT("Migrate_2_to_3"), &Migrate_2_to_3 },
		{ 3, TEXT("Migrate_3_to_4"), &Migrate_3_to_4 },
		{ 4, TEXT("Migrate_4_to_5"), &Migrate_4_to_5 },
	};
}

TArrayView<const FCataclysmSaveMigrationStep>
UCataclysmAccountSave::MigrationSteps() const
{
	return CataclysmAccountSaveMigration::AccountSteps;
}

TArray<TSubclassOf<UCataclysmSaveRecord>> CataclysmSaveRecordClasses()
{
	return {
		UCataclysmAccountSave::StaticClass(),
		UCataclysmCharacterSave::StaticClass(),
		UCataclysmRunSave::StaticClass(),
	};
}
