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

	/** The seed `Character_v7.json` to `Character_v13.json` state: not the one their identifier gives, so a seed
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
	 * What `Migrate_9_to_10` writes into a run file written before version 10, and so what the run record writes back
	 * for one: a `bIsVengefulWraith` of false on every creature the floor holds. Issue #2725, 2026-11-19. THE RUN
	 * RECORD'S 10, not the character record's (`AddTheCharactersVersion10Defaults`). ONE COPY, beside the version 9 one
	 * and for the same callers.
	 */
	inline void AddTheRunsVersion10Defaults(FJsonObject& Expected)
	{
		const TSharedPtr<FJsonObject>* Floor = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Creatures = nullptr;
		if (!Expected.TryGetObjectField(TEXT("Floor"), Floor) || Floor == nullptr || !Floor->IsValid()
			|| !(*Floor)->TryGetArrayField(TEXT("Creatures"), Creatures) || Creatures == nullptr)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Creatures)
		{
			const TSharedPtr<FJsonObject>* Creature = nullptr;
			if (Entry.IsValid() && Entry->TryGetObject(Creature) && Creature != nullptr && Creature->IsValid())
			{
				(*Creature)->SetBoolField(TEXT("bIsVengefulWraith"), false);
			}
		}
	}

	/**
	 * What `Migrate_10_to_11` writes into a run file written before version 11, and so what the run record writes
	 * back for one: the run's own `DungeonsCleared` as `ClearsSinceLastDeath` when it records no death (an empty or
	 * missing `LastDeathDungeonName`), else 0. Issue #2741 slice 5b, 2026-11-20. THE RUN RECORD'S 11. ONE COPY, beside
	 * the version 10 one and for the same callers.
	 */
	inline void AddTheRunsVersion11Defaults(FJsonObject& Expected)
	{
		FString DeathDungeon;
		const bool bDiedThisRun = Expected.TryGetStringField(TEXT("LastDeathDungeonName"), DeathDungeon)
			&& !DeathDungeon.IsEmpty();
		double Cleared = 0.0;
		if (bDiedThisRun || !Expected.TryGetNumberField(TEXT("DungeonsCleared"), Cleared))
		{
			Cleared = 0.0;
		}
		Expected.SetNumberField(TEXT("ClearsSinceLastDeath"), Cleared);
	}

	/**
	 * What `Migrate_11_to_12` writes into a run file written before version 12, and so what a test that runs an older
	 * run file through the chain adds before comparing: no city marked as damaged, the run's population not fallen below
	 * half, and no gold counted in the walk. Issue #2741 slice 6b, 2026-11-20. THE RUN RECORD'S 12. ONE COPY, beside the
	 * version 11 one and for the same callers.
	 */
	inline void AddTheRunsVersion12Defaults(FJsonObject& Expected)
	{
		Expected.SetBoolField(TEXT("bPopulationFellBelowHalf"), false);
		const TArray<TSharedPtr<FJsonValue>>* Cities = nullptr;
		if (Expected.TryGetArrayField(TEXT("Cities"), Cities) && Cities)
		{
			for (const TSharedPtr<FJsonValue>& CityEntry : *Cities)
			{
				const TSharedPtr<FJsonObject>* City = nullptr;
				if (CityEntry.IsValid() && CityEntry->TryGetObject(City) && City && City->IsValid())
				{
					(*City)->SetBoolField(TEXT("bTookSurgeDamage"), false);
				}
			}
		}
		const TSharedPtr<FJsonObject>* Walk = nullptr;
		if (Expected.TryGetObjectField(TEXT("Walk"), Walk) && Walk && Walk->IsValid())
		{
			(*Walk)->SetNumberField(TEXT("GoldDroppedThisWalk"), 0);
			(*Walk)->SetNumberField(TEXT("GoldPickedUpThisWalk"), 0);
		}
	}

	/**
	 * What `Migrate_12_to_13` writes into a run file written before version 13, and so what a test that runs an older
	 * run file through the chain adds before comparing: an empty `Building` on every city and a `BuildDaysLeft` of 0 on
	 * every upgrade it holds. Issue #2825, 2026-11-21. THE RUN RECORD'S 13. ONE COPY, beside the version 12 one and for
	 * the same callers.
	 */
	inline void AddTheRunsVersion13Defaults(FJsonObject& Expected)
	{
		const TArray<TSharedPtr<FJsonValue>>* Cities = nullptr;
		if (!Expected.TryGetArrayField(TEXT("Cities"), Cities) || !Cities)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& CityEntry : *Cities)
		{
			const TSharedPtr<FJsonObject>* City = nullptr;
			if (!CityEntry.IsValid() || !CityEntry->TryGetObject(City) || !City || !City->IsValid())
			{
				continue;
			}
			(*City)->SetArrayField(TEXT("Building"), TArray<TSharedPtr<FJsonValue>>());
			const TArray<TSharedPtr<FJsonValue>>* Upgrades = nullptr;
			if (!(*City)->TryGetArrayField(TEXT("Upgrades"), Upgrades) || !Upgrades)
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& UpgradeEntry : *Upgrades)
			{
				const TSharedPtr<FJsonObject>* Upgrade = nullptr;
				if (UpgradeEntry.IsValid() && UpgradeEntry->TryGetObject(Upgrade) && Upgrade && Upgrade->IsValid())
				{
					(*Upgrade)->SetNumberField(TEXT("BuildDaysLeft"), 0);
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

	/**
	 * What `Migrate_4_to_5` writes into an account file written before version 5, and so what the account record writes
	 * back for one: nothing owed, `"GoldOwed": 0, "GoldOwedEachDay": 0`. Issue #2741 slice 6e, 2026-11-20. ONE COPY,
	 * for every test that runs an older account file through the whole chain.
	 */
	inline void AddTheAccountsVersion5Defaults(FJsonObject& Expected)
	{
		Expected.SetNumberField(TEXT("GoldOwed"), 0.0);
		Expected.SetNumberField(TEXT("GoldOwedEachDay"), 0.0);
	}

	/**
	 * What `Migrate_10_to_11` writes into a character file written before version 11, and so what the character record
	 * writes back for one: nothing owed, as the account's version 5. Issue #2741 slice 6e, 2026-11-20. ONE COPY, for
	 * every test that runs an older character file through the whole chain.
	 */
	inline void AddTheCharactersVersion11Defaults(FJsonObject& Expected)
	{
		Expected.SetNumberField(TEXT("GoldOwed"), 0.0);
		Expected.SetNumberField(TEXT("GoldOwedEachDay"), 0.0);
	}

	/**
	 * What the account record's `Migrate_5_to_6` and the character record's newest step write into each allocation a
	 * file holds: no extra option, `"ExtraOptionNode": "None", "ExtraOption": 0`. Issue #2741 slice 8, 2026-11-21. An
	 * allocation the expected record does not hold is left alone, as the step leaves it.
	 */
	inline void AddTheExtraOptionDefaults(FJsonObject& Holder, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Allocation = nullptr;
		if (Holder.TryGetObjectField(Key, Allocation) && Allocation != nullptr && Allocation->IsValid())
		{
			(*Allocation)->SetStringField(TEXT("ExtraOptionNode"), TEXT("None"));
			(*Allocation)->SetNumberField(TEXT("ExtraOption"), 0.0);
		}
	}

	/** What `Migrate_5_to_6` writes into an account file written before version 6: no extra option in its tree. ONE COPY,
	 *  for every test that runs an older account file through the whole chain. */
	inline void AddTheAccountsVersion6Defaults(FJsonObject& Expected)
	{
		AddTheExtraOptionDefaults(Expected, TEXT("EmpireTree"));
	}

	/**
	 * What the character record's newest step writes into a character file written before it: no extra option in any of
	 * its three allocations. Issue #2741 slice 8. THE CHARACTER RECORD'S 12; the gems window (issue #46) took 13, after
	 * it. ONE COPY, for every test that runs an older character file through the whole chain.
	 */
	inline void AddTheCharactersVersion12Defaults(FJsonObject& Expected)
	{
		AddTheExtraOptionDefaults(Expected, TEXT("PassiveAllocation"));
		AddTheExtraOptionDefaults(Expected, TEXT("PrivateEmpireTree"));
		const TSharedPtr<FJsonObject>* Build = nullptr;
		if (Expected.TryGetObjectField(TEXT("ConsumptionBuild"), Build) && Build != nullptr && Build->IsValid())
		{
			AddTheExtraOptionDefaults(**Build, TEXT("PassiveAllocation"));
		}
	}

	/**
	 * Every socket count the JSON value holds, at any depth, written as that many empty sockets that fit any type: what
	 * `CataclysmSaveGems::WriteTheSocketLists` writes. Issue #46, slice G3, ruled 2026-11-21: "sockets migrated from
	 * today's count fit any type". Used by the character record's version 13 defaults and the run record's version 14.
	 */
	inline void AddTheSocketLists(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return;
		}
		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				AddTheSocketLists(Entry);
			}
			return;
		}
		if (Value->Type != EJson::Object || !Value->AsObject().IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonObject> Holder = Value->AsObject();
		double Count = 0.0;
		const TSharedPtr<FJsonValue> Sockets = Holder->TryGetField(TEXT("Sockets"));
		if (Sockets.IsValid() && Sockets->Type == EJson::Number && Sockets->TryGetNumber(Count))
		{
			TArray<TSharedPtr<FJsonValue>> List;
			for (int32 Index = 0; Index < static_cast<int32>(Count); ++Index)
			{
				const TSharedRef<FJsonObject> Socket = MakeShared<FJsonObject>();
				Socket->SetStringField(TEXT("Type"), TEXT("Any"));
				Socket->SetStringField(TEXT("Gem"), TEXT("None"));
				Socket->SetStringField(TEXT("GemRarity"), TEXT("Everyday"));
				List.Add(MakeShared<FJsonValueObject>(Socket));
			}
			Holder->SetArrayField(TEXT("Sockets"), List);
		}
		for (const auto& Field : Holder->Values) // `auto`: the key type follows the engine's JSON storage setting.
		{
			AddTheSocketLists(Field.Value);
		}
	}

	/** "No gem" on every object of the list `Holder[Key]`: `Gem` and `GemRarity`, and `GemQuantity` when asked. */
	inline void AddNoGemOnEach(FJsonObject& Holder, const TCHAR* Key, bool bWithQuantity)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Holder.TryGetArrayField(Key, Entries) || !Entries)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Entries)
		{
			const TSharedPtr<FJsonObject>* Slot = nullptr;
			if (Entry.IsValid() && Entry->TryGetObject(Slot) && Slot && Slot->IsValid())
			{
				(*Slot)->SetStringField(TEXT("Gem"), TEXT("None"));
				(*Slot)->SetStringField(TEXT("GemRarity"), TEXT("Everyday"));
				if (bWithQuantity)
				{
					(*Slot)->SetNumberField(TEXT("GemQuantity"), 0);
				}
			}
		}
	}

	/**
	 * What the character record's `Migrate_12_to_13` writes into a character file written before version 13: each socket
	 * count as that many empty sockets of type Any, and no gem in any carried or stashed slot. Issue #46, slices G2 and
	 * G3. THE CHARACTER RECORD'S 13. ONE COPY, for every test that runs an older character file through the whole chain.
	 */
	inline void AddTheCharactersVersion13Defaults(FJsonObject& Expected)
	{
		for (const auto& Field : Expected.Values)
		{
			AddTheSocketLists(Field.Value);
		}
		AddNoGemOnEach(Expected, TEXT("CarriedSlots"), false);
		AddNoGemOnEach(Expected, TEXT("PrivateStash"), false);
	}

	/**
	 * What the run record's `Migrate_13_to_14` writes into a run file written before version 14: each socket count as
	 * that many empty sockets of type Any, and no gem on any ground item of `Floor`. Issue #46, slices G3 and G5. THE RUN
	 * RECORD'S 14. ONE COPY, beside the version 13 one and for the same callers.
	 */
	inline void AddTheRunsVersion14Defaults(FJsonObject& Expected)
	{
		for (const auto& Field : Expected.Values)
		{
			AddTheSocketLists(Field.Value);
		}
		const TSharedPtr<FJsonObject>* Floor = nullptr;
		if (Expected.TryGetObjectField(TEXT("Floor"), Floor) && Floor && Floor->IsValid())
		{
			AddNoGemOnEach(**Floor, TEXT("GroundItems"), true);
		}
	}

	/**
	 * What the account record's newest step writes into an account file written before it: no tier 1 gold spent on city
	 * upgrades that Reinvestment counts (`GoldSpentOnCityUpgrades`, 0). Issue #2741 slice 9. NAMED FOR THE VERSION IT
	 * REACHES, 7. ONE COPY, for every test that runs an older account file through the whole chain.
	 */
	inline void AddTheAccountsVersion7Defaults(FJsonObject& Expected)
	{
		Expected.SetNumberField(TEXT("GoldSpentOnCityUpgrades"), 0.0);
	}

	/**
	 * What the character record's newest step writes into a character file written before it: no tier 1 gold spent on
	 * city upgrades (`GoldSpentOnCityUpgrades`, 0), for a Solo Self-Found character's Reinvestment. Issue #2741 slice 9.
	 * NAMED FOR THE VERSION IT REACHES, 14, after the gems window's 13 (V1). ONE COPY.
	 */
	inline void AddTheCharactersVersion14Defaults(FJsonObject& Expected)
	{
		Expected.SetNumberField(TEXT("GoldSpentOnCityUpgrades"), 0.0);
	}

	/**
	 * What the run record's newest step writes into a run file written before it: no city bailed out, no dungeon a bonus
	 * one, no bonus dungeon cleared and no bribe made (`bBailedOut`, `bBonus`, `BonusDungeonsCleared`,
	 * `bBribedThisSurge`). Issue #2741 slice 9. NAMED FOR THE VERSION IT REACHES, 15, after the gems window's
	 * 14 (V1). ONE COPY.
	 */
	inline void AddTheRunsVersion15Defaults(FJsonObject& Expected)
	{
		for (const TCHAR* Field : { TEXT("Cities"), TEXT("Dungeons") })
		{
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (!Expected.TryGetArrayField(Field, Entries) || !Entries)
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Entry : *Entries)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				if (Entry.IsValid() && Entry->TryGetObject(Object) && Object && Object->IsValid())
				{
					(*Object)->SetBoolField(FCString::Strcmp(Field, TEXT("Cities")) == 0 ? TEXT("bBailedOut") : TEXT("bBonus"),
											false);
				}
			}
		}
		Expected.SetNumberField(TEXT("BonusDungeonsCleared"), 0.0);
		Expected.SetBoolField(TEXT("bBribedThisSurge"), false);
	}

	/**
	 * What the run record's newest step writes into a run file written before it: the cleanse not bought
	 * (`bCleanseBoughtThisRun`). Issue #2838. NAMED FOR THE VERSION IT REACHES, 16. ONE COPY.
	 */
	inline void AddTheRunsVersion16Defaults(FJsonObject& Expected)
	{
		Expected.SetBoolField(TEXT("bCleanseBoughtThisRun"), false);
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
