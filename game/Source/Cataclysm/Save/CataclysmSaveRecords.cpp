// Copyright Stephen Dubois. All Rights Reserved.

#include "Save/CataclysmSaveRecords.h"

#include "Cataclysm.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

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
	 * The chain, newest step last.
	 *
	 * The order is for a reader rather than for the machinery, which looks a
	 * step up by the version it leads out of. Writing it in order anyway means a
	 * reader can see a gap.
	 */
	const FCataclysmSaveMigrationStep CharacterSteps[] = {
		{ 1, TEXT("Migrate_1_to_2"), &Migrate_1_to_2 },
		{ 2, TEXT("Migrate_2_to_3"), &Migrate_2_to_3 },
	};
}

TArrayView<const FCataclysmSaveMigrationStep>
UCataclysmCharacterSave::MigrationSteps() const
{
	return CataclysmCharacterSaveMigration::CharacterSteps;
}

TArray<TSubclassOf<UCataclysmSaveRecord>> CataclysmSaveRecordClasses()
{
	return {
		UCataclysmAccountSave::StaticClass(),
		UCataclysmCharacterSave::StaticClass(),
		UCataclysmRunSave::StaticClass(),
	};
}
