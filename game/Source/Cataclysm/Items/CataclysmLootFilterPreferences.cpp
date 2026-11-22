// Copyright Stephen Dubois. All Rights Reserved.

#include "Items/CataclysmLootFilterPreferences.h"

#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

const TCHAR* UCataclysmLootFilterPreferences::BuiltInRefusal =
	TEXT("The built-in filters cannot be changed. Copy one to make a filter of your own.");
const TCHAR* UCataclysmLootFilterPreferences::NameTakenRefusal = TEXT("Another filter already has that name.");
const TCHAR* UCataclysmLootFilterPreferences::NameEmptyRefusal = TEXT("A filter needs a name.");
const TCHAR* UCataclysmLootFilterPreferences::NameTooLongRefusal = TEXT("That name is longer than 40 characters.");
const TCHAR* UCataclysmLootFilterPreferences::TooManyFiltersRefusal =
	TEXT("You already have 10 filters of your own. Delete one first.");
const TCHAR* UCataclysmLootFilterPreferences::TooManyRulesRefusal = TEXT("A filter holds at most 75 rules.");
const TCHAR* UCataclysmLootFilterPreferences::NoSuchFilterRefusal = TEXT("There is no filter of that name.");
const TCHAR* UCataclysmLootFilterPreferences::NoSuchRuleRefusal = TEXT("There is no such rule.");

namespace
{
	/** The player's filter of this name, ignoring case, or null. Never a built-in. */
	FCataclysmLootFilter* OwnFilter(FCataclysmLootFilterPreferences& Preferences, const FString& Name)
	{
		const FString Wanted = Name.TrimStartAndEnd();
		return Preferences.Filters.FindByPredicate([&Wanted](const FCataclysmLootFilter& Filter)
		{
			return Filter.Name.Equals(Wanted, ESearchCase::IgnoreCase);
		});
	}

	/** Whether any filter, built-in or the player's, has this name, ignoring case. */
	bool NameTaken(const FCataclysmLootFilterPreferences& Preferences, const FString& Name)
	{
		return UCataclysmLootFilterPreferences::IsBuiltInName(Name)
			|| OwnFilter(const_cast<FCataclysmLootFilterPreferences&>(Preferences), Name) != nullptr;
	}

	/** "My filter N", the smallest N whose name no filter has. */
	FString FreeName(const FCataclysmLootFilterPreferences& Preferences)
	{
		for (int32 Number = 1;; ++Number)
		{
			const FString Name = FString::Printf(TEXT("My filter %d"), Number);
			if (!NameTaken(Preferences, Name))
			{
				return Name;
			}
		}
	}

	/**
	 * The player's filter a change may be made to: refuses a built-in first, then a name that names no filter of the
	 * player's. The single place every rule-changing function asks, so none can reach a built-in.
	 */
	FCataclysmLootFilter* EditableFilter(FCataclysmLootFilterPreferences& Preferences, const FString& Name,
										 FString& OutRefusal)
	{
		if (UCataclysmLootFilterPreferences::IsBuiltInName(Name))
		{
			OutRefusal = UCataclysmLootFilterPreferences::BuiltInRefusal;
			return nullptr;
		}
		FCataclysmLootFilter* Filter = OwnFilter(Preferences, Name);
		if (Filter == nullptr)
		{
			OutRefusal = UCataclysmLootFilterPreferences::NoSuchFilterRefusal;
		}
		return Filter;
	}
}

TArray<FString> UCataclysmLootFilterPreferences::BuiltInNames()
{
	return {UCataclysmLootFilter::Everything().Name, UCataclysmLootFilter::HideWorseThanWorn().Name,
			UCataclysmLootFilter::Strict().Name};
}

