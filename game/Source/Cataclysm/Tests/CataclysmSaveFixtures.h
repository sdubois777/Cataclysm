// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

	/** Text, parsed. Null when it is not a JSON object. */
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
