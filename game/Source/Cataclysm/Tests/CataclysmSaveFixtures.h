// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Save/CataclysmSaveRecords.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/**
 * Reading the committed example save files, and comparing two records field by
 * field.
 *
 * WHY THE FILES ARE COMMITTED RATHER THAN WRITTEN BY THE TEST.
 * `docs/Save_System_Design.md` section 5: "a test that writes a save with the
 * current code and reads it back proves only that the code agrees with itself".
 * The files in `game/Tests/SaveFixtures/` were written by hand and are never
 * edited again, so they stay a record of what an older build actually produced.
 */
namespace CataclysmSaveFixtures
{
	/** Where the committed example save files live. */
	inline FString Directory()
	{
		return FPaths::ProjectDir() / TEXT("Tests") / TEXT("SaveFixtures");
	}

	/** One fixture's text. False, with a reason, when it is not there. */
	inline bool Read(const TCHAR* FileName, FString& OutText, FString& OutReason)
	{
		const FString Path = Directory() / FileName;
		if (!FFileHelper::LoadFileToString(OutText, *Path))
		{
			OutReason = FString::Printf(TEXT("could not read the fixture at %s"), *Path);
			return false;
		}
		return true;
	}

	/** One fixture, parsed. Null when it is missing or is not a JSON object. */
	inline TSharedPtr<FJsonObject> ReadParsed(const TCHAR* FileName, FString& OutReason)
	{
		FString Text;
		if (!Read(FileName, Text, OutReason))
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
		{
			OutReason = FString::Printf(TEXT("the fixture %s is not a JSON object"), FileName);
			return nullptr;
		}
		return Parsed;
	}

	/**
	 * The Cataclysm order seed the committed character files' identifier, `5C0B1A4E5B7F4D2E9A3C6F81D24E7B03`, gives:
	 * what `Migrate_6_to_7` writes into `Character_v1.json` to `Character_v6.json` on their way to version 7. Issue #1324
	 * slice 6a. A NUMBER WORKED OUT APART FROM THE STEP, in Python, by
	 * `tools/tests/test_a_character_keeps_its_cataclysm_draw.py`, which checks it is still what the identifier gives; so
	 * a test comparing a migrated record with it compares two things made apart, as `EmptyPlayerBuild` below does.
	 */
	inline constexpr int32 FixtureIdentifiersOrderSeed = 1412640493;

	/** The seed `Character_v7.json` to `Character_v10.json` state: not the one their identifier gives, so a seed
	 *  re-derived on load cannot pass. */
	inline constexpr int32 CommittedCharacterOrderSeed = 975318642;

	/**
	 * An empty `FCataclysmPlayerBuild` as the save writes one: what `Migrate_5_to_6` writes into a character record and
	 * what a record holding no build writes back. Issue #2688. Built from the struct's defaults here, independently of
	 * the step, so a test comparing a migrated record with it compares two things made apart.
	 */
	inline TSharedRef<FJsonObject> EmptyPlayerBuild()
	{
		const FCataclysmPlayerBuild None;
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(FCataclysmPlayerBuild::StaticStruct(), &None, Out, CPF_SaveGame, 0,
												  nullptr, EJsonObjectConversionFlags::SkipStandardizeCase);
		return Out;
	}

	/** Text, parsed. Null when it is not a JSON object. */
	/**
	 * What `Migrate_5_to_6` writes into a run file written before version 6, and so what the run record writes back
	 * for one: no award, no clear and no walk ended. Issue #2733 slice 3, 2026-11-17. ONE COPY, for every test that
	 * runs an older run file through the whole chain and compares what comes back with what it expects.
	 */
	inline void AddTheRunsVersion6Defaults(FJsonObject& Expected)
	{
		Expected.SetNumberField(TEXT("LastEmpirePointsAwarded"), 0.0);
		Expected.SetNumberField(TEXT("LastClearDay"), INDEX_NONE);
		Expected.SetStringField(TEXT("LastClearDungeonName"), FString());
		Expected.SetNumberField(TEXT("LastClearWalkDays"), 0);
		const TSharedRef<FJsonObject> NoneAway = MakeShared<FJsonObject>();
		NoneAway->SetNumberField(TEXT("FromDay"), INDEX_NONE);
		NoneAway->SetNumberField(TEXT("ToDay"), INDEX_NONE);
		NoneAway->SetBoolField(TEXT("bEndedInAClear"), false);
		NoneAway->SetArrayField(TEXT("Cities"), TArray<TSharedPtr<FJsonValue>>());
		NoneAway->SetArrayField(TEXT("Fallen"), TArray<TSharedPtr<FJsonValue>>());
		NoneAway->SetNumberField(TEXT("Surges"), 0);
		NoneAway->SetNumberField(TEXT("SurgeDungeons"), 0);
		Expected.SetObjectField(TEXT("LastDaysAway"), NoneAway);
	}