bool UCataclysmLootFilterPreferences::IsBuiltInName(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd();
	for (const FString& BuiltIn : BuiltInNames())
	{
		if (BuiltIn.Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

TArray<FString> UCataclysmLootFilterPreferences::AllNames(const FCataclysmLootFilterPreferences& Preferences)
{
	TArray<FString> Names = BuiltInNames();
	for (const FCataclysmLootFilter& Filter : Preferences.Filters)
	{
		Names.Add(Filter.Name);
	}
	return Names;
}

bool UCataclysmLootFilterPreferences::Find(const FCataclysmLootFilterPreferences& Preferences, const FString& Name,
										   FCataclysmLootFilter& OutFilter)
{
	// BUILT-INS FIRST, so even a player's filter that somehow held a built-in's name could not stand in for it.
	if (UCataclysmLootFilter::BuiltInNamed(Name.TrimStartAndEnd(), OutFilter))
	{
		return true;
	}
	if (const FCataclysmLootFilter* Own = OwnFilter(const_cast<FCataclysmLootFilterPreferences&>(Preferences), Name))
	{
		OutFilter = *Own;
		return true;
	}
	return false;
}

FCataclysmLootFilter UCataclysmLootFilterPreferences::ActiveFilter(const FCataclysmLootFilterPreferences& Preferences)
{
	FCataclysmLootFilter Filter;
	return Find(Preferences, Preferences.Active, Filter) ? Filter : UCataclysmLootFilter::HideWorseThanWorn();
}

bool UCataclysmLootFilterPreferences::SetActive(FCataclysmLootFilterPreferences& Preferences, const FString& Name,
												FString& OutRefusal)
{
	FCataclysmLootFilter Filter;
	if (!Find(Preferences, Name, Filter))
	{
		OutRefusal = NoSuchFilterRefusal;
		return false;
	}
	Preferences.Active = Filter.Name;
	return true;
}

bool UCataclysmLootFilterPreferences::NewFilter(FCataclysmLootFilterPreferences& Preferences, FString& OutName,
												FString& OutRefusal)
{
	if (Preferences.Filters.Num() >= MaxSavedFilters)
	{
		OutRefusal = TooManyFiltersRefusal;
		return false;
	}
	FCataclysmLootFilter Filter;
	Filter.Name = FreeName(Preferences);
	OutName = Filter.Name;
	Preferences.Filters.Add(Filter);
	return true;
}

bool UCataclysmLootFilterPreferences::CopyFilter(FCataclysmLootFilterPreferences& Preferences, const FString& From,
												 FString& OutName, FString& OutRefusal)
{
	FCataclysmLootFilter Source;
	if (!Find(Preferences, From, Source))
	{
		OutRefusal = NoSuchFilterRefusal;
		return false;
	}
	if (Preferences.Filters.Num() >= MaxSavedFilters)
	{
		OutRefusal = TooManyFiltersRefusal;
		return false;
	}
	Source.Name = FreeName(Preferences);
	OutName = Source.Name;
	Preferences.Filters.Add(Source);
	return true;
}

bool UCataclysmLootFilterPreferences::RenameFilter(FCataclysmLootFilterPreferences& Preferences, const FString& From,
												   const FString& To, FString& OutRefusal)
{
	FCataclysmLootFilter* Filter = EditableFilter(Preferences, From, OutRefusal);
	if (Filter == nullptr)
	{
		return false;
	}
	const FString Name = To.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		OutRefusal = NameEmptyRefusal;
		return false;
	}
	if (Name.Len() > MaxNameLength)
	{
		OutRefusal = NameTooLongRefusal;
		return false;
	}
	// TAKEN BY A BUILT-IN, IN ANY CASE, OR BY ANOTHER OF THE PLAYER'S. The filter's own name in another case is allowed.
	if (IsBuiltInName(Name)
		|| (!Name.Equals(Filter->Name, ESearchCase::IgnoreCase) && OwnFilter(Preferences, Name) != nullptr))
	{
		OutRefusal = NameTakenRefusal;
		return false;
	}
	const bool bWasActive = Preferences.Active.Equals(Filter->Name, ESearchCase::IgnoreCase);
	Filter->Name = Name;
	if (bWasActive)
	{
		Preferences.Active = Name;
	}
	return true;
}

bool UCataclysmLootFilterPreferences::DeleteFilter(FCataclysmLootFilterPreferences& Preferences, const FString& Name,
												   FString& OutRefusal)
{
	FCataclysmLootFilter* Filter = EditableFilter(Preferences, Name, OutRefusal);
	if (Filter == nullptr)
	{
		return false;
	}
	const bool bWasActive = Preferences.Active.Equals(Filter->Name, ESearchCase::IgnoreCase);
	const FString Deleted = Filter->Name;
	Preferences.Filters.RemoveAll([&Deleted](const FCataclysmLootFilter& Each) { return Each.Name == Deleted; });
	if (bWasActive)
	{
		Preferences.Active = UCataclysmLootFilter::DefaultFilterName;
	}
	return true;
}

bool UCataclysmLootFilterPreferences::AddRule(FCataclysmLootFilterPreferences& Preferences, const FString& Name,
											  FString& OutRefusal)
{
	FCataclysmLootFilter* Filter = EditableFilter(Preferences, Name, OutRefusal);
	if (Filter == nullptr)
	{
		return false;
	}
	if (Filter->Rules.Num() >= UCataclysmLootFilter::MaxRules)
	{
		OutRefusal = TooManyRulesRefusal;
		return false;
	}
	Filter->Rules.Add(FCataclysmLootFilterRule());
	return true;
}

bool UCataclysmLootFilterPreferences::RemoveRule(FCataclysmLootFilterPreferences& Preferences, const FString& Name,
												 int32 Index, FString& OutRefusal)
{
	FCataclysmLootFilter* Filter = EditableFilter(Preferences, Name, OutRefusal);
	if (Filter == nullptr)
	{
		return false;
	}
	if (!Filter->Rules.IsValidIndex(Index))
	{
		OutRefusal = NoSuchRuleRefusal;
		return false;
	}
	Filter->Rules.RemoveAt(Index);
	return true;
}

bool UCataclysmLootFilterPreferences::MoveRule(FCataclysmLootFilterPreferences& Preferences, const FString& Name,
											   int32 Index, int32 Delta, FString& OutRefusal)
{
	FCataclysmLootFilter* Filter = EditableFilter(Preferences, Name, OutRefusal);
	if (Filter == nullptr)
	{
		return false;
	}
	const int32 To = Index + Delta;
	if (!Filter->Rules.IsValidIndex(Index) || !Filter->Rules.IsValidIndex(To))
	{
		OutRefusal = NoSuchRuleRefusal;
		return false;
	}
	Filter->Rules.Swap(Index, To);
	return true;
}

bool UCataclysmLootFilterPreferences::SetRule(FCataclysmLootFilterPreferences& Preferences, const FString& Name,
											  int32 Index, const FCataclysmLootFilterRule& Rule, FString& OutRefusal)
{
	FCataclysmLootFilter* Filter = EditableFilter(Preferences, Name, OutRefusal);
	if (Filter == nullptr)
	{
		return false;
	}
	if (!Filter->Rules.IsValidIndex(Index))
	{
		OutRefusal = NoSuchRuleRefusal;
		return false;
	}
	Filter->Rules[Index] = Rule;
	return true;
}

FCataclysmLootFilterPreferences UCataclysmLootFilterPreferences::Sanitised(
	const FCataclysmLootFilterPreferences& Preferences)
{
	FCataclysmLootFilterPreferences Out;
	Out.Version = Preferences.Version;
	for (const FCataclysmLootFilter& Stored : Preferences.Filters)
	{
		if (Out.Filters.Num() >= MaxSavedFilters)
		{
			break;
		}
		FCataclysmLootFilter Filter = Stored;
		if (Filter.Rules.Num() > UCataclysmLootFilter::MaxRules)
		{
			Filter.Rules.SetNum(UCataclysmLootFilter::MaxRules);
		}
		const FString Name = Filter.Name.TrimStartAndEnd();
		Filter.Name = Name.IsEmpty() || Name.Len() > MaxNameLength || NameTaken(Out, Name) ? FreeName(Out) : Name;
		Out.Filters.Add(Filter);
	}

	// THE ACTIVE NAME, matched as the file holds it, so a filter renamed above loses being active rather than another
	// filter gaining it.
	FCataclysmLootFilter Active;
	const bool bKept = Find(Out, Preferences.Active, Active)
		&& (IsBuiltInName(Preferences.Active)
			|| Preferences.Filters.ContainsByPredicate([&Active](const FCataclysmLootFilter& Each)
			{
				return Each.Name.TrimStartAndEnd().Equals(Active.Name, ESearchCase::IgnoreCase);
			}));
	Out.Active = bKept ? Active.Name : FString(UCataclysmLootFilter::DefaultFilterName);
	return Out;
}

FString UCataclysmLootFilterPreferences::ToJson(const FCataclysmLootFilterPreferences& Preferences)
{
	FString Json;
	FJsonObjectConverter::UStructToJsonObjectString(Preferences, Json);
	return Json;
}

bool UCataclysmLootFilterPreferences::FromJson(const FString& Json, FCataclysmLootFilterPreferences& Out)
{
	FCataclysmLootFilterPreferences Read;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Read))
	{
		Out = NewPlayer();
		return false;
	}
	Out = Sanitised(Read);
	return true;
}

FString UCataclysmLootFilterPreferences::FilePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Preferences"), TEXT("LootFilters.json"));
}

bool UCataclysmLootFilterPreferences::Load(const FString& Path, FCataclysmLootFilterPreferences& Out)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		Out = NewPlayer();
		return false;
	}
	return FromJson(Json, Out);
}

bool UCataclysmLootFilterPreferences::Save(const FString& Path, const FCataclysmLootFilterPreferences& Preferences)
{
	return FFileHelper::SaveStringToFile(ToJson(Preferences), *Path);
}

FCataclysmLootFilterPreferences UCataclysmLootFilterPreferences::NewPlayer()
{
	FCataclysmLootFilterPreferences Preferences;
	Preferences.Active = UCataclysmLootFilter::DefaultFilterName;
	return Preferences;
}