	/**
	 * What `Migrate_6_to_7` writes into a run file written before version 7, and so what the run record writes back
	 * for one: a `Gold` of 0 on every item lying on its floor. Issue #2742, 2026-11-18. ONE COPY, beside the version 6
	 * one and for the same callers. A file whose floor has no ground items is left as it is.
	 */
	inline void AddTheRunsVersion7Defaults(FJsonObject& Expected)
	{
		const TSharedPtr<FJsonObject>* Floor = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* GroundItems = nullptr;
		if (!Expected.TryGetObjectField(TEXT("Floor"), Floor) || Floor == nullptr || !Floor->IsValid()
			|| !(*Floor)->TryGetArrayField(TEXT("GroundItems"), GroundItems) || GroundItems == nullptr)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *GroundItems)
		{
			const TSharedPtr<FJsonObject>* Item = nullptr;
			if (Entry.IsValid() && Entry->TryGetObject(Item) && Item != nullptr && Item->IsValid())
			{
				(*Item)->SetNumberField(TEXT("Gold"), 0);
			}
		}
	}

	/**
	 * What `Migrate_7_to_8` writes into a run file written before version 8, and so what the run record writes back
	 * for one: a mask of 0 on its dungeons' walk seeds, the mask that leaves every dungeon the seed it had. Issue #2749
	 * (B10), 2026-11-18. ONE COPY, beside the version 7 one and for the same callers.
	 */
	inline void AddTheRunsVersion8Defaults(FJsonObject& Expected)
	{
		Expected.SetNumberField(TEXT("WalkSeedMask"), 0);
	}

	/**
	 * What `Migrate_8_to_9` writes into a run file written before version 9, and so what the run record writes back
	 * for one: the empire upgrade tree's event counts at nothing -- no clear since the last surge, no city reclaimed,
	 * the Last Stand not spent -- and a `BoughtDay` of 0 on every upgrade of every city. Issue #2741 slice 3b,
	 * 2026-11-19. ONE COPY, beside the version 8 one and for the same callers.
	 */
	inline void AddTheRunsVersion9Defaults(FJsonObject& Expected)
	{
		Expected.SetNumberField(TEXT("ClearsSinceLastSurge"), 0);
		Expected.SetNumberField(TEXT("CitiesReclaimed"), 0);
		Expected.SetBoolField(TEXT("bSanctuaryLastStandSpent"), false);
		const TArray<TSharedPtr<FJsonValue>>* Cities = nullptr;
		if (!Expected.TryGetArrayField(TEXT("Cities"), Cities) || Cities == nullptr)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& CityEntry : *Cities)
		{
			const TSharedPtr<FJsonObject>* City = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Upgrades = nullptr;
			if (!CityEntry.IsValid() || !CityEntry->TryGetObject(City) || City == nullptr || !City->IsValid()
				|| !(*City)->TryGetArrayField(TEXT("Upgrades"), Upgrades) || Upgrades == nullptr)
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& UpgradeEntry : *Upgrades)
			{
				const TSharedPtr<FJsonObject>* Upgrade = nullptr;
				if (UpgradeEntry.IsValid() && UpgradeEntry->TryGetObject(Upgrade) && Upgrade != nullptr
					&& Upgrade->IsValid())
				{
					(*Upgrade)->SetNumberField(TEXT("BoughtDay"), 0);
				}
			}
		}
	}

	/**
	 * What `Migrate_3_to_4` writes into an account file written before version 4, and so what the account record
	 * writes back for one: an empty empire upgrade tree, `"EmpireTree": { "Nodes": [] }`. Issue #2741 slice 1b,
	 * 2026-11-18. ONE COPY, for every test that runs an older account file through the whole chain.
	 */
	inline void AddTheAccountsVersion4Defaults(FJsonObject& Expected)
	{
		const TSharedRef<FJsonObject> Empty = MakeShared<FJsonObject>();
		Empty->SetArrayField(TEXT("Nodes"), TArray<TSharedPtr<FJsonValue>>());
		Expected.SetObjectField(TEXT("EmpireTree"), Empty);
	}

	/**
	 * What `Migrate_8_to_9` writes into a character file written before version 9, and so what the character record
	 * writes back for one: its private empire upgrade points as the same number (0 where the key is absent) and an empty
	 * tree of its own, `"PrivateEmpireTree": { "Nodes": [] }`. Issue #2741 slice 1c, 2026-11-18. ONE COPY, for every test
	 * that runs an older character file through the whole chain.
	 */
	inline void AddTheCharactersVersion9Defaults(FJsonObject& Expected)
	{
		double Banked = 0.0;
		Expected.TryGetNumberField(TEXT("PrivateEmpireUpgradePoints"), Banked);
		Expected.SetNumberField(TEXT("PrivateEmpireUpgradePoints"), Banked);
		const TSharedRef<FJsonObject> Empty = MakeShared<FJsonObject>();
		Empty->SetArrayField(TEXT("Nodes"), TArray<TSharedPtr<FJsonValue>>());
		Expected.SetObjectField(TEXT("PrivateEmpireTree"), Empty);
	}

	/**
	 * What `Migrate_9_to_10` writes into a character file written before version 10, and so what the character record
	 * writes back for one: no skill experience, `"SkillExperience": []`. Issue #2764, 2026-11-19. ONE COPY, for every
	 * test that runs an older character file through the whole chain.
	 */
	inline void AddTheCharactersVersion10Defaults(FJsonObject& Expected)
	{
		Expected.SetArrayField(TEXT("SkillExperience"), TArray<TSharedPtr<FJsonValue>>());
	}

	inline TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Parsed))
		{
			return nullptr;
		}
		return Parsed;
	}

	/**
	 * Whether two parsed records hold the same thing, ignoring the order keys
	 * were written in.
	 *
	 * `OutWhere` NAMES THE PATH TO THE DIFFERENCE, for example
	 * `CarriedSlots[0].Item.Sockets`, because a failure saying only "they differ"
	 * on a record with a hundred fields in it is a failure you have to reproduce
	 * by hand before you can act on it.
	 *
	 * NUMBERS ARE COMPARED EXACTLY AND NOT WITHIN A TOLERANCE. Every number in
	 * the fixtures is exactly representable in binary on purpose, so a difference
	 * here is a real difference and not a rounding one. `game/Tests/SaveFixtures/README.md`
	 * says so where somebody editing a fixture will see it.
	 */
	inline bool Same(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right,
			  const FString& Path, FString& OutWhere);

	inline bool SameObject(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right,
						   FString& OutWhere)
	{
		return Same(MakeShared<FJsonValueObject>(Left), MakeShared<FJsonValueObject>(Right),
					FString(), OutWhere);
	}

	inline bool Same(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right,
					 const FString& Path, FString& OutWhere)
	{
		const FString Here = Path.IsEmpty() ? TEXT("the record") : Path;

		if (!Left.IsValid() || !Right.IsValid())
		{
			if (Left.IsValid() != Right.IsValid())
			{
				OutWhere = FString::Printf(TEXT("%s is present on one side and not the other"), *Here);
				return false;
			}
			return true;
		}

		if (Left->Type != Right->Type)
		{
			OutWhere = FString::Printf(TEXT("%s is a different kind of value on each side"), *Here);
			return false;
		}

		switch (Left->Type)
		{
			case EJson::Object:
			{
				const TSharedPtr<FJsonObject> LeftObject = Left->AsObject();
				const TSharedPtr<FJsonObject> RightObject = Right->AsObject();

				for (const auto& Pair : LeftObject->Values)
				{
					const TSharedPtr<FJsonValue>* Other = RightObject->Values.Find(Pair.Key);
					if (Other == nullptr)
					{
						OutWhere = FString::Printf(TEXT("%s.%s is on the first side only"),
							*Here, *Pair.Key);
						return false;
					}
					if (!Same(Pair.Value, *Other, FString::Printf(TEXT("%s.%s"), *Here, *Pair.Key), OutWhere))
					{
						return false;
					}
				}

				for (const auto& Pair : RightObject->Values)
				{
					if (!LeftObject->Values.Contains(Pair.Key))
					{
						OutWhere = FString::Printf(TEXT("%s.%s is on the second side only"),
							*Here, *Pair.Key);
						return false;
					}
				}
				return true;
			}

			case EJson::Array:
			{
				const TArray<TSharedPtr<FJsonValue>>& LeftArray = Left->AsArray();
				const TArray<TSharedPtr<FJsonValue>>& RightArray = Right->AsArray();
				if (LeftArray.Num() != RightArray.Num())
				{
					OutWhere = FString::Printf(TEXT("%s holds %d entries on one side and %d on the other"),
						*Here, LeftArray.Num(), RightArray.Num());
					return false;
				}
				for (int32 Index = 0; Index < LeftArray.Num(); ++Index)
				{
					if (!Same(LeftArray[Index], RightArray[Index],
							  FString::Printf(TEXT("%s[%d]"), *Here, Index), OutWhere))
					{
						return false;
					}
				}
				return true;
			}

			case EJson::Number:
				if (Left->AsNumber() != Right->AsNumber())
				{
					OutWhere = FString::Printf(TEXT("%s is %f on one side and %f on the other"),
						*Here, Left->AsNumber(), Right->AsNumber());
					return false;
				}
				return true;

			case EJson::String:
				if (!Left->AsString().Equals(Right->AsString(), ESearchCase::CaseSensitive))
				{
					OutWhere = FString::Printf(TEXT("%s is '%s' on one side and '%s' on the other"),
						*Here, *Left->AsString(), *Right->AsString());
					return false;
				}
				return true;

			case EJson::Boolean:
				if (Left->AsBool() != Right->AsBool())
				{
					OutWhere = FString::Printf(TEXT("%s is %s on one side and %s on the other"),
						*Here, Left->AsBool() ? TEXT("true") : TEXT("false"),
						Right->AsBool() ? TEXT("true") : TEXT("false"));
					return false;
				}
				return true;

			default:
				return true;
		}
	}
}
