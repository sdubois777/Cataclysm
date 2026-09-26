// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonGameMode.h"

#include "AbilitySystem/CataclysmRegeneration.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmContagion.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Dungeon/CataclysmFloorHazardSource.h"
#include "Interface/CataclysmCreaturePanel.h"
#include "Items/CataclysmDropRoll.h"
#include "Items/CataclysmDroppedItem.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerController.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmBeaconCharacter.h"
#include "Character/CataclysmBloomCharacter.h"
#include "Character/CataclysmChorusSourceCharacter.h"
#include "Character/CataclysmFloorSourceCharacter.h"
#include "Character/CataclysmSpireCharacter.h"
#include "Character/CataclysmVeinCharacter.h"
#include "Character/CataclysmSarcophagusCharacter.h"
#include "Character/CataclysmPortalCharacter.h"
#include "Character/CataclysmRiftCharacter.h"
#include "GameplayTagsManager.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmGatekeeperCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonStairs.h"
#include "Dungeon/CataclysmFloorBrief.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Empire/CataclysmDungeonKind.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/CataclysmGameInstance.h"
#include "HAL/IConsoleManager.h"
#include "Save/CataclysmSaveWriter.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/Class.h"

namespace
{
	/**
	 * Which dungeon to walk. 0 uses the setting, above 0 is that dungeon, -1
	 * rolls a new one every time play begins.
	 *
	 * TYPED AT THE CONSOLE RATHER THAN EDITED, so a person can look at another
	 * floor without changing a default and rebuilding.
	 *
	 * NAMED FOR THIS FILE. Unreal merges a module's `.cpp` files into one
	 * translation unit, so two files declaring the same file-scope name collide,
	 * and only once both are committed. The project names console variables after
	 * what owns them for exactly that reason.
	 */
	static int32 GCataclysmDungeonSeedOverride = 0;
	static FAutoConsoleVariableRef CVarCataclysmDungeonSeed(
		TEXT("Cataclysm.DungeonSeed"),
		GCataclysmDungeonSeedOverride,
		TEXT("Which dungeon to generate. 0 uses the game mode's own setting, "
			 "above 0 is that dungeon, -1 rolls a new one every time play begins."),
		ECVF_Default);

	/**
	 * The floor control's name, written once.
	 *
	 * TWO THINGS USE IT: the registration below, and the lookup in
	 * `DungeonGameModeFollowFloorAtTheConsole`, which cannot reach the variable
	 * any other way -- `FAutoConsoleVariableRef` inherits privately from
	 * `FAutoConsoleObject`, so its `AsVariable` is not accessible. Two spellings
	 * of the same name would fail as a lookup that silently finds nothing.
	 */
	const TCHAR* const GCataclysmDungeonFloorVariableName = TEXT("Cataclysm.DungeonFloor");

	/** Which floor of it. 0 uses the setting, above 0 is that floor. */
	static int32 GCataclysmDungeonFloorOverride = 0;
	static FAutoConsoleVariableRef CVarCataclysmDungeonFloor(
		GCataclysmDungeonFloorVariableName,
		GCataclysmDungeonFloorOverride,
		TEXT("Which floor of the dungeon to generate. 0 uses the game mode's "
			 "own setting."),
		ECVF_Default);

	/**
	 * How many floors the whole dungeon has. 0 uses the setting.
	 *
	 * ZERO MEANS "USE THE SETTING", like the seed and floor controls above and
	 * unlike the layout and density controls below, because a dungeon of no
	 * floors is not a real answer -- it is one that would divide by zero in the
	 * Enemy Score model.
	 *
	 * WHAT IT IS FOR. Enemy Score, and therefore the experience a kill grants,
	 * is driven by how deep into a dungeon a floor is. Being able to type
	 * another length and press Play again is how "what is this creature worth"
	 * gets looked at rather than argued about. Issue #926.
	 */
	static int32 GCataclysmDungeonFloorCountOverride = 0;
	static FAutoConsoleVariableRef CVarCataclysmDungeonFloorCount(
		TEXT("Cataclysm.DungeonFloorCount"),
		GCataclysmDungeonFloorCountOverride,
		TEXT("How many floors the whole dungeon has, which decides how deep a "
			 "floor is and so what a creature standing on it is worth. 0 uses "
			 "the game mode's own setting. Never reported below the floor being "
			 "walked, because the stairs go down for ever."),
		ECVF_Default);

	/**
	 * Which layout family carves it. -1 uses the setting.
	 *
	 * MINUS ONE RATHER THAN ZERO MEANS "USE THE SETTING" HERE, unlike the two
	 * above, because zero is a real answer: it is the Halls family. A layout
	 * that could not be asked for would be the one nobody could look at.
	 */
	static int32 GCataclysmDungeonLayoutOverride = -1;
	static FAutoConsoleVariableRef CVarCataclysmDungeonLayout(
		TEXT("Cataclysm.DungeonLayout"),
		GCataclysmDungeonLayoutOverride,
		TEXT("Which layout family carves the floor. -1 uses the game mode's own "
			 "setting, 0 Halls, 1 Caverns, 2 Arena."),
		ECVF_Default);

	/**
	 * Which sub-type the dungeon is. An empty name uses the setting.
	 *
	 * WITHOUT THIS, THREE SUB-TYPES COULD NOT BE REACHED IN PLAY AT ALL. The
	 * sub-type is an `EditDefaultsOnly` property and `L_Dungeon` uses this game
	 * mode directly with no Blueprint subclass, so there was nowhere to set it
	 * from: Horde, Elite and Volatile were built and unreachable. Issue #1502.
	 *
	 * A NAME RATHER THAN A NUMBER, unlike the four controls above, and the
	 * reason is not taste. `ECataclysmDungeonSubType`'s own comment says "a
	 * saved dungeon or a data table row that stores one of these stores the
	 * number", so its numbers exist to be written to disk. The names are what
	 * the design document, the issues and this log all call these things, and
	 * a person typing 6 for Volatile is a person who had to look it up.
	 *
	 * LOOKED UP THROUGH THE REFLECTED ENUM AND NOT THROUGH A TABLE WRITTEN HERE.
	 * A table would be a second copy of the eight sub-types, which is the
	 * arrangement `ECataclysmDungeonSubType`'s own comment says this repository
	 * has been bitten by, and it would go stale in silence: a ninth sub-type
	 * would be added, nothing would fail, and it simply would not be reachable,
	 * which is this issue happening a second time.
	 */
	static FString GCataclysmDungeonSubTypeOverride;

	/**
	 * Every sub-type's name, in the order they are declared, for the log.
	 *
	 * THE LAST ENTRY IS SKIPPED. Every `UENUM` gains a hidden maximum entry
	 * that `NumEnums` counts. It is added when the enum registers, by
	 * `UEnum::SetEnums`, and NOT by Unreal's header tool -- the generated file
	 * for this enum lists only the eight declared sub-types, so looking there
	 * suggests no such entry exists. `EverySubType` in
	 * `Tests/CataclysmFloorBriefTests.cpp` skips it for the same reason.
	 */
	FString DungeonGameModeSubTypeNames()
	{
		const UEnum* Reflected = StaticEnum<ECataclysmDungeonSubType>();
		if (!Reflected)
		{
			return FString();
		}

		TArray<FString> Names;
		for (int32 Index = 0; Index + 1 < Reflected->NumEnums(); ++Index)
		{
			Names.Add(Reflected->GetNameStringByIndex(Index));
		}

		return FString::Join(Names, TEXT(", "));
	}

	/**
	 * The sub-type named at the console, or nothing when there is no usable
	 * name there.
	 *
	 * NOTHING COVERS TWO CASES AND THEY ARE DELIBERATELY THE SAME ANSWER:
	 * nobody has typed anything, and somebody typed a word that is not a
	 * sub-type. Both mean the game mode's own setting decides, which is what
	 * every other control in this file does when it is not asked for anything.
	 * Which of the two happened is said in the log rather than in the return.
	 *
	 * SPACES ARE DROPPED BEFORE THE LOOKUP, so the one sub-type whose editor
	 * name is two words -- `CowLevel`, shown as "Cow Level" -- can be typed
	 * either way. The match ignores letter case because `FName` does.
	 */
	TOptional<ECataclysmDungeonSubType> DungeonGameModeSubTypeAskedFor()
	{
		FString Asked = GCataclysmDungeonSubTypeOverride;
		Asked.ReplaceInline(TEXT(" "), TEXT(""));

		if (Asked.IsEmpty())
		{
			return TOptional<ECataclysmDungeonSubType>();
		}

		const UEnum* Reflected = StaticEnum<ECataclysmDungeonSubType>();
		if (!Reflected)
		{
			return TOptional<ECataclysmDungeonSubType>();
		}

		const int32 Index = Reflected->GetIndexByNameString(Asked);

		// THE HIDDEN `_MAX` IS NOT A SUB-TYPE, and it is the one wrong answer
		// this lookup can give that would read as a right one: it is a real
		// entry with a real index, so it would be accepted and then handed to
		// the floor rules as a value no dungeon can carry.
		if (Index == INDEX_NONE || Index + 1 >= Reflected->NumEnums())
		{
			return TOptional<ECataclysmDungeonSubType>();
		}

		return static_cast<ECataclysmDungeonSubType>(
			Reflected->GetValueByIndex(Index));
	}

	/**
	 * Says in the log what the sub-type control did with what was typed.
	 *
	 * WHEN IT IS TYPED AND NOT WHEN A FLOOR IS BUILT. A person types a name at
	 * the console and then presses Play, and a mistyped name that said nothing
	 * until the floor appeared would look exactly like the fault this control
	 * was written to remove: asking for a Horde dungeon and walking an ordinary
	 * one. Saying it here puts the answer on the line after the question.
	 *
	 * IT NAMES THE SUB-TYPES IT TAKES WHEN IT REFUSES ONE, read off the same
	 * reflected enum the lookup uses, so the complaint cannot list a different
	 * set from the one that would have been accepted.
	 */
	void DungeonGameModeSubTypeChanged(IConsoleVariable* Variable)
	{
		(void)Variable;

		const FString Asked = GCataclysmDungeonSubTypeOverride;
		if (Asked.IsEmpty())
		{
			UE_LOG(LogCataclysm, Log,
				TEXT("Cataclysm.DungeonSubType was cleared, so the game mode's "
					 "own sub-type setting decides again."));
			return;
		}

		const TOptional<ECataclysmDungeonSubType> Chosen =
			DungeonGameModeSubTypeAskedFor();
		if (!Chosen.IsSet())
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Cataclysm.DungeonSubType does not know \"%s\", so the "
					 "game mode's own setting still decides. The sub-types it "
					 "takes are: %s."),
				*Asked, *DungeonGameModeSubTypeNames());
			return;
		}

		const UEnum* Reflected = StaticEnum<ECataclysmDungeonSubType>();
		const FString Understood = Reflected
			? Reflected->GetNameStringByValue(static_cast<int64>(Chosen.GetValue()))
			: Asked;

		UE_LOG(LogCataclysm, Log,
			TEXT("Cataclysm.DungeonSubType is %s, asked for as \"%s\". Every "
				 "dungeon floor built from now on is a floor of a %s dungeon."),
			*Understood, *Asked, *Understood);
	}

	static FAutoConsoleVariableRef CVarCataclysmDungeonSubType(
		TEXT("Cataclysm.DungeonSubType"),
		GCataclysmDungeonSubTypeOverride,
		TEXT("Which sub-type the dungeon is, by name: None, Timed, Horde, "
			 "Siege, CowLevel, Elite, Volatile, Sacrificial. Letter case does "
			 "not matter and \"Cow Level\" may be typed with its space. Horde "
			 "is one arena walked as waves, Elite stands a boss at every "
			 "floor's exit, and Volatile draws new modifiers on every floor. "
			 "Type None for an ordinary dungeon, or \"\" to hand the choice "
			 "back to the game mode's own setting."),
		FConsoleVariableDelegate::CreateStatic(&DungeonGameModeSubTypeChanged),
		ECVF_Default);

	/**
	 * Which dungeon modifiers the dungeon being played carries, by row key or by
	 * name, separated by commas. Empty uses the dungeon's own. Issue #41.
	 *
	 * WITHOUT THIS NO DUNGEON THE OWNER PLAYS CARRIES A MODIFIER. A dungeon gets
	 * modifiers only when `EnterEmpireDungeon` copies them off an empire dungeon,
	 * and the owner's playtests press Play in `L_Dungeon` and set
	 * `Cataclysm.DungeonSubType` instead. Their six saved play logs of 2026-09-08
	 * to 2026-09-10 hold no dungeon modifier key at all.
	 *
	 * READ WHEN A FLOOR IS BUILT, like every other control in this file, so typing
	 * it and then `Cataclysm.DungeonFloor 2` puts the modifiers on floor 2.
	 */
	static FString GCataclysmDungeonModifiersOverride;

	/**
	 * The row keys typed at the console, in the order typed, or none when nothing
	 * typed names a row.
	 *
	 * NONE COVERS TWO CASES AND THEY GET THE SAME ANSWER, as they do for the
	 * sub-type control: nothing typed, and only words that name no modifier.
	 * Both mean the dungeon's own modifiers decide.
	 */
	TArray<FName> DungeonGameModeModifiersAskedFor(TArray<FString>& OutNotUnderstood)
	{
		OutNotUnderstood.Reset();
		if (GCataclysmDungeonModifiersOverride.TrimStartAndEnd().IsEmpty())
		{
			return {};
		}

		return UCataclysmDungeonModifierTable::KeysNamedBy(
			GCataclysmDungeonModifiersOverride,
			UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
			OutNotUnderstood);
	}

	/**
	 * A modifier's name and how much of it is built, for the log: "Starvation
	 * (Built)", "Edict of Silence (Not built yet)".
	 */
	FString DungeonGameModeModifierNameAndState(FName Key)
	{
		return FString::Printf(
			TEXT("%s (%s)"), *UCataclysmDungeonModifierTable::NameOf(Key),
			*UEnum::GetDisplayValueAsText(
				UCataclysmDungeonModifierEffects::BuiltStateOf(Key)).ToString());
	}

	/**
	 * Says in the log what the modifier control did with what was typed.
	 *
	 * WHEN IT IS TYPED AND NOT WHEN A FLOOR IS BUILT, for the reason the sub-type
	 * control gives: a mistyped name that said nothing until the floor appeared
	 * would look exactly like the modifier doing nothing.
	 */
	void DungeonGameModeModifiersChanged(IConsoleVariable* Variable)
	{
		(void)Variable;

		TArray<FString> NotUnderstood;
		const TArray<FName> Keys = DungeonGameModeModifiersAskedFor(NotUnderstood);

		for (const FString& Piece : NotUnderstood)
		{
			UE_LOG(LogCataclysm, Warning,
				TEXT("Cataclysm.DungeonModifiers does not know \"%s\". Type row "
					 "keys or names from game/Data/DungeonModifiers.csv, "
					 "separated by commas."),
				*Piece);
		}

		if (Keys.IsEmpty())
		{
			UE_LOG(LogCataclysm, Log,
				TEXT("Cataclysm.DungeonModifiers names no dungeon modifier, so every "
					 "dungeon floor built from now on carries the dungeon's own."));
			return;
		}

		TArray<FString> Names;
		for (const FName Key : Keys)
		{
			Names.Add(DungeonGameModeModifierNameAndState(Key));
		}

		UE_LOG(LogCataclysm, Log,
			TEXT("Cataclysm.DungeonModifiers is %s. Every dungeon floor built from "
				 "now on carries these instead of the dungeon's own."),
			*FString::Join(Names, TEXT(", ")));
	}

	static FAutoConsoleVariableRef CVarCataclysmDungeonModifiers(
		TEXT("Cataclysm.DungeonModifiers"),
		GCataclysmDungeonModifiersOverride,
		TEXT("Which dungeon modifiers the dungeon carries, by row key or by name "
			 "from game/Data/DungeonModifiers.csv, separated by commas, for "
			 "example: Starvation, Dehydration. Read when a floor is built. Type "
			 "\"\" to hand the choice back to the dungeon's own modifiers."),
		FConsoleVariableDelegate::CreateStatic(&DungeonGameModeModifiersChanged),
		ECVF_Default);

	/**
	 * How dense the floor's creatures are. Below 0 uses the game mode's setting.
	 *
	 * BELOW ZERO RATHER THAN ZERO MEANS "USE THE SETTING", unlike the seed and
	 * floor controls above, because zero is a real answer here: it is a floor
	 * with nothing standing on it, which is what walking one to look at its shape
	 * wants. The layout control takes -1 for the same reason.
	 *
	 * WHAT IT IS FOR. How many creatures a floor should hold is the one number in
	 * this feature that the design document does not answer and that nobody has
	 * played. Being able to type another one and press Play again is how it gets
	 * judged rather than argued.
	 */
	static float GCataclysmDungeonEnemyScaleOverride = -1.0f;
	static FAutoConsoleVariableRef CVarCataclysmDungeonEnemyScale(
		TEXT("Cataclysm.DungeonEnemyScale"),
		GCataclysmDungeonEnemyScaleOverride,
		TEXT("How many creatures a dungeon floor holds, as a multiple of the "
			 "designed density. Below 0 uses the game mode's own setting, 0 "
			 "empties the floor, 1 is the designed density, 2 is twice as many."),
		ECVF_Default);

	/**
	 * Keeps the console's floor override on the floor actually being walked.
	 *
	 * WITHOUT THIS THE STAIRS SILENTLY DO NOTHING for anybody who has typed
	 * `Cataclysm.DungeonFloor 5`. That override wins over the game mode's own
	 * setting every time a floor is built, so walking down from floor 5 would set
	 * the setting to 6, build floor 5 again, and look exactly like a bug in the
	 * stairs.
	 *
	 * IT ONLY FOLLOWS AN OVERRIDE THAT IS ALREADY SET. Zero means "use the game
	 * mode's own setting", and turning that into a number would take the choice
	 * away from anybody who had not made one.
	 *
	 * IT WRITES THROUGH THE CONSOLE VARIABLE AND NOT THROUGH `GCataclysmDungeon-
	 * FloorOverride`, AND THAT IS NOT A STYLE PREFERENCE. `FAutoConsoleVariableRef`
	 * keeps a copy of the value beside the variable it references, and answers
	 * `GetInt` from the copy. Assigning to the variable moves what the game reads
	 * and leaves what the console reports behind, so `Cataclysm.DungeonFloor`
	 * would have said 5 while the player walked floor 6. Measured on 2026-08-21.
	 *
	 * AT THE CONSOLE'S OWN PRIORITY, because Unreal remembers who last set a
	 * console variable and silently discards a lower-priority write. An override
	 * above zero was typed at the console, so a write from code would be thrown
	 * away. A floor pinned on the command line is higher still and will not
	 * follow; that is a person asking for one floor over and over.
	 */
	void DungeonGameModeFollowFloorAtTheConsole(int32 NewFloorNumber)
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(
			GCataclysmDungeonFloorVariableName);

		if (Variable && Variable->GetInt() > 0)
		{
			Variable->Set(NewFloorNumber, ECVF_SetByConsole);
		}
	}

	/** How far above the walking surface a pawn's capsule middle has to sit. */
	float DungeonGameModeStandingHeightOf(const APawn* Pawn)
	{
		if (const UCapsuleComponent* Capsule =
				Pawn ? Pawn->FindComponentByClass<UCapsuleComponent>() : nullptr)
		{
			return Capsule->GetScaledCapsuleHalfHeight();
		}

		// A pawn with no capsule is not a character. Placing it exactly on the
		// surface is the honest answer: there is no half height to raise it by.
		return 0.0f;
	}

	/**
	 * The same question for a creature that has not been spawned yet.
	 *
	 * READ FROM THE CLASS DEFAULT OBJECT rather than corrected by a constant. The
	 * sandbox's spawners each carry their own `RiseCm` worked out from a base
	 * enemy's 80 cm capsule, which is six copies of the same arithmetic and six
	 * chances for one to be left behind when a creature is resized. The default
	 * object already holds the answer.
	 *
	 * A SEPARATE NAME RATHER THAN AN OVERLOAD, because a `TSubclassOf` converts
	 * to a `UClass*` and an overload set that also takes a pointer is a place for
	 * the wrong one to be chosen silently.
	 */
	float DungeonGameModeStandingHeightOfClass(
		const TSubclassOf<ACataclysmEnemyCharacter>& Class)
	{
		const ACataclysmEnemyCharacter* Default =
			Class ? Class->GetDefaultObject<ACataclysmEnemyCharacter>() : nullptr;

		if (const UCapsuleComponent* Capsule =
				Default ? Default->GetCapsuleComponent() : nullptr)
		{
			return Capsule->GetScaledCapsuleHalfHeight();
		}

		return 0.0f;
	}
}

ACataclysmDungeonGameMode::ACataclysmDungeonGameMode()
{
	// The one thing this game mode turns off. See the class comment.
	bSpawnsSandboxCreatures = false;

	// A GAME MODE THAT TICKS, WHICH THIS ONE DID NOT UNTIL ISSUE #1467. A Horde
	// dungeon's next wave arrives when the one standing is down to a tenth, and
	// noticing that is something only a clock can do -- nothing in the project
	// spawned a creature into a floor after it was built, because nothing was
	// watching a floor while it was being played.
	//
	// `bStartWithTickEnabled` AS WELL AS `bCanEverTick`. An actor with the first
	// off never gets a tick however the second is set, and a game mode's
	// defaults are not the same as an ordinary actor's.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

/**
 * Pins Wasting Sickness's chance roll so a test can assert what a blow did.
 * Issues #1786 and #41.
 *
 * THE SAME SHAPE AS `Cataclysm.AilmentRoll`, and for the reason that variable
 * gives: a test asserting that a blow did or did not inflict a stack would
 * otherwise pass some of the time and fail the rest.
 *
 * -1, THE DEFAULT, ROLLS NORMALLY. 0 inflicts a stack on every landed blow,
 * because any chance above zero beats it. 100 inflicts none, because the
 * comparison is strictly less than and the chance is below 100.
 */
static TAutoConsoleVariable<float> CVarWastingSicknessRoll(
	TEXT("Cataclysm.WastingSicknessRoll"),
	-1.0f,
	TEXT("Pin the roll Wasting Sickness compares its chance with, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins Grasping Tentacles' grab roll so a test can assert what a beat did.
 * Issues #1786 and #41.
 *
 * THE SAME SHAPE AS `Cataclysm.WastingSicknessRoll` BESIDE IT AND
 * `Cataclysm.AilmentRoll` BEFORE THAT. -1 rolls normally, 0 grabs on every beat
 * inside a reach, 100 never grabs.
 *
 * A SEPARATE VARIABLE RATHER THAN SHARING WASTING SICKNESS'S, because a test of
 * one row must be able to pin its own chance without deciding the other's -- and
 * a floor can carry both.
 */
static TAutoConsoleVariable<float> CVarGraspingTentaclesRoll(
	TEXT("Cataclysm.GraspingTentaclesRoll"),
	-1.0f,
	TEXT("Pin the roll Grasping Tentacles compares its grab chance with, 0 to ")
	TEXT("100. -1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins the roll Spore Clouds releases on, so a test can assert what a death did.
 * Issues #1820 and #41.
 *
 * THE SAME SHAPE AS THE TWO ABOVE. -1 rolls normally, 0 releases spores on every
 * creature's death, 100 releases on none -- the comparison is strictly less than
 * and the chance is below 100.
 *
 * A SEPARATE VARIABLE RATHER THAN SHARING EITHER OF THEM, for the reason
 * `Cataclysm.GraspingTentaclesRoll` gives: a floor can carry more than one of
 * these rows, and a test of one must be able to pin its own chance without
 * deciding another's.
 */
static TAutoConsoleVariable<float> CVarSporeCloudsRoll(
	TEXT("Cataclysm.SporeCloudsRoll"),
	-1.0f,
	TEXT("Pin the roll Spore Clouds compares its release chance with, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins the roll Hellfire explodes on, so a test can assert what a death did.
 * Issues #1820 and #41.
 *
 * ITS OWN VARIABLE AND NOT SPORE CLOUDS', for the reason given beside
 * `Cataclysm.GraspingTentaclesRoll`: a floor can carry more than one of these
 * rows, and a test of one must be able to pin its own chance without deciding
 * another's. Both rows are a chance on a death, so sharing one variable would
 * make either test unable to describe a floor carrying both.
 */
static TAutoConsoleVariable<float> CVarHellfireRoll(
	TEXT("Cataclysm.HellfireRoll"),
	-1.0f,
	TEXT("Pin the roll Hellfire compares its explosion chance with, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins the roll that decides which kind of mushroom a death leaves, so a test
 * can assert what it left. Issues #1820 and #41.
 *
 * IT DECIDES A KIND AND NOT WHETHER ANYTHING HAPPENS, which is what makes it
 * different from the four variables above it. Each of those can be pinned to a
 * value at which the rule does nothing at all; this one always leaves a
 * mushroom, and 0 makes every one of them the kind that helps while 100 makes
 * every one the kind that hurts.
 *
 * ITS OWN VARIABLE, for the reason `Cataclysm.GraspingTentaclesRoll` gives: a
 * floor can carry more than one of these rows and a test of one must be able to
 * pin its own roll without deciding another's.
 */
static TAutoConsoleVariable<float> CVarFungalOvergrowthRoll(
	TEXT("Cataclysm.FungalOvergrowthRoll"),
	-1.0f,
	TEXT("Pin the roll Fungal Overgrowth picks a mushroom's kind with, 0 to ")
	TEXT("100. -1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins the roll that decides whether a creature being placed is an illusion, so
 * a test can assert what a floor was populated with. Issues #1820 and #41.
 *
 * ROLLED AS A FLOOR IS POPULATED AND NOT ON AN EVENT, which is what makes it
 * different from the five variables above. Each of those is read when something
 * happens during play; this one is read once per creature, while the floor is
 * being built, and is never read again for that creature.
 *
 * 0 MAKES EVERY CREATURE ON THE FLOOR AN ILLUSION and 100 makes none of them
 * one, because the comparison is strictly below the share.
 */
/**
 * Pins the roll a creature answers a blow with, so a test can assert what a blow
 * provoked. Issues #1820 and #41.
 *
 * ITS OWN VARIABLE, for the reason `Cataclysm.GraspingTentaclesRoll` gives: a
 * floor can carry more than one of these rows and a test of one must be able to
 * pin its own roll without deciding another's.
 */
static TAutoConsoleVariable<float> CVarHolyRepercussionsRoll(
	TEXT("Cataclysm.HolyRepercussionsRoll"),
	-1.0f,
	TEXT("Pin the roll Holy Repercussions answers a blow with, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarIllusoryEnemiesRoll(
	TEXT("Cataclysm.IllusoryEnemiesRoll"),
	-1.0f,
	TEXT("Pin the roll Illusory Enemies decides a creature with, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins the roll a wounded creature is offered a mutation on, so a test can assert what
 * a beat did. Issues #1820 and #41.
 *
 * ITS OWN VARIABLE, for the reason `Cataclysm.GraspingTentaclesRoll` gives: a floor can
 * carry more than one of these rows and a test of one must be able to pin its own roll
 * without deciding another's.
 */
static TAutoConsoleVariable<float> CVarVolatileEvolutionRoll(
	TEXT("Cataclysm.VolatileEvolutionRoll"),
	-1.0f,
	TEXT("Pin the roll Volatile Evolution offers a wounded creature, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins the roll a badly hurt creature calls its guards on, so a test can assert what a
 * beat did. Issues #1820 and #41. Its own variable, for the reason
 * `Cataclysm.GraspingTentaclesRoll` gives.
 */
static TAutoConsoleVariable<float> CVarRoyalGuardRoll(
	TEXT("Cataclysm.RoyalGuardRoll"),
	-1.0f,
	TEXT("Pin the roll Royal Guard offers a badly hurt creature, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins the roll a kill the player made is offered a greater creature on, so a test can
 * assert what a death brought. Issues #1820 and #41. Its own variable, for the reason
 * `Cataclysm.GraspingTentaclesRoll` gives.
 */
static TAutoConsoleVariable<float> CVarDemonPrinceRoll(
	TEXT("Cataclysm.DemonPrinceRoll"),
	-1.0f,
	TEXT("Pin the roll Demon Prince offers a kill the player made, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * Pins the roll a diseased corpse passes its debuffs on with, so a test can assert what a
 * death spread. Issues #1820 and #41. Its own variable, for the reason
 * `Cataclysm.GraspingTentaclesRoll` gives.
 */
static TAutoConsoleVariable<float> CVarEpidemicRoll(
	TEXT("Cataclysm.EpidemicRoll"),
	-1.0f,
	TEXT("Pin the roll Epidemic offers a diseased corpse, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * The roll Vengeful Wraiths offers a creature the player killed, pinned for tests.
 *
 * THE SAME SHAPE AS THE FOUR ABOVE, for the reason the first of them gives: a rule whose
 * chance cannot be pinned can only be tested by running it until it happens.
 */
static TAutoConsoleVariable<float> CVarVengefulWraithRoll(
	TEXT("Cataclysm.VengefulWraithRoll"),
	-1.0f,
	TEXT("Pin the roll Vengeful Wraiths offers a creature the player killed, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * The roll Void Parasite offers a creature the player killed, pinned for tests. The same shape as
 * the one above, for the same reason. Issues #1820 and #41.
 */
static TAutoConsoleVariable<float> CVarVoidParasiteRoll(
	TEXT("Cataclysm.VoidParasiteRoll"),
	-1.0f,
	TEXT("Pin the roll Void Parasite offers a creature the player killed, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * The roll Dead Rising offers every creature that dies, pinned for tests. The same shape
 * as the one above, for the same reason.
 */
/**
 * The roll a step through an unstable portal makes, pinned for tests. The same shape as
 * the other pinned rolls, for the same reason.
 */
static TAutoConsoleVariable<float> CVarUnstablePortalRoll(
	TEXT("Cataclysm.UnstablePortalRoll"),
	-1.0f,
	TEXT("Pin the roll a step through an unstable portal makes, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

/**
 * The draw that decides which curse a floor carrying the starvation curse adds, pinned for
 * tests. The same shape as the other pinned rolls, for the same reason.
 */
static TAutoConsoleVariable<float> CVarStarvationCurseRoll(
	TEXT("Cataclysm.StarvationCurseRoll"),
	-1.0f,
	TEXT("Pin the draw that picks the starvation curse a floor adds, 0 to 100: below 50 ")
	TEXT("slows movement, from 50 lowers maximum health. -1 draws normally."),
	ECVF_Cheat);

/**
 * The roll a clicked pickup makes on a floor carrying Trick or Treat, pinned for tests. The
 * same shape as the other pinned rolls, for the same reason.
 */
static TAutoConsoleVariable<float> CVarTrickOrTreatRoll(
	TEXT("Cataclysm.TrickOrTreatRoll"),
	-1.0f,
	TEXT("Pin the roll a clicked pickup makes under Trick or Treat, 0 to 100: below 50 ")
	TEXT("raises two creatures, from 50 hastes the player. -1 rolls normally."),
	ECVF_Cheat);

/** Pins The Infested Hoard's roll on a paying floor creature's death, 0 to 100. Issues #1820 and #41. */
static TAutoConsoleVariable<float> CVarInfestedHoardRoll(
	TEXT("Cataclysm.InfestedHoardRoll"),
	-1.0f,
	TEXT("Pin the roll a paying floor creature's death makes under The Infested Hoard, 0 to 100: below the ")
	TEXT("chance, one infested drop. -1 rolls normally."),
	ECVF_Cheat);

/**
 * The draw that decides which kind a floor carrying Chaos Touched adds, pinned for tests.
 */
static TAutoConsoleVariable<float> CVarChaosTouchedRoll(
	TEXT("Cataclysm.ChaosTouchedRoll"),
	-1.0f,
	TEXT("Pin the draw that picks the kind Chaos Touched adds, 0 to 100 in eight even bands: ")
	TEXT("health, speed, attack speed, resistances more, then the same less. -1 draws normally."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarDeadRisingRoll(
	TEXT("Cataclysm.DeadRisingRoll"),
	-1.0f,
	TEXT("Pin the roll Dead Rising offers a creature that died, 0 to 100. ")
	TEXT("-1 rolls normally."),
	ECVF_Cheat);

namespace
{
	/** The roll Wasting Sickness's chance is compared with: pinned, or drawn. */
	float DungeonGameModeWastingSicknessRoll()
	{
		const float Pinned = CVarWastingSicknessRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll a tentacle's grab chance is compared with: pinned, or drawn. */
	float DungeonGameModeGraspingTentaclesRoll()
	{
		const float Pinned = CVarGraspingTentaclesRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll Spore Clouds' release chance is compared with: pinned, or drawn. */
	float DungeonGameModeSporeCloudsRoll()
	{
		const float Pinned = CVarSporeCloudsRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll Hellfire's explosion chance is compared with: pinned, or drawn. */
	float DungeonGameModeHellfireRoll()
	{
		const float Pinned = CVarHellfireRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll a mushroom's kind is chosen with: pinned, or drawn. */
	float DungeonGameModeFungalOvergrowthRoll()
	{
		const float Pinned = CVarFungalOvergrowthRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll a creature answers a blow with: pinned, or drawn. */
	float DungeonGameModeHolyRepercussionsRoll()
	{
		const float Pinned = CVarHolyRepercussionsRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll a creature is judged an illusion by: pinned, or drawn. */
	float DungeonGameModeIllusoryEnemiesRoll()
	{
		const float Pinned = CVarIllusoryEnemiesRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll a wounded creature's mutation is decided by: pinned, or drawn. */
	float DungeonGameModeVolatileEvolutionRoll()
	{
		const float Pinned = CVarVolatileEvolutionRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll a badly hurt creature's guards are decided by: pinned, or drawn. */
	float DungeonGameModeRoyalGuardRoll()
	{
		const float Pinned = CVarRoyalGuardRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll a corpse is decided by: pinned, or drawn. */
	float DungeonGameModeDemonPrinceRoll()
	{
		const float Pinned = CVarDemonPrinceRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/** The roll a diseased corpse's spread is decided by: pinned, or drawn. */
	float DungeonGameModeEpidemicRoll()
	{
		const float Pinned = CVarEpidemicRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	float DungeonGameModeVengefulWraithRoll()
	{
		const float Pinned = CVarVengefulWraithRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	float DungeonGameModeVoidParasiteRoll()
	{
		const float Pinned = CVarVoidParasiteRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	float DungeonGameModeUnstablePortalRoll()
	{
		const float Pinned = CVarUnstablePortalRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	float DungeonGameModeStarvationCurseRoll()
	{
		const float Pinned = CVarStarvationCurseRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	float DungeonGameModeTrickOrTreatRoll()
	{
		const float Pinned = CVarTrickOrTreatRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	float DungeonGameModeInfestedHoardRoll()
	{
		const float Pinned = CVarInfestedHoardRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	float DungeonGameModeChaosTouchedRoll()
	{
		const float Pinned = CVarChaosTouchedRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	float DungeonGameModeDeadRisingRoll()
	{
		const float Pinned = CVarDeadRisingRoll.GetValueOnAnyThread();
		return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	}

	/**
	 * Which of the seven kinds a creature is, or `Count` when it is none of them.
	 *
	 * BY CLASS, BECAUSE A CREATURE DOES NOT CARRY ITS KIND. `ACataclysmDungeonGameMode::
	 * ClassFor` maps a kind to a class and nothing maps back, so this asks each kind in
	 * turn. The seven classes all derive straight from `ACataclysmEnemyCharacter` and
	 * from none of each other, so at most one answers -- a Blueprint made from one of
	 * them answers for that one.
	 *
	 * `Count` IS A REAL ANSWER AND NOT AN ERROR. The plain `ACataclysmEnemyCharacter`
	 * that automation tests spawn is not any kind, and neither is a creature class added
	 * to the game without being added to `ClassFor`. The caller decides what to do about
	 * it; Royal Guard calls no guards and says so in the log.
	 */
	ECataclysmDungeonCreature DungeonGameModeKindOf(const ACataclysmEnemyCharacter* Creature)
	{
		if (!Creature)
		{
			return ECataclysmDungeonCreature::Count;
		}
		const int32 Kinds = static_cast<int32>(ECataclysmDungeonCreature::Count);
		for (int32 Index = 0; Index < Kinds; ++Index)
		{
			const ECataclysmDungeonCreature Kind =
				static_cast<ECataclysmDungeonCreature>(Index);
			const TSubclassOf<ACataclysmEnemyCharacter> Class =
				ACataclysmDungeonGameMode::ClassFor(Kind);
			if (Class && Creature->IsA(Class))
			{
				return Kind;
			}
		}
		return ECataclysmDungeonCreature::Count;
	}

	/**
	 * The damage type a dungeon rule's damage carries: its row's `CataclysmType`.
	 * Issue #1924.
	 *
	 * NAME_None WHEN THE ROW CANNOT BE FOUND, which deals the damage untyped rather
	 * than refusing to deal it, so a table that failed to load changes what a blow
	 * is met by and not whether it lands.
	 */
	FName DungeonGameModeTypeOfRow(const TCHAR* RowKey)
	{
		const FCataclysmDungeonModifierRow* Row = UCataclysmDungeonModifierTable::FindRow(
			UCataclysmDungeonModifierTable::LoadDungeonModifierTable(), FName(RowKey));
		return Row ? FName(*Row->CataclysmType) : NAME_None;
	}

	/**
	 * Destroy every ground zone the floor's rules placed, and answer how many.
	 * Issue #1925.
	 *
	 * BY OWNER, WHICH IS WHAT MAKES THIS ONE PLACE. Every rule places its zones in
	 * the name of the floor's one `ACataclysmFloorHazardSource`, and
	 * `tools/tests/test_dungeon_modifier_rules_are_the_rows.py` holds every zone
	 * spawn in this file to that. So asking for the owner finds every rule's zones,
	 * a rule added later included, with no list of lists to keep up to date.
	 *
	 * A ZONE WITH ANY OTHER OWNER IS LEFT ALONE: a creature's burning ground and a
	 * player's skill belong to whatever placed them, not to the floor's rules.
	 *
	 * NOTHING TO DO WHEN THE WORLD HOLDS NO SOURCE, which is a floor no rule has
	 * placed anything on, and a new arena after `ClearTheFloor` has destroyed the
	 * source with the rest of the last floor. `Existing` never makes one.
	 *
	 * COLLECTED FIRST AND DESTROYED AFTER, the way `ClearTheFloor` does it, so the
	 * iteration never walks a world it is changing.
	 */
	int32 DungeonGameModeDestroyTheRulesZones(UWorld* World)
	{
		ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::Existing(World);
		if (!Source)
		{
			return 0;
		}

		TArray<ACataclysmGroundZone*> Doomed;
		for (TActorIterator<ACataclysmGroundZone> It(World); It; ++It)
		{
			if (IsValid(*It) && It->GetOwner() == Source)
			{
				Doomed.Add(*It);
			}
		}
		for (ACataclysmGroundZone* Zone : Doomed)
		{
			Zone->Destroy();
		}

		// AND WHO THEY LAST BURNED. Issue #2074. The source outlives the floor, so the record of
		// the last floor's burns would otherwise refuse a burn on this one's first second.
		Source->ForgetBurns();
		return Doomed.Num();
	}
}

void ACataclysmDungeonGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// EVERY FRAME, UNLIKE THE CHECK BELOW. Issue #1544. Putting a few of an
	// arriving wave's creatures down in each frame is the whole point, so this
	// cannot wait a quarter of a second between turns. On a floor with nothing
	// arriving it costs one test of an empty array.
	ContinueTheWaveArriving();

	// NOT EVERY FRAME. See `SecondsBetweenWaveChecks`: counting the wave walks
	// up to 350 creatures and the answer cannot change faster than a player can
	// kill one.
	SinceWaveCheckSeconds += DeltaSeconds;
	if (SinceWaveCheckSeconds < SecondsBetweenWaveChecks)
	{
		return;
	}

	// SET BACK TO ZERO RATHER THAN HAVING THE INTERVAL TAKEN OFF IT. A frame
	// long enough to cover several intervals should bring one wave in, not four.
	SinceWaveCheckSeconds = 0.0f;

	BringTheNextWaveIn();

	// AND THE THREE MODIFIERS THAT CHANGE WHILE THE PLAYER PLAYS. Issue #41,
	// slices 2 and 5. On this beat rather than a timer of their own, and after
	// the wave check because a wave arriving is what the player is looking at.
	//
	// A BEAT IS TAKEN TO BE A QUARTER OF A SECOND BY THE RULES BELOW, AND THE
	// LINE ABOVE IS WHY THAT IS NOT QUITE TRUE. Zeroing rather than subtracting
	// is right for the wave and means a frame longer than the interval yields
	// one beat however long it was, so below four frames a second those rules
	// run slow. It is in the player's favour, it does not bite at a playable
	// frame rate, and the fix belongs in the rules rather than here because the
	// wave must keep the behaviour the comment above defends. Issue #1613.
	StepFloorRulesThatChange();

	// AND THE FLOOR'S CLEAR TIME, WHATEVER THE RULES, so Trial of Endurance's time can be tuned from play
	// rather than from a guess. Issues #1820 and #41.
	NoteTheFloorsClearTime();
}

void ACataclysmDungeonGameMode::StartPlay()
{
	// THE SAME CALL TAKING THE STAIRS MAKES, so beginning play and going down a
	// floor cannot drift apart into two lists of steps in two orders. It builds
	// the floor, puts creatures on it and places the way down.
	//
	// BEFORE `Super::StartPlay`, and the order matters. The parent starts the
	// save writer, which records the floor being stood on, and a floor that does
	// not exist yet is one the record cannot describe.
	//
	// ITS LAST STEP, STANDING THE PLAYER AT THE ENTRANCE, DOES NOTHING HERE and
	// that is expected rather than a waste: there is no pawn until the parent has
	// run, so it is done again below.
	GoToFloor(ChooseFloorNumber());

	Super::StartPlay();

	// AND THE PLAYER IS MOVED AFTER, because the pawn is created during login,
	// which the parent's `StartPlay` is downstream of. Moved rather than spawned
	// there: the player start is wherever the map put it, and where the player
	// arrives is decided by the generator and is different every floor.
	if (const APlayerController* Controller =
			GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PlaceAtEntrance(Controller->GetPawn());
	}

	// AND THE FIRST FLOOR'S MODIFIERS REACH THE PLAYER, for the same reason the
	// move above is repeated: `GoToFloor` ran before there was a pawn to reach.
	// Issue #41.
	ApplyFloorRulesToPlayer();

	// AND A DEATH ANYWHERE ON THE FLOOR REACHES THE NIHIL'S EMBRACE'S CLEANSE.
	// Issue #41, slice 2.
	//
	// BOUND ONCE HERE RATHER THAN ASKED FOR ON THE BEAT, because a death is an
	// event: nothing on a character says one happened a moment ago.
	//
	// NOT UNBOUND, AND THAT IS SAFE. The subsystem belongs to the world, so it
	// cannot outlive this game mode, and a binding to a destroyed object is
	// skipped rather than called.
	if (UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(GetWorld()))
	{
		Events->OnDeath.AddUObject(
			this, &ACataclysmDungeonGameMode::OnSomethingDied);

		// AND A BLOW LANDING ANYWHERE ON THE FLOOR REACHES WASTING SICKNESS.
		// Issues #1786 and #41. Bound here for the same three reasons the death
		// binding gives: a blow is an event, the subsystem cannot outlive this
		// game mode, and a binding to a destroyed object is skipped.
		//
		// THIS IS THE FIRST PRODUCTION LISTENER ON THAT ANNOUNCEMENT. It has been
		// broadcast on every blow since slice 4 and only tests listened.
		Events->OnHit.AddUObject(
			this, &ACataclysmDungeonGameMode::OnSomethingWasHit);

		// AND A DROP TAKEN ANYWHERE REACHES TRICK OR TREAT, bound for the same three reasons.
		// Issues #1820 and #41.
		Events->OnLootTaken.AddUObject(this, &ACataclysmDungeonGameMode::OnLootTaken);
	}
}

int32 ACataclysmDungeonGameMode::ChooseSeed(int64 Entropy) const
{
	if (GCataclysmDungeonSeedOverride > 0)
	{
		return GCataclysmDungeonSeedOverride;
	}

	if (GCataclysmDungeonSeedOverride < 0)
	{
		// A NEW DUNGEON EVERY TIME PLAY BEGINS. The clock is read here and
		// nowhere inside the generator, which stays deterministic: the same seed
		// still gives the same floor. Two numbers rather than one because a
		// tick count alone changes slowly enough that two runs started in the
		// same millisecond would walk the same dungeon.
		const int64 Rolled = (Entropy != 0)
			? Entropy
			: (FDateTime::Now().GetTicks() ^ static_cast<int64>(FPlatformTime::Cycles64()));

		return FCataclysmFloorGenerator::SeedForFloor(
			static_cast<int32>(Rolled), static_cast<int32>(Rolled >> 32));
	}

	return DungeonSeed;
}

int32 ACataclysmDungeonGameMode::ChooseFloorNumber() const
{
	return FMath::Max(1, (GCataclysmDungeonFloorOverride > 0)
		? GCataclysmDungeonFloorOverride : FloorNumber);
}

int32 ACataclysmDungeonGameMode::ChooseTotalFloors() const
{
	const int32 Asked = FMath::Max(1, (GCataclysmDungeonFloorCountOverride > 0)
		? GCataclysmDungeonFloorCountOverride : TotalFloors);

	// NEVER BELOW THE FLOOR BEING WALKED. The stairs descend for ever -- there
	// is no bottom to a dungeon until issue #41 -- so a player can stand on
	// floor 40 of a dungeon whose length says 10. Enemy Score divides the two to
	// get a floor ratio, and a ratio above one is outside anything the model was
	// fitted for: at tier 8 it would make an ordinary creature on floor 40 worth
	// more than a Cataclysm Boss on the last floor. Answering with the deeper of
	// the two treats a player who has walked past the end as being at the end,
	// which is the honest reading of a length nothing enforces.
	return FMath::Max(Asked, ChooseFloorNumber());
}

ECataclysmFloorLayout ACataclysmDungeonGameMode::ChooseLayout() const
{
	// CLAMPED RATHER THAN TRUSTED. The value is typed by hand at a console, and
	// casting 40 to this enum would carve nothing and place the player nowhere.
	if (GCataclysmDungeonLayoutOverride >= 0
		&& GCataclysmDungeonLayoutOverride < static_cast<int32>(ECataclysmFloorLayout::Count))
	{
		return static_cast<ECataclysmFloorLayout>(GCataclysmDungeonLayoutOverride);
	}

	return Layout;
}

ECataclysmDungeonSubType ACataclysmDungeonGameMode::ChooseSubType() const
{
	// NAMED RATHER THAN CLAMPED, unlike the layout control above. A number out
	// of range has to be caught here because casting it would carve nothing; a
	// name that is not a sub-type cannot become one in the first place, so the
	// lookup answers with nothing set and the setting decides.
	const TOptional<ECataclysmDungeonSubType> Asked =
		DungeonGameModeSubTypeAskedFor();
	if (Asked.IsSet())
	{
		return Asked.GetValue();
	}

	return DungeonSubType;
}

float ACataclysmDungeonGameMode::ChooseEnemyScale() const
{
	if (GCataclysmDungeonEnemyScaleOverride >= 0.0f)
	{
		return GCataclysmDungeonEnemyScaleOverride;
	}

	// CLAMPED RATHER THAN TRUSTED, the same as the layout control. The setting
	// carries `ClampMin` in the editor and a Blueprint default set before that
	// meta was added would not be re-clamped, so a negative here would silently
	// mean "read the console variable" a second time.
	return FMath::Max(0.0f, EnemyScale);
}

FCataclysmDungeonIdentity ACataclysmDungeonGameMode::DungeonIdentity() const
{
	FCataclysmDungeonIdentity Dungeon;
	Dungeon.DungeonSeed = ChooseSeed();
	Dungeon.TotalFloors = ChooseTotalFloors();
	Dungeon.SubType = ChooseSubType();
	Dungeon.Layout = ChooseLayout();
	Dungeon.DifficultyTier = DifficultyTierFor(this);
	// THE CONSOLE'S MODIFIERS WHEN IT NAMES ANY, AND THE DUNGEON'S OWN OTHERWISE.
	// See `ChooseModifiers`. Everything downstream -- a Volatile re-draw, the
	// Unstable Dimensions extra, the enemy score -- treats the two the same.
	Dungeon.Modifiers = ChooseModifiers(Dungeon.ModifierScore);
	Dungeon.ModifierPool = DungeonModifierPool;
	return Dungeon;
}

ACataclysmDungeonFloor* ACataclysmDungeonGameMode::BuildFloor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (!CurrentFloor)
	{
		CurrentFloor = World->SpawnActor<ACataclysmDungeonFloor>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	}
	if (!CurrentFloor)
	{
		return nullptr;
	}

	// WHAT THIS FLOOR OF THIS DUNGEON IS, DECIDED ONCE. `BuildFloor` takes the
	// layout out of it, `PopulateFloor` takes the boss and the wave, and
	// `RunModifierScore` takes the modifier score. Deciding it here rather than
	// in `GoToFloor` is what keeps this function callable on its own, which is
	// what every test of the geometry does. Issue #41.
	FloorBrief = FCataclysmDungeonFloorRules::BriefFor(
		DungeonIdentity(), ChooseFloorNumber());

	// **AND A HORDE DUNGEON KEEPS THE ARENA IT ALREADY CARVED.** Its floors are
	// waves into one open space, so floor 2 is floor 1's geometry with the next
	// wave walking into it. Re-generating would give the same cells back --
	// nothing about the arena depends on the floor number for a Horde dungeon --
	// but it would rebuild the floor actor's meshes underneath a player who is
	// standing on them, and it would move the entrance and the exit for no
	// reason. The brief above is still decided first, because the floor number
	// changed even though the floor did not.
	if (FloorBrief.bSameArenaAsLastFloor && CurrentFloor->IsBuilt())
	{
		return CurrentFloor;
	}

	FCataclysmFloorRequest Request;
	Request.DungeonSeed = ChooseSeed();

	// THE BRIEF'S FLOOR NUMBER AND NOT `ChooseFloorNumber`. They are the same
	// number for every dungeon but a Horde one, whose every floor is carved as
	// floor 1 because every floor of it is the same arena. Without this a save
	// taken on wave 5 and loaded back would build floor 5's shape and put the
	// player in an arena they had never been in.
	Request.FloorNumber = FloorBrief.CarvedAsFloorNumber;

	// THE BRIEF'S LAYOUT AND NOT `ChooseLayout`, which the brief has already
	// read. A Horde dungeon is one open space on every floor and every other
	// dungeon is carved by whatever the setting or the console variable said.
	Request.Layout = FloorBrief.Layout;

	if (!CurrentFloor->Build(FCataclysmFloorGenerator::Generate(Request)))
	{
		return nullptr;
	}

	return CurrentFloor;
}

bool ACataclysmDungeonGameMode::PlaceAtEntrance(APawn* Pawn)
{
	if (!Pawn || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return false;
	}

	const FVector Standing = CurrentFloor->EntranceWorld()
		+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOf(Pawn));

	// SWEEP OFF. The pawn is being put somewhere it is not, across a floor that
	// may be a hundred metres away, and a swept move would stop at the first wall
	// between here and there and leave the player inside it.
	return Pawn->TeleportTo(Standing, Pawn->GetActorRotation(),
						    /*bIsATest=*/false, /*bNoCheck=*/true);
}

// ---------------------------------------------------------------------------
// Putting creatures on the floor
// ---------------------------------------------------------------------------

TSubclassOf<ACataclysmEnemyCharacter> ACataclysmDungeonGameMode::ClassFor(
	ECataclysmDungeonCreature Creature)
{
	switch (Creature)
	{
	case ECataclysmDungeonCreature::Imp:
		return ACataclysmImpCharacter::StaticClass();
	case ECataclysmDungeonCreature::Hellhound:
		return ACataclysmHellhoundCharacter::StaticClass();
	case ECataclysmDungeonCreature::Brute:
		return ACataclysmBruteCharacter::StaticClass();
	case ECataclysmDungeonCreature::AbyssalWarden:
		return ACataclysmAbyssalWardenCharacter::StaticClass();
	case ECataclysmDungeonCreature::CorruptedSentinel:
		return ACataclysmCorruptedSentinelCharacter::StaticClass();
	case ECataclysmDungeonCreature::Succubus:
		return ACataclysmSuccubusCharacter::StaticClass();
	case ECataclysmDungeonCreature::Gatekeeper:
		return ACataclysmGatekeeperCharacter::StaticClass();
	default:
		// NOT A FALLBACK TO SOMETHING SPAWNABLE, deliberately. A creature added
		// to the enum and forgotten here should show up as a creature that never
		// appears, which a test can see, rather than as a floor quietly full of
		// Imps, which nothing can.
		return nullptr;
	}
}

void ACataclysmDungeonGameMode::ApplyDesignedStats(
	ACataclysmEnemyCharacter* Enemy, ECataclysmDungeonCreature Creature,
	int32 FixedRung) const
{
	// A FIXED RUNG WINS OVER EVERY KIND'S OWN SETTING. See `SpawnPlacedCreature`: only
	// Divine Resurgence passes one, and `RarityStepFor` returns any setting that is not
	// `RollTheRarity` unchanged.
	const auto Setting = [FixedRung](int32 KindsOwn)
	{
		return FixedRung >= 0 ? FixedRung : KindsOwn;
	};

	if (!Enemy)
	{
		return;
	}

	switch (Creature)
	{
	case ECataclysmDungeonCreature::Imp:
		Enemy->SetHealth(ImpHealth);
		Enemy->SetAttackDamage(ImpAttackDamage);
		// NO SetArmour CALL. See the header: this creature's designed armour
		// share is exactly zero and it is the only one in the roster with none.
		Enemy->SetRarityStep(RarityStepFor(Setting(ImpRarityStep), Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::Hellhound:
		Enemy->SetHealth(HellhoundHealth);
		Enemy->SetArmour(HellhoundArmour);
		Enemy->SetAttackDamage(HellhoundAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(Setting(HellhoundRarityStep), Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::Brute:
		Enemy->SetHealth(BruteHealth);
		Enemy->SetArmour(BruteArmour);
		Enemy->SetAttackDamage(BruteAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(Setting(BruteRarityStep), Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::AbyssalWarden:
		Enemy->SetHealth(AbyssalWardenHealth);
		Enemy->SetArmour(AbyssalWardenArmour);
		Enemy->SetAttackDamage(AbyssalWardenAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(Setting(AbyssalWardenRarityStep), Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::CorruptedSentinel:
		Enemy->SetHealth(CorruptedSentinelHealth);
		Enemy->SetArmour(CorruptedSentinelArmour);
		Enemy->SetAttackDamage(CorruptedSentinelAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(Setting(CorruptedSentinelRarityStep), Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::Succubus:
		Enemy->SetHealth(SuccubusHealth);
		Enemy->SetArmour(SuccubusArmour);
		Enemy->SetAttackDamage(SuccubusAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(Setting(SuccubusRarityStep), Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	// THE BOSS, AND ITS NUMBERS COME FROM THE SAME PLACE THE SANDBOX'S DOES.
	// `ACataclysmGameMode::GatekeeperHealth` and the two beside it are what
	// `SpawnGatekeepers` already uses, so a Gatekeeper at the bottom of a
	// dungeon and one in the sandbox are the same creature. Issue #41.
	case ECataclysmDungeonCreature::Gatekeeper:
		Enemy->SetHealth(GatekeeperHealth);
		Enemy->SetArmour(GatekeeperArmour);
		Enemy->SetAttackDamage(GatekeeperAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(Setting(GatekeeperRarityStep), Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	default:
		break;
	}
}

void ACataclysmDungeonGameMode::ClearFloorEnemies()
{
	for (ACataclysmEnemyCharacter* Enemy : FloorEnemies)
	{
		// ALREADY GONE IS THE ORDINARY CASE, not an error: the player kills
		// creatures, and a killed one destroys itself once its death animation
		// has played.
		if (IsValid(Enemy))
		{
			// DESTROYING THE PAWN DESTROYS ITS BRAIN TOO. `APawn::Destroyed`
			// detaches the controller, and `AController::PawnPendingDestroy`
			// destroys any controller with no player state, which every AI
			// controller here is. Doing it by hand as well would be destroying
			// an actor twice.
			Enemy->Destroy();
		}
	}

	FloorEnemies.Reset();

	// AND THE WAVE WITH THEM, because every creature in it was one of those.
	// Left behind it would be a list of destroyed actors, and a wave that had
	// been cleared away rather than killed would read as one still standing.
	CurrentWave.Reset();
	WaveSpawned = 0;

	// AND WHATEVER OF IT HAD NOT ARRIVED YET. Issue #1544. Those creatures were
	// placed on the floor that is being cleared away, and putting them down
	// afterwards would stand them on whatever replaces it.
	WaveStillToArrive.Reset();
}

int32 ACataclysmDungeonGameMode::PopulateFloor()
{
	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return 0;
	}

	// FIRST, because this is called again every time the floor is replaced.
	//
	// **UNLESS THE FLOOR IS NOT BEING REPLACED.** A Horde dungeon's later floors
	// are waves into the arena the first floor carved, so the creatures already
	// standing there are the part of the last wave the player did not finish.
	// Clearing them would delete the enemies the owner's rule deliberately
	// leaves alive -- the next wave arrives at "10% or less remaining", and the
	// remainder is meant to still be fighting.
	// MARCH OF PROGRESS FORGETS THE LAST FLOOR'S COMMANDER FIRST, AND THIS IS THE ONLY
	// PLACE IT IS FORGOTTEN. Issues #1820 and #41. Here rather than in
	// `ApplyFloorRulesToPlayer` with the rest of that rule's per-floor state, because
	// `GoToFloor` calls this function and then that one: a Commander cleared there would
	// be the one chosen at the bottom of this function, and every floor would have none.
	//
	// BEFORE THE BRANCH BELOW AND NOT INSIDE IT, so a Horde dungeon's next wave chooses
	// its own Commander. That wave is a level, the row pays for "the Commander in each
	// level", and the survivors of the last wave are candidates for this one exactly as
	// the creatures walking in are.
	//
	// THE RUN'S COUNT OF COMMANDERS KILLED IS NOT TOUCHED. That is what the row pays and
	// nothing in the row takes it back; `LeaveEmpireDungeon` is where it ends.
	MarchOfProgressCommander = nullptr;
	bMarchOfProgressCommanderSlain = false;

	// AND EVERY FLOOR OR WAVE STARTS ITS CLEAR CLOCK AND TRIAL OF ENDURANCE AGAIN, here for the
	// Commander's reason: once a floor or wave, before the branch below. The last floor's creatures are
	// gone, so what the trial wrote on them goes with them. Issues #1820 and #41.
	FloorSecondsSincePlaced = 0.0f;
	FloorClearedSeconds = -1.0f;
	TrialSeconds = 0.0f;
	bTrialClearedInTime = false;
	bTrialRanOut = false;
	ForgetRuleResistances();
	TrialPanelLiving = -1;

	// AND PLAGUE HARBINGERS FORGETS THE LAST FLOOR'S OR WAVE'S HARBINGERS, here for the
	// Commander's reason: they are chosen at the bottom of this function, and
	// `ApplyFloorRulesToPlayer` runs after it. A Horde wave's survivors stop being
	// Harbingers; their trails go with the wave's other rule zones. Issues #1820 and #41.
	ForgetThePlagueHarbingers();

	if (!FloorBrief.bSameArenaAsLastFloor)
	{
		ClearFloorEnemies();

		// AND ETERNAL CHORUS'S SOURCES GO WITH THE ARENA AND A NEW ARENA GETS ITS OWN, here and
		// not with the per-floor resets: a Horde dungeon's later waves keep the first wave's sources,
		// as ruled, and `ApplyFloorRulesToPlayer` runs on every wave. The sources are not in
		// `FloorEnemies`, so the call above does not take them. Issues #1820 and #41.
		ForgetTheChoruses();
		PlaceTheChoruses();

		// AND NECROTIC BLOOM'S FLOWERS, FOR THE SAME REASON: placed once where an arena is placed, and
		// kept by a Horde dungeon's later waves. Issues #1820 and #41.
		ForgetTheBlooms();
		PlaceTheBlooms();

		// AND GOLDEN SPIRES, FOR THE SAME REASON. Issues #1820 and #41.
		ForgetTheSpires();
		PlaceTheSpires();

		// AND PESTILENT EMPOWERMENT'S BEACONS, FOR THE SAME REASON. The last floor's were counted in
		// `GoToFloor` before its creatures were cleared. Issues #1820 and #41.
		ForgetTheBeacons();
		PlaceTheBeacons();

		// AND PORTAL UNLEASHING, FOR THE SAME REASON; a Horde arena's waves keep its portal and what it sent.
		// Issues #1820 and #41.
		ForgetThePortals();
		PlaceThePortals();

		// AND ABYSSAL RIFTS, FOR THE SAME REASON: a new floor's rift, none on a Horde arena. The successes are the
		// dungeon's and are not touched. Issues #1820 and #41.
		ForgetTheRift();
		PlaceTheRift();

		// AND SWARM OF LOCUSTS' SHELTERS AND ANY SWARM, FOR THE SAME REASON; a Horde arena's waves keep its shelter.
		// Issues #1820 and #41.
		ForgetTheLocusts();
		PlaceTheShelters();

		// AND RAW SEWAGE'S RIVERS, FOR THE SAME REASON; a Horde arena's waves keep its river. The stacks are the
		// dungeon's and are not touched here. Issues #1820 and #41.
		ForgetTheRivers();
		PlaceTheRivers();

		// AND INFESTED VEINS, FOR THE SAME REASON; a new arena starts its destroyed count again, and a
		// Horde arena's waves keep it. Issues #1820 and #41.
		ForgetTheVeins();
		PlaceTheVeins();

		// AND VOID PARASITE: a new arena's voidlings, light and stacks start again, and a Horde arena's
		// waves keep them. Issues #1820 and #41.
		ForgetTheVoidParasite();
		PlaceTheLight();

		// AND OBSIDIAN SARCOPHAGI, FOR THE SAME REASON; a Horde arena's waves keep its coffin and its count.
		// Issues #1820 and #41.
		ForgetTheSarcophagi();
		PlaceTheSarcophagi();
	}
	else
	{
		// A WAVE STILL ARRIVING FINISHES ARRIVING BEFORE THE NEXT ONE BEGINS.
		// Issue #1544. In play this cannot happen, because
		// `ShouldTheNextWaveArrive` refuses a wave that has not all arrived. But
		// `GoToFloor` can be called directly, which a test does, and the
		// creatures still waiting belong to a wave the population pass has
		// already decided. Dropping them would change how many that wave
		// brought.
		while (!WaveStillToArrive.IsEmpty())
		{
			ContinueTheWaveArriving();
		}
	}

	// THE BRIEF IS WHAT MAKES A DUNGEON'S SUB-TYPE REACH ITS CREATURES. It puts
	// a Gatekeeper on the exit of a boss floor and gathers a Horde dungeon's
	// creatures into one wave. `BuildFloor` decided it; this only spends it.
	const FCataclysmFloorPopulation Population = FCataclysmFloorPopulator::Populate(
		CurrentFloor->GetPlan(), ChooseEnemyScale(), FloorBrief);

	// THIS WAVE'S OWN CREATURES, EMPTIED BEFORE IT ARRIVES. What is left of the
	// wave before stays in `FloorEnemies` and stops being counted here, which is
	// what makes "10% or less of the previous wave" a question about one wave.
	CurrentWave.Reset();

	// WHAT THE WAVE HAS PUT ON THE FLOOR, WHICH ONCE ALL OF IT HAS ARRIVED IS THE
	// DENOMINATOR OF THE OWNER'S RULE. Counted up as creatures arrive rather than
	// set once, because since issue #1544 a wave arrives over several frames;
	// `ShouldTheNextWaveArrive` does not judge a wave until all of it has
	// arrived, so the rule only ever reads the finished count. Recorded even on a
	// floor that is not a wave, because a floor that stops being one has to stop
	// carrying the last one's count.
	WaveSpawned = 0;

	int32 Spawned = 0;
	if (FloorBrief.bWaveWalksIn)
	{
		// A WAVE THAT WALKS IN ARRIVES A FEW CREATURES A FRAME. Issue #1544; see
		// `WaveCreaturesPerFrame`. Every creature and its cell are decided now,
		// as before. The first few are put down in this frame and `Tick` puts
		// down the rest.
		WaveStillToArrive = Population.Enemies;
		ArrivingSightRadiusMultiplier = FloorBrief.SightRadiusMultiplier;
		Spawned = ContinueTheWaveArriving();
	}
	else
	{
		// AN ORDINARY FLOOR'S CREATURES ARE PUT DOWN ALL AT ONCE, while the floor
		// is being built, as they always were.
		for (const FCataclysmEnemyPlacement& Placement : Population.Enemies)
		{
			if (ACataclysmEnemyCharacter* Enemy =
					SpawnPlacedCreature(Placement, FloorBrief.SightRadiusMultiplier))
			{
				FloorEnemies.Add(Enemy);
				CurrentWave.Add(Enemy);
				++Spawned;
			}
		}
		WaveSpawned = Spawned;

		// AND ONE OF THEM IS THE FLOOR'S MEDIC, if the floor carries that rule.
		// After the loop rather than inside it, because the choice is the
		// rarest creature and rarity is not known until each one has spawned.
		ChooseTheFloorsMedic();

		// AND ONE OF THEM IS THE FLOOR'S COMMANDER, if the floor carries THAT rule.
		// Issues #1820 and #41. Here for the reason directly above: the choice is the
		// highest rung and no rung is known until every creature has spawned. The two
		// may be the same creature, which nothing forbids -- neither rule reads what the
		// other wrote.
		ChooseTheFloorsCommander();

		// AND THE FLOOR'S PLAGUE HARBINGERS, for the same reason. Issues #1820 and #41.
		ChooseThePlagueHarbingers();
	}

	// AND WHICH WAVE OF THIS ARENA IT IS. Zero on a floor that is not a wave,
	// which is most floors in the game: an ordinary dungeon's creatures are not
	// a wave and counting them as one would make the figure mean two things.
	WavesArrived = FloorBrief.bWaveWalksIn
		? (FloorBrief.bSameArenaAsLastFloor ? WavesArrived + 1 : 1)
		: 0;

	// AND THE CLOCK STARTS AGAIN. Without this a wave that arrived a fraction
	// before the next check would be looked at almost immediately, and a wave
	// that spawned few enough creatures for its threshold to be zero could be
	// judged finished before the player had swung at it.
	SinceWaveCheckSeconds = 0.0f;

	// WHAT THE WHOLE WAVE WILL BE ONCE IT HAS ARRIVED. The same as `Spawned` on
	// an ordinary floor, which puts everything down at once.
	const int32 StillToArrive = WaveStillToArrive.Num();
	const int32 Arriving = Spawned + StillToArrive;

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d creatures on the dungeon floor now and %d more arrive over "
			 "the next %d frames, %d in all, in %d groups: %d Imps, %d "
			 "Hellhounds, %d Brutes, %d Abyssal Wardens, %d Corrupted Sentinels, "
			 "%d Succubi and %d Gatekeepers. The floor has %d walkable cells and "
			 "the density asked for %d. No creature stands within %d cells of "
			 "where the player arrives. It is %s, and its modifiers are worth "
			 "%.1f. It is wave %d of this arena, it notices from %.1f times the "
			 "ordinary distance, and the next wave arrives at %d still alive."),
		Spawned, StillToArrive,
		FMath::DivideAndRoundUp(StillToArrive, WaveCreaturesPerFrame),
		Arriving, Population.PackCount,
		Population.HowMany(ECataclysmDungeonCreature::Imp),
		Population.HowMany(ECataclysmDungeonCreature::Hellhound),
		Population.HowMany(ECataclysmDungeonCreature::Brute),
		Population.HowMany(ECataclysmDungeonCreature::AbyssalWarden),
		Population.HowMany(ECataclysmDungeonCreature::CorruptedSentinel),
		Population.HowMany(ECataclysmDungeonCreature::Succubus),
		Population.HowMany(ECataclysmDungeonCreature::Gatekeeper),
		CurrentFloor->GetPlan().FloorCount(), Population.Wanted,
		FCataclysmFloorPopulator::LeastCellsFromEntrance,
		FloorBrief.bWaveWalksIn
			? TEXT("one wave arriving around the outside")
			: (FloorBrief.bOneWave
				? TEXT("one wave gathered at the far end")
				: TEXT("separate encounters spread over the floor")),
		FloorBrief.ModifierScore, WavesArrived, FloorBrief.SightRadiusMultiplier,
		FCataclysmDungeonFloorRules::NextWaveArrivesAtOrBelow(Arriving));

	return Spawned;
}

ACataclysmEnemyCharacter* ACataclysmDungeonGameMode::SpawnPlacedCreature(
	const FCataclysmEnemyPlacement& Placement, float SightRadiusMultiplier,
	int32 FixedRung)
{
	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return nullptr;
	}

	const TSubclassOf<ACataclysmEnemyCharacter> Class = ClassFor(Placement.Creature);
	if (!Class)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// RAISED BY ITS OWN CAPSULE'S HALF HEIGHT, read from the class rather
	// than assumed, because the six creatures placed here range from 87.95
	// to 114 cm and none of them is the base enemy's 80. Putting a capsule's
	// middle on the walking surface buries its lower half in the ground,
	// which is the same fault `PlaceAtEntrance` above exists to avoid for the
	// player.
	const FVector Where = CurrentFloor->WorldOfCell(Placement.Cell)
		+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));

	// FACING THE WAY THE PLAYER WILL COME FROM, flattened so nothing leans
	// back to look up a slope. It costs nothing and it means a group reads as
	// waiting rather than as six creatures pointing in six directions.
	FVector Toward = CurrentFloor->EntranceWorld() - Where;
	Toward.Z = 0.0f;
	const FRotator Facing = Toward.IsNearlyZero()
		? FRotator::ZeroRotator : Toward.Rotation();

	ACataclysmEnemyCharacter* Enemy = World->SpawnActor<ACataclysmEnemyCharacter>(
		Class, Where, Facing, SpawnParams);
	if (!Enemy)
	{
		return nullptr;
	}

	ApplyDesignedStats(Enemy, Placement.Creature, FixedRung);

	// AND SOME OF THEM ARE ILLUSIONS. `Chaos_Illusory_Enemies`. Issues #1820 and
	// #41. This is the only rule in this file that changes a creature as it is
	// placed rather than acting on the player, on the floor, or on a beat.
	//
	// AFTER `ApplyDesignedStats` AND NOT BEFORE, which reads as an ordering
	// dependency and is NOT one. `SetIsAnIllusion` writes a flag that
	// `ACataclysmEnemyCharacter::ApplyStartingAttributes` honours on every later
	// recompute, so the two calls are safe in either order; its own declaration
	// says why the flag exists rather than a zero written here. It is placed
	// after so that a reader sees the creature fully made and then made an
	// illusion, which is the order the row describes.
	//
	// EVERY CREATURE IS ASKED, INCLUDING THE GATEKEEPER. The row says "Some
	// enemies" and names no exception, and a boss that turns out to be harmless
	// is the strongest form of what the row is for. If a later ruling exempts
	// bosses, the exemption goes here and says so.
	if (FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::IllusoryEnemiesKey))
		&& UCataclysmDungeonModifierEffects::IllusoryEnemiesIsAnIllusion(
			DungeonGameModeIllusoryEnemiesRoll()))
	{
		Enemy->SetIsAnIllusion(true);
	}

	// PLACED AGAIN NOW ITS SIZE IS KNOWN. `Where` above was raised by the
	// half height of the CLASS, which is the creature at Common. Since
	// issue #849 a rarer creature is bigger, and ApplyDesignedStats is what
	// decides its rarity -- so until it has run there is no way to know how
	// far to raise it. A capsule grows from its middle, so getting this
	// wrong buries a Boss 4.6 metres into the floor, which is what
	// Cataclysm.DungeonMode.ItPutsCreaturesOnTheFloorAndNotInsideIt found.
	//
	// HERE RATHER THAN IN SetRarityStep, because that setter cannot tell a
	// creature being placed from one being restored from a save, whose
	// height already accounts for its size. Its own comment says so.
	Enemy->SetActorLocation(CurrentFloor->WorldOfCell(Placement.Cell)
		+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOf(Enemy)));

	// AND WHAT THIS FLOOR LETS IT NOTICE FROM. One everywhere but a Horde
	// dungeon, where it is what makes the wave run at the player from the
	// far side of the arena instead of standing where it spawned.
	//
	// SET HERE AND NOT ON THE CREATURE'S OWN DEFAULT, which is the whole
	// reason it is a multiplier: an Imp in an ordinary dungeon is the same
	// Imp it always was, and nothing about the class has changed.
	//
	// PASSED IN RATHER THAN READ OFF `FloorBrief`, so a wave still arriving
	// when a new floor's brief is written keeps the figure it began with. See
	// `ArrivingSightRadiusMultiplier`.
	Enemy->SightRadiusMultiplier = SightRadiusMultiplier;

	return Enemy;
}

void ACataclysmDungeonGameMode::ChooseTheFloorsMedic()
{
	// ONLY A FLOOR CARRYING THE RULE HAS A MEDIC. `FloorBrief.Modifiers` is
	// the floor's own list rather than the dungeon's, so a Volatile dungeon
	// that draws Field Medic onto one floor puts a medic on that floor only.
	if (!FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::FieldMedicKey)))
	{
		return;
	}

	ACataclysmEnemyCharacter* Rarest = nullptr;
	for (ACataclysmEnemyCharacter* Enemy : FloorEnemies)
	{
		if (!IsValid(Enemy))
		{
			continue;
		}

		if (Enemy->bHealsAlliesForTheFloorRule)
		{
			// THE FLOOR ALREADY HAS ONE AND IT IS STILL HERE. A second would
			// heal alongside the first, which the row does not ask for.
			return;
		}

		if (Rarest == nullptr || Enemy->RarityStep > Rarest->RarityStep)
		{
			// STRICTLY GREATER, so a tie keeps the earlier creature and the
			// choice does not depend on how the list happens to be ordered
			// beyond the order the population pass placed them in.
			Rarest = Enemy;
		}
	}

	if (Rarest == nullptr)
	{
		// AN EMPTY FLOOR HAS NO MEDIC, rather than the rule failing.
		return;
	}

	Rarest->bHealsAlliesForTheFloorRule = true;

	UE_LOG(LogCataclysm, Verbose,
		TEXT("%s is this floor's medic, at rarity step %d."),
		*Rarest->GetName(), Rarest->RarityStep);
}

ACataclysmEnemyCharacter* ACataclysmDungeonGameMode::TheFloorsCommander() const
{
	return MarchOfProgressCommander.Get();
}

bool ACataclysmDungeonGameMode::IsTheFloorsCommander(const AActor* Creature) const
{
	// THREE QUESTIONS, BECAUSE THE WEAK POINTER ALONE ANSWERS WRONGLY FOR A CORPSE. A
	// killed creature's weak pointer stays valid until its body is removed, so the floor's
	// slain flag and the creature's own dead mark are asked too. Issue #1997.
	const ACataclysmEnemyCharacter* Chosen = MarchOfProgressCommander.Get();
	return Creature && Chosen && Creature == Chosen && !bMarchOfProgressCommanderSlain
		&& !UCataclysmSkillEffects::IsDead(Chosen);
}

bool ACataclysmDungeonGameMode::IsTheFloorsCommanderIn(const UObject* WorldContext,
														const AActor* Creature)
{
	// THE HOP NO AUTOMATION TEST CAN TAKE. See the declaration.
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return false;
	}

	const ACataclysmDungeonGameMode* Mode =
		World->GetAuthGameMode<ACataclysmDungeonGameMode>();
	return Mode && Mode->IsTheFloorsCommander(Creature);
}

void ACataclysmDungeonGameMode::ChooseTheFloorsCommander()
{
	// ONLY A FLOOR CARRYING THE RULE HAS A COMMANDER, the same test and for the same
	// reason `ChooseTheFloorsMedic` above makes it.
	if (!FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::MarchOfProgressKey)))
	{
		return;
	}

	// ONE COMMANDER AT A TIME, AND ONE PAYMENT A FLOOR. A Horde dungeon's next wave
	// arrives in the arena the player is standing in and calls this again, so both
	// questions have to be asked: a Commander is still alive, or this floor's has already
	// been killed and paid for. The row pays for "the Commander in each level" -- one of
	// them. `PopulateFloor` forgets both before it places a new floor's creatures.
	if (MarchOfProgressCommander.IsValid() || bMarchOfProgressCommanderSlain)
	{
		return;
	}

	ACataclysmEnemyCharacter* Highest = nullptr;
	for (ACataclysmEnemyCharacter* Enemy : FloorEnemies)
	{
		if (!IsValid(Enemy))
		{
			continue;
		}

		if (Highest == nullptr || Enemy->RarityStep > Highest->RarityStep)
		{
			// STRICTLY GREATER, so a tie keeps the earlier creature, which is the rule
			// the medic chooser above already follows and for its stated reason.
			Highest = Enemy;
		}
	}

	if (Highest == nullptr)
	{
		// AN EMPTY FLOOR HAS NO COMMANDER, and the rule does not fail. The floor's
		// creatures still hit harder for its depth -- there are none -- and there is no
		// armour to earn here. The panel says "no Commander".
		return;
	}

	MarchOfProgressCommander = Highest;

	// NOTHING IS WRITTEN ON THE CREATURE. It is an ordinary creature of its kind and
	// rung, and this log line and the floor panel are the only places the choice shows.
	// Issue #1997 is what a player would need in order to pick it out in play.
	UE_LOG(LogCataclysm, Verbose,
		TEXT("%s is this floor's Commander, at rarity step %d."),
		*Highest->GetName(), Highest->RarityStep);
}

// ---------------------------------------------------------------------------
// Waves, for a Horde dungeon. Issue #1467
// ---------------------------------------------------------------------------

int32 ACataclysmDungeonGameMode::ContinueTheWaveArriving()
{
	if (WaveStillToArrive.IsEmpty())
	{
		return 0;
	}

	// NO FLOOR TO STAND THEM ON, SO NONE OF THEM ARRIVE. The queue is emptied
	// rather than kept, which is what lets a loop that calls this until nothing
	// is left always end.
	if (!GetWorld() || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		WaveStillToArrive.Reset();
		return 0;
	}

	const int32 ThisFrame = FMath::Min(WaveCreaturesPerFrame, WaveStillToArrive.Num());
	int32 Arrived = 0;
	for (int32 Index = 0; Index < ThisFrame; ++Index)
	{
		// ONE THAT FAILS TO SPAWN IS DROPPED RATHER THAN TRIED AGAIN, which is
		// what putting a whole floor down at once has always done with one.
		if (ACataclysmEnemyCharacter* Enemy = SpawnPlacedCreature(
				WaveStillToArrive[Index], ArrivingSightRadiusMultiplier))
		{
			FloorEnemies.Add(Enemy);
			CurrentWave.Add(Enemy);
			++WaveSpawned;
			++Arrived;
		}
	}

	// FROM THE FRONT, so creatures arrive in the order the population pass
	// placed them.
	WaveStillToArrive.RemoveAt(0, ThisFrame, EAllowShrinking::No);

	if (WaveStillToArrive.IsEmpty())
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("The arriving wave is all on the floor: %d creatures."),
			WaveSpawned);

		// NOW AND NOT EARLIER, for the reason the ordinary route waits too: the
		// choice is the rarest creature, and a wave that is still arriving may
		// yet bring a rarer one.
		ChooseTheFloorsMedic();

		// AND THE COMMANDER WITH IT, for the same reason: a wave still arriving may yet
		// bring a creature of a higher rung. Issues #1820 and #41.
		ChooseTheFloorsCommander();

		// AND THIS WAVE'S PLAGUE HARBINGERS, chosen per wave as ruled. Issues #1820 and #41.
		ChooseThePlagueHarbingers();
	}

	return Arrived;
}

int32 ACataclysmDungeonGameMode::WaveStillAlive() const
{
	int32 Alive = 0;
	for (const ACataclysmEnemyCharacter* Enemy : CurrentWave)
	{
		// TWO WAYS TO BE GONE AND BOTH COUNT. A creature destroys itself once
		// its death animation has played, so it stops being valid; between the
		// killing blow and that moment it is still a valid actor with no health
		// left. Counting only the first would hold the next wave back for the
		// length of a death animation, and the wave's last few would each add
		// their own wait.
		if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy))
		{
			++Alive;
		}
	}
	return Alive;
}

bool ACataclysmDungeonGameMode::ShouldTheNextWaveArrive() const
{
	// AN ORDINARY DUNGEON NEVER REACHES THE REST OF THIS. Its floors are not
	// waves, so there is no next one and nothing to count.
	if (!FloorBrief.bWaveWalksIn)
	{
		return false;
	}

	// A WAVE STILL ARRIVING HAS NOT BEEN BEATEN, even when every creature of it
	// that has arrived so far is dead. Issue #1544. The threshold is a tenth of
	// what the wave arrives with, and that is not known until all of it has:
	// judged early, a wave whose first few were killed as they appeared would
	// count as finished and bring the next wave in on top of the rest of it.
	if (!WaveStillToArrive.IsEmpty())
	{
		return false;
	}

	// A WAVE THAT PUT NOTHING ON THE FLOOR IS NOT A WAVE THAT HAS BEEN BEATEN.
	// `Cataclysm.DungeonEnemyScale 0` empties a floor, and without this the
	// whole dungeon's worth of waves would run through in one tick, spending a
	// day of empire time for each.
	if (WaveSpawned <= 0)
	{
		return false;
	}

	// **THE LAST WAVE IS CLEARED RATHER THAN THINNED, AND THAT IS NOT THE
	// OWNER'S RULE BEING BENT.** Their rule says when the NEXT wave spawns, and
	// after the last wave there is no next one for it to say anything about.
	//
	// WHAT GOES WRONG WITHOUT IT. The Gatekeeper is placed into the last wave
	// like any other creature -- `FCataclysmFloorBrief::bBossAtTheExit` is true
	// on a dungeon's final floor, and that floor is the final wave. So ending
	// the dungeon at a tenth remaining lets a player beat it with its boss
	// standing, and "Every dungeon has a boss on the final floor" would gate
	// nothing at all.
	//
	// IT CANNOT LOCK. Every creature the population pass places stands on a cell
	// it has already proved can be walked to from the entrance, so a wave can
	// always be finished off. That is the same reason
	// `NextWaveArrivesAtOrBelow` is safe to round down to zero.
	//
	// A DUNGEON WITH NO BOTTOM NEVER REACHES IT, which is what keeps pressing
	// Play in `L_Dungeon` running waves for ever the way it runs floors for
	// ever. `IsOnTheLastFloor` answers false when there is no empire dungeon.
	const int32 Threshold = IsOnTheLastFloor()
		? 0
		: FCataclysmDungeonFloorRules::NextWaveArrivesAtOrBelow(WaveSpawned);

	return WaveStillAlive() <= Threshold;
}

void ACataclysmDungeonGameMode::BringTheNextWaveIn()
{
	if (!ShouldTheNextWaveArrive())
	{
		return;
	}

	// READ BEFORE THE NEXT WAVE ARRIVES, ALL THREE. `GoDownOneFloor` runs
	// `PopulateFloor`, which replaces `CurrentWave` and `WaveSpawned` with the
	// NEW wave's -- so asking afterwards would report the arriving wave's
	// numbers under the finished wave's name.
	const int32 Finished = WavesArrived;
	const int32 LeftStanding = WaveStillAlive();
	const int32 ArrivedWith = WaveSpawned;

	// THE SAME CALL THE STAIRS MAKE. A wave is a floor, so bringing the next one
	// in spends a day, moves the floor number, and -- on the last wave --
	// finishes the dungeon, all without any of those rules being written down a
	// second time. `GoToFloor` is what leaves the arena standing, because
	// `FCataclysmFloorBrief::bSameArenaAsLastFloor` is true for every floor of a
	// Horde dungeon after the first.
	if (GoDownOneFloor())
	{
		UE_LOG(LogCataclysm, Verbose,
			   TEXT("Wave %d of the arena was down to %d of the %d it arrived "
					"with, so wave %d walked in."),
			   Finished, LeftStanding, ArrivedWith, WavesArrived);
		return;
	}

	// IT ANSWERED NO, WHICH ON THE LAST WAVE MEANS THE DUNGEON IS BEATEN.
	// `GoDownOneFloor` clears the dungeon and returns false there. Forgetting
	// the wave is what stops this being asked again on every tick afterwards,
	// which would otherwise try to descend out of a dungeon that has already
	// been left.
	UE_LOG(LogCataclysm, Verbose,
		   TEXT("The last wave of the arena, wave %d, was down to %d of the %d "
				"it arrived with."),
		   Finished, LeftStanding, ArrivedWith);

	CurrentWave.Reset();
	WaveSpawned = 0;
}

// ---------------------------------------------------------------------------
// The stairs down
// ---------------------------------------------------------------------------

ACataclysmDungeonStairs* ACataclysmDungeonGameMode::PlaceStairs()
{
	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return nullptr;
	}

	if (!Stairs)
	{
		Stairs = World->SpawnActor<ACataclysmDungeonStairs>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Stairs)
		{
			return nullptr;
		}

		// BOUND ONCE, WHEN THE ACTOR IS MADE, rather than on every floor. A
		// dynamic multicast delegate's `AddDynamic` binds with `AddUnique`, so
		// binding the same object and function again would be discarded and the
		// player would not descend two floors for one flight of stairs. Doing it
		// here anyway says what is meant instead of relying on that.
		Stairs->OnTaken.AddDynamic(this,
			&ACataclysmDungeonGameMode::HandleStairsTaken);
	}

	Stairs->PlaceAt(CurrentFloor->ExitWorld());
	Stairs->StartWatching();

	return Stairs;
}

void ACataclysmDungeonGameMode::HandleStairsTaken()
{
	// BLOOD GATES: SEALED, SO NOTHING HAPPENS, AND THE STAIRS WATCH AGAIN. Issues #1820
	// and #41. `ACataclysmDungeonStairs::ArriveAt` stops its watch before announcing, so
	// a refusal that did not start it again would leave stairs that never answer, even
	// once the gate had opened. A player standing on them goes down on the first look
	// after it opens. THE STAIRS THEMSELVES SHOW NOTHING: no system in the interface
	// shows the player a message, so the floor panel's line is this rule's interface.
	if (BloodGatesSealTheStairs())
	{
		if (Stairs)
		{
			Stairs->StartWatching();
		}
		RefreshFloorModifierPanel();
		return;
	}

	// UNSTABLE PORTAL: ONE ROLL, and only where the stairs lead to a next floor. Issues
	// #1820 and #41. A roll that does not take the player down leaves them on this floor
	// and the stairs watching again, ignoring them until they step off.
	using Effects = UCataclysmDungeonModifierEffects;
	if (FloorBrief.Modifiers.Contains(FName(Effects::UnstablePortalKey)) && !IsOnTheLastFloor())
	{
		const int32 Outcome =
			Effects::UnstablePortalOutcomeFor(DungeonGameModeUnstablePortalRoll());
		++UnstablePortalRolls;
		UnstablePortalLast = Outcome;

		if (Outcome == Effects::UnstablePortalDescends)
		{
			GoDownOneFloor();
			return;
		}

		if (Outcome == Effects::UnstablePortalReturns)
		{
			// THE PLAYER ONLY, to the floor's entrance, the way a floor places them.
			UWorld* World = GetWorld();
			APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			PlaceAtEntrance(Controller ? Controller->GetPawn() : nullptr);
		}
		else
		{
			RaiseTheUnstablePortalsWarden();
		}

		if (Stairs)
		{
			Stairs->RequireThePlayerToLeaveFirst();
			Stairs->StartWatching();
		}
		RefreshFloorModifierPanel();
		return;
	}

	GoDownOneFloor();
}

bool ACataclysmDungeonGameMode::IsTheFinalFloorForItsBoss() const
{
	const int32 Floors = ChooseTotalFloors();
	return Floors > 1 && FloorBrief.FloorNumber >= Floors;
}

void ACataclysmDungeonGameMode::NoteDeathForNothingIsForgotten(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::NothingIsForgottenKey)))
	{
		return;
	}

	// "THAT THE PLAYER KILLS", read as Vengeful Wraiths and Blood Gates read it; and never a
	// marked creature, whose second death pays nothing.
	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
	if (!Fallen || !Fallen->PaysForItsDeath() || !Player || Notice.Killer != Player)
	{
		return;
	}

	const UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Fallen);
	if (!Abilities)
	{
		return;
	}
	NothingIsForgottenHealth += Effects::NothingIsForgottenPortionOf(
		Abilities->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
	NothingIsForgottenDamage += Effects::NothingIsForgottenPortionOf(
		Abilities->GetNumericAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute()));
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepTheReaper()
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE FLOOR'S OWN SECONDS, counted on the beat as Death's Embrace counts them, so a
	// floor change puts the clock back by putting the count back.
	TheReaperSecondsOnFloor += SecondsBetweenWaveChecks;
	if (!Effects::TheReaperIsDue(TheReaperSecondsOnFloor))
	{
		return;
	}

	// ONCE, WHETHER OR NOT THE SPAWN SUCCEEDS: a floor that could not hold it does not
	// try again four times a second.
	bTheReaperRaised = true;
	RaiseTheReaper();
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::RaiseTheReaper()
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// AN ABYSSAL WARDEN AT THE ENTRANCE, AT THE COMMON RUNG, the way Unstable Portal
	// raises its Warden at the exit. The rung is set before its modifiers are drawn;
	// see `SpawnPlacedCreature`.
	FCataclysmEnemyPlacement Placement;
	Placement.Cell = CurrentFloor->CellOfWorld(CurrentFloor->EntranceWorld());
	Placement.Creature = ECataclysmDungeonCreature::AbyssalWarden;
	ACataclysmEnemyCharacter* Reaper = SpawnPlacedCreature(
		Placement, Effects::TheReaperSightMultiplier, Effects::TheReaperRung);
	if (!Reaper)
	{
		return;
	}
	Reaper->bCannotBeHurt = true;
	FloorEnemies.Add(Reaper);
	Reaper->bRaisedByARule = true;
	CreaturesRaisedByARule.Add(Reaper);
	TheReaper = Reaper;
	UE_LOG(LogCataclysm, Log, TEXT("The Reaper: %s arrives at floor %d's entrance"),
		   *Reaper->GetName(), FloorNumber);
}

void ACataclysmDungeonGameMode::NoteHitForTheReaper(const FCataclysmHitNotice& Notice)
{
	// ITS OWN BLOW, AND ONE THAT LANDED: `Landed` is zero for a blow evaded or wholly
	// stopped. A damage-over-time tick is not a blow of the scythe, so it does not kill.
	ACataclysmEnemyCharacter* Reaper = TheReaper.Get();
	if (!Reaper || Notice.Attacker != Reaper || Notice.Landed <= 0.0f
		|| Notice.bDamageOverTime)
	{
		return;
	}
	ACataclysmPlayerCharacter* Player = Cast<ACataclysmPlayerCharacter>(Notice.Target);
	if (!Player || UCataclysmSkillEffects::IsDead(Player))
	{
		return;
	}

	// STRAIGHT TO HEALTH, NOT AS A BLOW. Nothing Stops It saves only from a blow, so it
	// cannot catch this: the row says "instantly die". The ordinary death follows from
	// health reaching zero, revival and every death rule with it.
	const UAbilitySystemComponent* Theirs = Player->GetAbilitySystemComponent();
	const float Maximum = Theirs
		? Theirs->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
		: 0.0f;
	UCataclysmSkillEffects::ReduceHealthDirectly(Reaper, Player, FMath::Max(1.0f, Maximum));
	UE_LOG(LogCataclysm, Log, TEXT("The Reaper: its blow landed and %s died"),
		   *Player->GetName());
}

void ACataclysmDungeonGameMode::StepBloodBond(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!Player || UCataclysmSkillEffects::IsDead(Player))
	{
		return;
	}

	// THE NEAREST, when more than one elite notices the player on the same beat: the one
	// the player met first, as nearly as a quarter-second beat can tell.
	ACataclysmEnemyCharacter* Nearest = nullptr;
	float NearestCm = 0.0f;
	for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : FloorEnemies)
	{
		if (!IsValid(Enemy) || UCataclysmSkillEffects::IsDead(Enemy))
		{
			continue;
		}
		const float Cm = FVector::Dist(Enemy->GetActorLocation(), Player->GetActorLocation());
		if (!Effects::BloodBondMayBond(Enemy->RarityStep, DiedAsAFloorsBoss(Enemy), Cm,
										 Enemy->NoticesFromCm()))
		{
			continue;
		}
		if (!Nearest || Cm < NearestCm)
		{
			Nearest = Enemy;
			NearestCm = Cm;
		}
	}
	if (!Nearest)
	{
		return;
	}

	bBloodBondFormed = true;
	Nearest->bCannotBeHurt = true;
	CreaturesRaisedByARule.Add(Nearest);
	BloodBonded = Nearest;
	UE_LOG(LogCataclysm, Log, TEXT("Blood Bond: %s is bound to the player on floor %d"),
		   *Nearest->GetName(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteDeathForBloodBond(const FCataclysmDeathNotice& Notice)
{
	ACataclysmEnemyCharacter* Bonded = BloodBonded.Get();
	if (!Bonded || !Cast<ACataclysmPlayerCharacter>(Notice.Victim)
		|| UCataclysmSkillEffects::IsDead(Bonded))
	{
		return;
	}

	// THE ORDINARY DEATH, WITH NOTHING TO PAY AND NOBODY TO CREDIT. The flag comes off
	// first, or the health write below would be held at the maximum. The last blow is
	// emptied because a death written to health names whoever last struck the creature,
	// the way Sacrificial Ward's spent minion is. Cleared before the write, so the death
	// this causes finds no bond to act on.
	BloodBonded.Reset();
	Bonded->bCannotBeHurt = false;
	Bonded->bDiesUnpaid = true;
	if (UCataclysmAbilitySystemComponent* Its = Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Bonded)))
	{
		Its->RecordLastBlow(FCataclysmLastBlow());
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
	}
	UE_LOG(LogCataclysm, Log, TEXT("Blood Bond: the player died and %s died with them"),
		   *Bonded->GetName());
	RefreshFloorModifierPanel();
}

int32 ACataclysmDungeonGameMode::PlagueConvergenceCreaturesAlive() const
{
	int32 Alive = 0;
	for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Creature : PlagueConvergenceCreatures)
	{
		if (Creature.IsValid() && !UCataclysmSkillEffects::IsDead(Creature.Get()))
		{
			++Alive;
		}
	}
	return Alive;
}

bool ACataclysmDungeonGameMode::IsAPlagueConvergenceCreature(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Creature : PlagueConvergenceCreatures)
	{
		if (Creature.Get() == Actor)
		{
			return true;
		}
	}
	return false;
}

TArray<FIntPoint> ACataclysmDungeonGameMode::ConvergenceArrivalCells(
	const FCataclysmFloorPlan& Plan, FIntPoint From, int32 Count)
{
	// AN EDGE CELL IS FLOOR WITH A SIDE ON ROCK OR OFF THE GRID: where a horde can come out
	// of the walls rather than out of the middle of a room.
	TArray<FIntPoint> Edge;
	for (int32 Y = 0; Y < Plan.Height; ++Y)
	{
		for (int32 X = 0; X < Plan.Width; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (Plan.IsFloor(Cell)
				&& (!Plan.IsFloor(Cell + FIntPoint(1, 0)) || !Plan.IsFloor(Cell + FIntPoint(-1, 0))
					|| !Plan.IsFloor(Cell + FIntPoint(0, 1)) || !Plan.IsFloor(Cell + FIntPoint(0, -1))))
			{
				Edge.Add(Cell);
			}
		}
	}

	// THE FARTHEST FIRST, by distance in cells; ties keep the grid's order, so the answer is
	// the same every time for one plan.
	const auto Away = [From](const FIntPoint& Cell)
	{
		return (Cell - From).SizeSquared();
	};
	Edge.StableSort([&Away](const FIntPoint& A, const FIntPoint& B) { return Away(A) > Away(B); });
	if (Edge.Num() > Count)
	{
		Edge.SetNum(FMath::Max(0, Count));
	}
	return Edge;
}

void ACataclysmDungeonGameMode::StepPlagueConvergence(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !Player || !AbilitySystem || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// THE DISEASE BURNS ONCE A SECOND WHILE IT IS HELD, typed as the row, from the floor's
	// hazard source as Necrotic Ground's fog burns: damage over time, which pestilence
	// resistance meets. Unlike Wasting Sickness, which weakens by a share per stack, this is
	// damage.
	PlagueConvergenceSecondsSinceBurn += SecondsBetweenWaveChecks;
	if (PlagueConvergenceStacks > 0
		&& PlagueConvergenceSecondsSinceBurn >= Effects::PlagueConvergenceSecondsBetweenBurns)
	{
		PlagueConvergenceSecondsSinceBurn = 0.0f;
		const float Burn = AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute())
			* Effects::PlagueConvergenceDiseasePercentPerSecond(PlagueConvergenceStacks) / 100.0f;
		ACataclysmFloorHazardSource* Burning = ACataclysmFloorHazardSource::ForFloor(World);
		if (Burning && Burn > 0.0f && !UCataclysmSkillEffects::IsDead(Player))
		{
			FCataclysmHitDelivery Delivery;
			Delivery.bIsDamageOverTime = true;
			Delivery.DamageType = DungeonGameModeTypeOfRow(Effects::PlagueConvergenceKey);
			UCataclysmSkillEffects::ApplyDirectDamage(Burning, Player, Burn, Delivery);
		}
	}

	// THE CLOCK, the Reaper's kind: the floor's seconds on the beat. Killing the creatures
	// does not wind it back; only a new floor does.
	PlagueConvergenceSecondsOnFloor += SecondsBetweenWaveChecks;
	if (!Effects::PlagueConvergenceHasBegun(PlagueConvergenceSecondsOnFloor))
	{
		return;
	}

	// A WAVE AS IT BEGINS AND EVERY CADENCE AFTER. The first beat at or past the start finds
	// the count at its cadence already, so the first wave comes on that beat.
	PlagueConvergenceSecondsSinceWave += SecondsBetweenWaveChecks;
	const bool bFirst = PlagueConvergenceCreatures.IsEmpty();
	if (!bFirst
		&& PlagueConvergenceSecondsSinceWave < Effects::PlagueConvergenceSecondsBetweenWaves)
	{
		return;
	}
	PlagueConvergenceSecondsSinceWave = 0.0f;

	PlagueConvergenceCreatures.RemoveAll([](const TWeakObjectPtr<ACataclysmEnemyCharacter>& One)
	{
		return !One.IsValid() || UCataclysmSkillEffects::IsDead(One.Get());
	});
	const int32 Wanted = Effects::PlagueConvergenceWaveSize(PlagueConvergenceCreatures.Num());
	if (Wanted <= 0)
	{
		return;
	}

	// THE FLOOR'S OWN KINDS, drawn from a fresh population as Grave Tide draws them.
	const FCataclysmFloorPopulation Population = FCataclysmFloorPopulator::Populate(
		CurrentFloor->GetPlan(), ChooseEnemyScale(), FloorBrief);
	const TArray<FIntPoint> Cells = ConvergenceArrivalCells(
		CurrentFloor->GetPlan(), CurrentFloor->CellOfWorld(Player->GetActorLocation()), Wanted);
	if (Population.Enemies.IsEmpty() || Cells.IsEmpty())
	{
		return;
	}

	int32 Placed = 0;
	for (int32 Which = 0; Which < Wanted; ++Which)
	{
		FCataclysmEnemyPlacement Placement =
			Population.Enemies[FMath::RandRange(0, Population.Enemies.Num() - 1)];
		Placement.Cell = Cells[Which % Cells.Num()];

		// NOTICING FROM ANYWHERE ON THE FLOOR, the Reaper's figure: a horde that converges comes
		// for the player rather than waiting at the wall to be found.
		ACataclysmEnemyCharacter* Arrived =
			SpawnPlacedCreature(Placement, Effects::TheReaperSightMultiplier);
		if (!Arrived)
		{
			continue;
		}
		Arrived->bDiesUnpaid = true;
		Arrived->bRaisedByARule = true;
		CreaturesRaisedByARule.Add(Arrived);
		FloorEnemies.Add(Arrived);
		PlagueConvergenceCreatures.Add(Arrived);
		++Placed;
	}
	UE_LOG(LogCataclysm, Log,
		   TEXT("Plague Convergence: %d creature(s) arrived on floor %d, %d alive of at most %d"),
		   Placed, FloorNumber, PlagueConvergenceCreatures.Num(), Effects::PlagueConvergenceMostAlive);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteHitForPlagueConvergence(const FCataclysmHitNotice& Notice)
{
	// A LANDED BLOW FROM ONE OF ITS CREATURES ON THE PLAYER, and not a tick: a creature's
	// damage over time is not a hit.
	if (Notice.Landed <= 0.0f || Notice.bDamageOverTime
		|| !Cast<ACataclysmPlayerCharacter>(Notice.Target)
		|| !IsAPlagueConvergenceCreature(Notice.Attacker))
	{
		return;
	}
	PlagueConvergenceStacks =
		UCataclysmDungeonModifierEffects::PlagueConvergenceStacksAfterHit(PlagueConvergenceStacks);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteDeathForPlagueConvergence(const FCataclysmDeathNotice& Notice)
{
	// THE PLAYER'S OWN DEATH CLEARS THE DISEASE, under the owner's ruling of 2026-09-10 that
	// what lasts only for a dungeon ends at a death. The clock and the creatures stay: only
	// descending stops the convergence.
	if (PlagueConvergenceStacks > 0 && Cast<ACataclysmPlayerCharacter>(Notice.Victim))
	{
		PlagueConvergenceStacks = 0;
		PlagueConvergenceSecondsSinceBurn = 0.0f;
		RefreshFloorModifierPanel();
	}
}

TArray<int32> ACataclysmDungeonGameMode::EchoAttacksRecordedThisFloor() const
{
	TArray<int32> Attacks;
	for (const FEchoOfTheDead& Dead : EchoesThisFloor)
	{
		Attacks.Add(Dead.Attack);
	}
	return Attacks;
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::EchoesStandingNow() const
{
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (const FEchoStanding& One : EchoesStanding)
	{
		if (ACataclysmEnemyCharacter* Echo = One.Echo.Get())
		{
			Standing.Add(Echo);
		}
	}
	return Standing;
}

void ACataclysmDungeonGameMode::NoteDeathForEchoesOfThePast(const FCataclysmDeathNotice& Notice)
{
	// EVERY FLOOR, WHETHER OR NOT IT CARRIES THE ROW: the next floor decides. Not a death that
	// pays nothing, which covers the risen and the rules' own unpaid creatures, and not an
	// echo, which cannot be hurt and never dies. A creature the floor cannot name a kind for
	// cannot be brought back and is not kept.
	const ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Fallen || !Fallen->PaysForItsDeath() || Fallen->bCannotBeHurt)
	{
		return;
	}
	const ECataclysmDungeonCreature Kind = DungeonGameModeKindOf(Fallen);
	if (Kind == ECataclysmDungeonCreature::Count)
	{
		return;
	}
	FEchoOfTheDead Dead;
	Dead.Kind = Kind;
	Dead.Rung = Fallen->RarityStep;
	Dead.Attack = Fallen->LastAttackUsed;
	EchoesThisFloor.Add(Dead);
	// THE LAST TO DIE ARE THE ONES KEPT.
	while (EchoesThisFloor.Num() > UCataclysmDungeonModifierEffects::EchoesMost)
	{
		EchoesThisFloor.RemoveAt(0);
	}
}

void ACataclysmDungeonGameMode::DismissTheEchoes()
{
	for (const FEchoStanding& One : EchoesStanding)
	{
		if (ACataclysmEnemyCharacter* Echo = One.Echo.Get())
		{
			Echo->Destroy();
		}
	}
	EchoesStanding.Reset();
}

void ACataclysmDungeonGameMode::StepEchoesOfThePast(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!Player || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}
	EchoesSecondsOnFloor += SecondsBetweenWaveChecks;
	const FVector Centre = Player->GetActorLocation();

	if (EchoesStage == 0)
	{
		if (EchoesSecondsOnFloor < Effects::EchoesAppearAfterSeconds)
		{
			return;
		}
		// THE LAST FLOOR'S DEAD APPEAR, each at its own ordinary attack reach from the player,
		// at even angles. SpawnPlacedCreature puts it on the right cell at the right height;
		// it is then moved to the exact spot and turned to face the player.
		const int32 Count = EchoesFromLastFloor.Num();
		for (int32 Which = 0; Which < Count; ++Which)
		{
			const FEchoOfTheDead& Dead = EchoesFromLastFloor[Which];
			FCataclysmEnemyPlacement Placement;
			Placement.Creature = Dead.Kind;
			Placement.Cell = CurrentFloor->CellOfWorld(Centre);
			ACataclysmEnemyCharacter* Echo = SpawnPlacedCreature(Placement, 1.0f, Dead.Rung);
			if (!Echo)
			{
				continue;
			}

			// ITS BRAIN GOES AT ONCE: with no controller nothing moves it, chooses for it or
			// attacks with it. Unpossessing clears the controller's thinking timer.
			if (AController* Brain = Echo->GetController())
			{
				Brain->UnPossess();
				Brain->Destroy();
			}

			const FVector Offset = Effects::EchoesOffset(
				Which, Count, Effects::EchoesDistanceCm(Echo->AttackReachCm()));
			const FVector Where(Centre.X + Offset.X, Centre.Y + Offset.Y, Echo->GetActorLocation().Z);
			Echo->SetActorLocation(Where);
			FVector Toward = Centre - Where;
			Toward.Z = 0.0f;
			if (!Toward.IsNearlyZero())
			{
				Echo->SetActorRotation(Toward.Rotation());
			}

			// NOT ON THE FLOOR'S LIST OF CREATURES: an echo is gone two seconds later, and a rule
			// that chose from that list -- Blood Bond's one bond, say -- must not spend itself on one.
			Echo->bCannotBeHurt = true;
			Echo->bDiesUnpaid = true;
			Echo->bRaisedByARule = true;
			CreaturesRaisedByARule.Add(Echo);
			FEchoStanding Standing;
			Standing.Echo = Echo;
			Standing.Attack = Dead.Attack;
			EchoesStanding.Add(Standing);
		}
		EchoesStage = 1;
		UE_LOG(LogCataclysm, Log, TEXT("Echoes of the Past: %d echo(es) appeared on floor %d"),
			   EchoesStanding.Num(), FloorNumber);
		RefreshFloorModifierPanel();
		return;
	}

	if (EchoesStage == 1)
	{
		if (EchoesSecondsOnFloor
			< Effects::EchoesAppearAfterSeconds + Effects::EchoesStrikeAfterSeconds)
		{
			return;
		}
		// EACH STRIKES ONCE: its recorded ability if it has that ability, else its ordinary
		// attack, which is also what a creature that never attacked repeats.
		for (const FEchoStanding& One : EchoesStanding)
		{
			ACataclysmEnemyCharacter* Echo = One.Echo.Get();
			if (!Echo)
			{
				continue;
			}
			if (One.Attack >= 0 && Echo->EnemyAbilities().IsValidIndex(One.Attack))
			{
				Echo->UseEnemyAbility(One.Attack, Player, Player->GetActorLocation());
			}
			else
			{
				Echo->AttackTarget(Player);
			}
		}
		EchoesStage = 2;
		return;
	}

	if (EchoesSecondsOnFloor < Effects::EchoesAppearAfterSeconds + Effects::EchoesStrikeAfterSeconds
			+ Effects::EchoesVanishAfterSeconds)
	{
		return;
	}
	DismissTheEchoes();
	EchoesStage = 3;
	RefreshFloorModifierPanel();
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::PlagueHarbingersAlive() const
{
	TArray<ACataclysmEnemyCharacter*> Alive;
	for (const FPlagueHarbingerTrail& Trail : PlagueHarbingerTrails)
	{
		ACataclysmEnemyCharacter* Harbinger = Trail.Harbinger.Get();
		if (IsValid(Harbinger) && !UCataclysmSkillEffects::IsDead(Harbinger))
		{
			Alive.Add(Harbinger);
		}
	}
	return Alive;
}

int32 ACataclysmDungeonGameMode::PlagueHarbingerTrailPatches(
	const ACataclysmEnemyCharacter* Harbinger) const
{
	int32 Standing = 0;
	for (const FPlagueHarbingerTrail& Trail : PlagueHarbingerTrails)
	{
		if (Harbinger && Trail.Harbinger.Get() != Harbinger)
		{
			continue;
		}
		for (const TWeakObjectPtr<ACataclysmGroundZone>& Patch : Trail.Patches)
		{
			Standing += Patch.IsValid() ? 1 : 0;
		}
	}
	return Standing;
}

void ACataclysmDungeonGameMode::ForgetThePlagueHarbingers()
{
	for (const FPlagueHarbingerTrail& Trail : PlagueHarbingerTrails)
	{
		if (ACataclysmEnemyCharacter* Harbinger = Trail.Harbinger.Get())
		{
			Harbinger->bPlagueHarbinger = false;
		}
	}
	PlagueHarbingerTrails.Reset();
	PlagueHarbingersPanelAlive = -1;
	PlagueHarbingersPanelPatches = -1;
}

void ACataclysmDungeonGameMode::ChooseThePlagueHarbingers()
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::PlagueHarbingersKey)))
	{
		return;
	}

	// ONE PER TEN PLACED, ROUNDED UP, FROM THIS FLOOR'S OR WAVE'S OWN CREATURES, never a
	// floor's boss: a Gatekeeper, or a creature that drew the Boss rung. Fewer when fewer
	// can be chosen.
	TArray<ACataclysmEnemyCharacter*> Candidates;
	for (ACataclysmEnemyCharacter* Enemy : CurrentWave)
	{
		if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy) && !Enemy->bPlagueHarbinger
			&& !DiedAsAFloorsBoss(Enemy))
		{
			Candidates.Add(Enemy);
		}
	}
	const int32 Wanted = FMath::Min(Effects::PlagueHarbingersFor(CurrentWave.Num()), Candidates.Num());

	// AT RANDOM: the first `Wanted` of an even shuffle.
	for (int32 Index = 0; Index < Wanted; ++Index)
	{
		Candidates.Swap(Index, FMath::RandRange(Index, Candidates.Num() - 1));
		ACataclysmEnemyCharacter* Chosen = Candidates[Index];
		Chosen->bPlagueHarbinger = true;
		FPlagueHarbingerTrail Trail;
		Trail.Harbinger = Chosen;
		PlagueHarbingerTrails.Add(Trail);
	}
	UE_LOG(LogCataclysm, Log, TEXT("Plague Harbingers: %d of %d creature(s) chosen on floor %d"),
		   Wanted, CurrentWave.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepPlagueHarbingers(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	const FName Type = DungeonGameModeTypeOfRow(Effects::PlagueHarbingersKey);
	const float Burn = Effects::PlagueHarbingersBurn(
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()));

	// EACH LIVING HARBINGER LAYS A PATCH WHERE IT STANDS once it has moved far enough from its
	// last. THE PATCH BURNS BY ITSELF, marked as the row's so overlapping patches burn the
	// player once a second between them (#2074), and it lasts the floor: the floor ending,
	// or the Horde wave's rule-zone clearing, is what ends it. A dead Harbinger's trail is
	// already gone; see `NoteDeathForPlagueHarbingers`.
	for (FPlagueHarbingerTrail& Trail : PlagueHarbingerTrails)
	{
		Trail.Patches.RemoveAll([](const TWeakObjectPtr<ACataclysmGroundZone>& Patch)
		{
			return !Patch.IsValid();
		});
		ACataclysmEnemyCharacter* Harbinger = Trail.Harbinger.Get();
		if (!IsValid(Harbinger) || UCataclysmSkillEffects::IsDead(Harbinger) || !Source || Burn <= 0.0f)
		{
			continue;
		}
		const FVector Feet = Harbinger->GetActorLocation();
		if (Trail.bHasLaidAPatch
			&& !Effects::PlagueHarbingersPatchIsDue(
				static_cast<float>(FVector::Dist2D(Feet, Trail.LastPatchAt))))
		{
			continue;
		}
		ACataclysmGroundZone* Patch = ACataclysmGroundZone::SpawnForTheFloor(
			Source, Feet, Feet, Effects::PlagueHarbingersPatchRadiusCm, Burn,
			/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type, /*InDamageType=*/Type);
		if (!Patch)
		{
			continue;
		}
		Patch->BurnsOnceASecondAs = FName(Effects::PlagueHarbingersKey);
		Trail.Patches.Add(Patch);
		Trail.LastPatchAt = Feet;
		Trail.bHasLaidAPatch = true;

		// ITS NEWEST TWENTY: the oldest goes when a twenty-first is laid.
		while (Trail.Patches.Num() > Effects::PlagueHarbingersMostPatchesEach)
		{
			if (ACataclysmGroundZone* Oldest = Trail.Patches[0].Get())
			{
				Oldest->Destroy();
			}
			Trail.Patches.RemoveAt(0);
		}
	}

	// EVERY CREATURE STANDING IN ANY PATCH IS EMPOWERED, Harbingers included, with the buff
	// Hallowed Groundfall's craters grant and in the way they grant it: refreshed each beat
	// rather than stacked, so it lapses a second after the creature steps out.
	const FGameplayTag Empowered = UCataclysmSkillShapes::StatusTagFor(TEXT("Commander"));
	if (Empowered.IsValid())
	{
		for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
		{
			ACataclysmEnemyCharacter* Creature = *It;
			if (!IsValid(Creature) || !UCataclysmTargeting::IsHostileTo(Creature, Player))
			{
				continue;
			}
			const FVector Feet = Creature->GetActorLocation();
			bool bOnATrail = false;
			for (const FPlagueHarbingerTrail& Trail : PlagueHarbingerTrails)
			{
				for (const TWeakObjectPtr<ACataclysmGroundZone>& Patch : Trail.Patches)
				{
					if (Patch.IsValid() && Patch->Covers(Feet))
					{
						bOnATrail = true;
						break;
					}
				}
				if (bOnATrail)
				{
					break;
				}
			}
			if (bOnATrail)
			{
				UCataclysmSkillEffects::ApplyTagForDuration(
					Creature, Creature, Empowered, Effects::PlagueHarbingersEmpowerSeconds);
			}
		}
	}

	const int32 Alive = PlagueHarbingersAlive().Num();
	const int32 Patches = PlagueHarbingerTrailPatches();
	if (Alive != PlagueHarbingersPanelAlive || Patches != PlagueHarbingersPanelPatches)
	{
		PlagueHarbingersPanelAlive = Alive;
		PlagueHarbingersPanelPatches = Patches;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::NoteDeathForPlagueHarbingers(const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// ANY DEATH OF A HARBINGER, whoever or whatever caused it.
	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	const int32 Which = PlagueHarbingerTrails.IndexOfByPredicate(
		[Fallen](const FPlagueHarbingerTrail& Trail) { return Fallen && Trail.Harbinger.Get() == Fallen; });
	if (Which == INDEX_NONE)
	{
		return;
	}

	// ITS TRAIL IS CLEANSED.
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Patch : PlagueHarbingerTrails[Which].Patches)
	{
		if (ACataclysmGroundZone* Standing = Patch.Get())
		{
			Standing->Destroy();
		}
	}
	PlagueHarbingerTrails.RemoveAt(Which);
	Fallen->bPlagueHarbinger = false;

	// AND THE LIVING CREATURES NEAR IT ARE WEAKENED, by the Weaken row of
	// `game/Data/StatusEffects.csv` at its own strength and duration, given in the name of
	// the floor's hazard source as the floor rules' other ailments are.
	UWorld* World = GetWorld();
	ACataclysmFloorHazardSource* Source = World ? ACataclysmFloorHazardSource::ForFloor(World) : nullptr;
	const FCataclysmAilmentKind* Weaken = UCataclysmAilments::KindNamed(TEXT("Weaken"));
	if (Source && Weaken)
	{
		const FVector Where = Fallen->GetActorLocation();
		for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
		{
			ACataclysmEnemyCharacter* Near = *It;
			// THE HARBINGER'S OWN SIDE ONLY: a creature the player has taken is not weakened.
			if (!IsValid(Near) || Near == Fallen || UCataclysmSkillEffects::IsDead(Near)
				|| !UCataclysmTargeting::IsFriendlyTo(Near, Source)
				|| FVector::Dist2D(Near->GetActorLocation(), Where) > Effects::PlagueHarbingersWeakenRadiusCm)
			{
				continue;
			}
			UCataclysmAilments::Apply(Source, Near, *Weaken, /*Magnitude=*/1.0f);
		}
	}
	RefreshFloorModifierPanel();
}

TArray<ACataclysmGroundZone*> ACataclysmDungeonGameMode::WingsOfTheHostMarksNow() const
{
	TArray<ACataclysmGroundZone*> Marks;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Mark : WingsOfTheHostMarks)
	{
		if (ACataclysmGroundZone* Standing = Mark.Get())
		{
			Marks.Add(Standing);
		}
	}
	return Marks;
}

TArray<FVector> ACataclysmDungeonGameMode::WingsOfTheHostFeathers(
	const ACataclysmDungeonFloor& Floor, const FVector& Through, const FVector& Direction)
{
	using Effects = UCataclysmDungeonModifierEffects;

	TArray<FVector> Feathers;
	const FCataclysmFloorPlan& Plan = Floor.GetPlan();
	const FVector Along = FVector(Direction.X, Direction.Y, 0.0f).GetSafeNormal();
	if (Plan.Width <= 0 || Plan.Height <= 0 || Along.IsZero())
	{
		return Feathers;
	}

	// FAR ENOUGH EACH WAY TO LEAVE THE FLOOR: the distance from `Through` to the floor's middle
	// and then half its diagonal. Points past its edge are off the grid, and `IsFloor` answers
	// false for them, so walking too far costs only the tests.
	const float CellCm = static_cast<float>(
		FVector::Dist2D(Floor.WorldOfCell(FIntPoint(0, 0)), Floor.WorldOfCell(FIntPoint(1, 0))));
	const float Reach = static_cast<float>(FVector::Dist2D(Through, Floor.GetActorLocation()))
		+ 0.5f * CellCm * FMath::Sqrt(static_cast<float>(Plan.Width * Plan.Width + Plan.Height * Plan.Height));
	const int32 Steps = FMath::CeilToInt(Reach / Effects::WingsOfTheHostFeatherEveryCm);
	for (int32 Step = -Steps; Step <= Steps; ++Step)
	{
		const FVector Point = Through + Along * (Effects::WingsOfTheHostFeatherEveryCm * Step);
		// FLOOR CELLS ONLY: where the line crosses rock, no feather is marked and it goes on.
		if (Plan.IsFloor(Floor.CellOfWorld(Point)))
		{
			Feathers.Add(FVector(Point.X, Point.Y, Through.Z));
		}
	}
	return Feathers;
}

TArray<FIntPoint> ACataclysmDungeonGameMode::WingsOfTheHostThroughCells(
	const ACataclysmDungeonFloor& Floor, const FVector& Centre)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// A PLAYER ON A FLOOR CELL ALWAYS HAS ONE: the middle of the cell they stand in is at most half
	// a cell's diagonal away.
	static_assert(0.5f * FCataclysmFloorGenerator::CellSizeCm * 1.41422f < Effects::WingsOfTheHostPassesWithinCm,
				  "A player standing on a floor cell must be within reach of that cell's middle.");

	TArray<FIntPoint> Cells;
	const FCataclysmFloorPlan& Plan = Floor.GetPlan();
	for (int32 Y = 0; Y < Plan.Height; ++Y)
	{
		for (int32 X = 0; X < Plan.Width; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (Plan.IsFloor(Cell)
				&& FVector::Dist2D(Floor.WorldOfCell(Cell), Centre) <= Effects::WingsOfTheHostPassesWithinCm)
			{
				Cells.Add(Cell);
			}
		}
	}
	return Cells;
}

void ACataclysmDungeonGameMode::StepWingsOfTheHost(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// A FLYOVER IS MARKED: COUNT ITS WARNING, then every feather lands together.
	if (!WingsOfTheHostMarksNow().IsEmpty())
	{
		WingsOfTheHostWarningSoFar += SecondsBetweenWaveChecks;
		if (!Effects::WingsOfTheHostHasLanded(WingsOfTheHostWarningSoFar))
		{
			return;
		}
		ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		Delivery.DamageType = DungeonGameModeTypeOfRow(Effects::WingsOfTheHostKey);

		// THE PLAYER ONLY, and once however the marks lie. The hazard source's enemies are the
		// player's side, which includes the player's minions; the row names neither them nor any
		// creature, so only a player character is struck. NO LINE OF SIGHT IS ASKED, as no area
		// damage in this game asks it: the row's feathers "pierce terrain".
		TSet<AActor*> Struck;
		for (ACataclysmGroundZone* Mark : WingsOfTheHostMarksNow())
		{
			const FVector Where = Mark->GetActorLocation();
			if (Source)
			{
				for (AActor* Target : UCataclysmTargeting::FindEnemiesInLine(
						 World, Source, Where, Where, Effects::WingsOfTheHostFeatherRadiusCm))
				{
					if (!Cast<ACataclysmPlayerCharacter>(Target) || Struck.Contains(Target))
					{
						continue;
					}
					Struck.Add(Target);
					const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Target);
					const float Damage = Theirs
						? Effects::WingsOfTheHostDamage(Theirs->GetNumericAttribute(Vital::GetMaxHealthAttribute()))
						: 0.0f;
					if (Damage > 0.0f)
					{
						UCataclysmSkillEffects::ApplyDirectDamage(Source, Target, Damage, Delivery);
					}
				}
			}
			Mark->Destroy();
		}
		WingsOfTheHostMarks.Reset();
		WingsOfTheHostWarningSoFar = 0.0f;
		WingsOfTheHostSecondsSinceLast = 0.0f;
		RefreshFloorModifierPanel();
		return;
	}

	WingsOfTheHostSecondsSinceLast += SecondsBetweenWaveChecks;
	if (!Effects::WingsOfTheHostIsDue(WingsOfTheHostSecondsSinceLast))
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}

	// A STRAIGHT LINE AT A RANDOM ANGLE through the middle of a random floor cell within reach of
	// the player, across the whole floor, marked with harmless circles drawn in the row's type, as
	// Artillery Strike marks its one. THROUGH A FLOOR CELL, so the line always marks that cell's
	// feather and every flyover keeps to thirty seconds.
	const FVector Centre = Player->GetActorLocation();
	const TArray<FIntPoint> Cells = WingsOfTheHostThroughCells(*CurrentFloor, Centre);
	if (Cells.IsEmpty())
	{
		// ONLY A PLAYER OFF THE FLOOR has no cell in reach; the next beat tries again.
		return;
	}
	const FVector Middle = CurrentFloor->WorldOfCell(Cells[FMath::RandRange(0, Cells.Num() - 1)]);
	const FVector Through(Middle.X, Middle.Y, Centre.Z);
	const float Heading = FMath::FRandRange(0.0f, PI);
	const FName Type = DungeonGameModeTypeOfRow(Effects::WingsOfTheHostKey);
	for (const FVector& Where : WingsOfTheHostFeathers(
			 *CurrentFloor, Through, FVector(FMath::Cos(Heading), FMath::Sin(Heading), 0.0f)))
	{
		if (ACataclysmGroundZone* Mark = ACataclysmGroundZone::SpawnForTheFloor(
				Source, Where, Where, Effects::WingsOfTheHostFeatherRadiusCm, 0.0f,
				/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type))
		{
			WingsOfTheHostMarks.Add(Mark);
		}
	}
	WingsOfTheHostWarningSoFar = 0.0f;
	if (WingsOfTheHostMarks.IsEmpty())
	{
		// NO MARK COULD BE SPAWNED; the next beat tries again.
		return;
	}
	UE_LOG(LogCataclysm, Log, TEXT("Wings of the Host: %d feathers marked on floor %d"),
		   WingsOfTheHostMarks.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::EternalChorusSourcesNow() const
{
	TArray<ACataclysmEnemyCharacter*> Singing;
	for (const FEternalChorus& One : EternalChoruses)
	{
		ACataclysmEnemyCharacter* Source = One.Source.Get();
		if (IsValid(Source) && !UCataclysmSkillEffects::IsDead(Source))
		{
			Singing.Add(Source);
		}
	}
	return Singing;
}

ACataclysmGroundZone* ACataclysmDungeonGameMode::EternalChorusEarshotOf(
	const ACataclysmEnemyCharacter* Source) const
{
	for (const FEternalChorus& One : EternalChoruses)
	{
		if (Source && One.Source.Get() == Source)
		{
			return One.Earshot.Get();
		}
	}
	return nullptr;
}

TArray<FIntPoint> ACataclysmDungeonGameMode::EternalChorusCells(const ACataclysmDungeonFloor& Floor,
																 int32 Count)
{
	return FloorSourceCells(Floor, Count, /*bBesideAWallOnly=*/false);
}

bool ACataclysmDungeonGameMode::InfestedVeinsCellIsBesideAWall(const FCataclysmFloorPlan& Plan, FIntPoint Cell)
{
	if (!Plan.IsFloor(Cell))
	{
		return false;
	}
	// `IsFloor` answers false off the plan, so a floor cell on the plan's edge is beside a wall.
	return !Plan.IsFloor(Cell + FIntPoint(1, 0)) || !Plan.IsFloor(Cell + FIntPoint(-1, 0))
		|| !Plan.IsFloor(Cell + FIntPoint(0, 1)) || !Plan.IsFloor(Cell + FIntPoint(0, -1));
}

TArray<FIntPoint> ACataclysmDungeonGameMode::InfestedVeinsCells(const ACataclysmDungeonFloor& Floor, int32 Count)
{
	return FloorSourceCells(Floor, Count, /*bBesideAWallOnly=*/true);
}

TArray<FIntPoint> ACataclysmDungeonGameMode::FloorSourceCells(const ACataclysmDungeonFloor& Floor, int32 Count,
															  bool bBesideAWallOnly)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// EVERY FLOOR CELL FAR ENOUGH FROM THE ENTRANCE, in an even shuffle, and then the first that are
	// far enough from every one already taken.
	const FCataclysmFloorPlan& Plan = Floor.GetPlan();
	const FVector Entrance = Floor.EntranceWorld();
	TArray<FIntPoint> Candidates;
	for (int32 Y = 0; Y < Plan.Height; ++Y)
	{
		for (int32 X = 0; X < Plan.Width; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (Plan.IsFloor(Cell) && (!bBesideAWallOnly || InfestedVeinsCellIsBesideAWall(Plan, Cell))
				&& FVector::Dist2D(Floor.WorldOfCell(Cell), Entrance) >= Effects::EternalChorusApartCm)
			{
				Candidates.Add(Cell);
			}
		}
	}
	for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
	{
		Candidates.Swap(Index, FMath::RandRange(0, Index));
	}

	TArray<FIntPoint> Chosen;
	for (const FIntPoint& Cell : Candidates)
	{
		if (Chosen.Num() >= Count)
		{
			break;
		}
		const FVector Where = Floor.WorldOfCell(Cell);
		const bool bApart = !Chosen.ContainsByPredicate([&Floor, &Where](const FIntPoint& Taken)
		{
			return FVector::Dist2D(Floor.WorldOfCell(Taken), Where) < Effects::EternalChorusApartCm;
		});
		if (bApart)
		{
			Chosen.Add(Cell);
		}
	}
	return Chosen;
}

void ACataclysmDungeonGameMode::ForgetTheChoruses()
{
	for (const FEternalChorus& One : EternalChoruses)
	{
		if (ACataclysmEnemyCharacter* Source = One.Source.Get())
		{
			Source->Destroy();
		}
		if (ACataclysmGroundZone* Earshot = One.Earshot.Get())
		{
			Earshot->Destroy();
		}
	}
	EternalChoruses.Reset();
	EternalChorusPanelSinging = -1;
}

void ACataclysmDungeonGameMode::PlaceTheChoruses()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::EternalChorusKey)))
	{
		return;
	}

	// TWO ON A FLOOR, ONE ON A HORDE ARENA, as ruled.
	const int32 Count = FloorBrief.bWaveWalksIn ? Effects::EternalChorusHordeSources
											: Effects::EternalChorusSources;
	const TSubclassOf<ACataclysmEnemyCharacter> Class = ACataclysmChorusSourceCharacter::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FIntPoint& Cell : EternalChorusCells(*CurrentFloor, Count))
	{
		const FVector Where = CurrentFloor->WorldOfCell(Cell)
			+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));
		ACataclysmEnemyCharacter* Source =
			World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Spawn);
		if (!Source)
		{
			continue;
		}
		// THE IMP'S HEALTH AT COMMON, a play-test value; see `EternalChorusSourceHealth`. It pays
		// nothing, so it cannot be farmed, and it is not one of the floor's
		// creatures, so Blood Gates and every rule that picks from those leave it alone.
		Source->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Source->SetHealth(EternalChorusSourceHealth());
		Source->SetRarityStep(0);
		Source->bDiesUnpaid = true;
		Source->bRaisedByARule = true;
		CreaturesRaisedByARule.Add(Source);
		FEternalChorus One;
		One.Source = Source;
		EternalChoruses.Add(One);
	}
	UE_LOG(LogCataclysm, Log, TEXT("Eternal Chorus: %d source(s) placed on floor %d"),
		   EternalChoruses.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepEternalChorus(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	const FName Type = DungeonGameModeTypeOfRow(Effects::EternalChorusKey);
	const FVector Feet = Player->GetActorLocation();

	// A SOURCE DESTROYED SILENCES ITS CHORUS: its earshot goes. A LIVING SOURCE'S EARSHOT IS DRAWN
	// AGAIN whenever it is missing, which is after every floor or wave, when the rules' zones go.
	bool bWithinEarshot = false;
	for (FEternalChorus& One : EternalChoruses)
	{
		ACataclysmEnemyCharacter* Singer = One.Source.Get();
		if (!IsValid(Singer) || UCataclysmSkillEffects::IsDead(Singer))
		{
			if (ACataclysmGroundZone* Earshot = One.Earshot.Get())
			{
				Earshot->Destroy();
			}
			One.Earshot = nullptr;
			continue;
		}
		ACataclysmGroundZone* Earshot = One.Earshot.Get();
		if (!Earshot && Source)
		{
			const FVector Where = Singer->GetActorLocation();
			Earshot = ACataclysmGroundZone::SpawnForTheFloor(
				Source, Where, Where, Effects::EternalChorusEarshotCm, 0.0f,
				/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type);
			One.Earshot = Earshot;
		}
		bWithinEarshot |= Earshot && Earshot->Covers(Feet);
	}
	EternalChoruses.RemoveAll([](const FEternalChorus& One)
	{
		return !One.Source.IsValid() || UCataclysmSkillEffects::IsDead(One.Source.Get());
	});

	// WITHIN ANY EARSHOT, ONCE: the two figures, or nothing. Written only when they change, since
	// the applier works the character's stats out again.
	const float WantedCooldown = bWithinEarshot ? Effects::EternalChorusCooldownLongerPercent : 0.0f;
	const float WantedRegen = bWithinEarshot ? Effects::EternalChorusRegenLessPercent : 0.0f;
	if (!FMath::IsNearlyEqual(WantedCooldown, EternalChorusCooldownApplied)
		|| !FMath::IsNearlyEqual(WantedRegen, EternalChorusRegenApplied))
	{
		EternalChorusCooldownApplied = WantedCooldown;
		EternalChorusRegenApplied = WantedRegen;
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}

	const int32 Singing = EternalChoruses.Num();
	if (Singing != EternalChorusPanelSinging)
	{
		EternalChorusPanelSinging = Singing;
		RefreshFloorModifierPanel();
	}
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::NecroticBloomFlowersNow() const
{
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (const FNecroticBloom& One : NecroticBlooms)
	{
		ACataclysmEnemyCharacter* Flower = One.Flower.Get();
		if (IsValid(Flower) && !UCataclysmSkillEffects::IsDead(Flower))
		{
			Standing.Add(Flower);
		}
	}
	return Standing;
}

int32 ACataclysmDungeonGameMode::NecroticBloomWavesOf(const ACataclysmEnemyCharacter* Flower) const
{
	for (const FNecroticBloom& One : NecroticBlooms)
	{
		if (Flower && One.Flower.Get() == Flower)
		{
			return One.Waves;
		}
	}
	return -1;
}

TArray<FIntPoint> ACataclysmDungeonGameMode::NecroticBloomWaveCells(const ACataclysmDungeonFloor& Floor,
																	const FVector& Flower)
{
	using Effects = UCataclysmDungeonModifierEffects;

	const FCataclysmFloorPlan& Plan = Floor.GetPlan();
	const FIntPoint Own = Floor.CellOfWorld(Flower);
	TArray<FIntPoint> Cells;
	for (int32 Y = 0; Y < Plan.Height; ++Y)
	{
		for (int32 X = 0; X < Plan.Width; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (Cell != Own && Plan.IsFloor(Cell)
				&& FVector::Dist2D(Floor.WorldOfCell(Cell), Flower) <= Effects::NecroticBloomWaveWithinCm)
			{
				Cells.Add(Cell);
			}
		}
	}
	// A FLOWER WITH NO FLOOR BESIDE IT sends its wave onto its own cell rather than none.
	if (Cells.IsEmpty() && Plan.IsFloor(Own))
	{
		Cells.Add(Own);
	}
	return Cells;
}

void ACataclysmDungeonGameMode::ForgetTheBlooms()
{
	for (const FNecroticBloom& One : NecroticBlooms)
	{
		if (ACataclysmEnemyCharacter* Flower = One.Flower.Get())
		{
			Flower->Destroy();
		}
	}
	NecroticBlooms.Reset();
	NecroticBloomPanelFlowers = -1;
}

void ACataclysmDungeonGameMode::PlaceTheBlooms()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::NecroticBloomKey)))
	{
		return;
	}

	// TWO ON A FLOOR, ONE ON A HORDE ARENA, WHERE ETERNAL CHORUS'S SOURCES WOULD STAND, as ruled.
	const int32 Count = FloorBrief.bWaveWalksIn ? Effects::NecroticBloomHordeFlowers
											: Effects::NecroticBloomFlowers;
	const TSubclassOf<ACataclysmEnemyCharacter> Class = ACataclysmBloomCharacter::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FIntPoint& Cell : EternalChorusCells(*CurrentFloor, Count))
	{
		const FVector Where = CurrentFloor->WorldOfCell(Cell)
			+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));
		ACataclysmEnemyCharacter* Flower =
			World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Spawn);
		if (!Flower)
		{
			continue;
		}
		// THE IMP'S HEALTH AT COMMON, a play-test value. It pays nothing and is not one of the floor's
		// creatures, as a chorus source is not; its waves are, and they pay.
		Flower->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Flower->SetHealth(NecroticBloomFlowerHealth());
		Flower->SetRarityStep(0);
		Flower->bDiesUnpaid = true;
		Flower->bRaisedByARule = true;
		CreaturesRaisedByARule.Add(Flower);
		FNecroticBloom One;
		One.Flower = Flower;
		NecroticBlooms.Add(One);
	}
	UE_LOG(LogCataclysm, Log, TEXT("Necrotic Bloom: %d flower(s) placed on floor %d"),
		   NecroticBlooms.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepNecroticBloom()
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!GetWorld() || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// A DESTROYED FLOWER SENDS NOTHING MORE, and its waves' creatures stay: they are the floor's.
	NecroticBlooms.RemoveAll([](const FNecroticBloom& One)
	{
		return !One.Flower.IsValid() || UCataclysmSkillEffects::IsDead(One.Flower.Get());
	});

	bool bSent = false;
	TOptional<FCataclysmFloorPopulation> Population;
	for (FNecroticBloom& One : NecroticBlooms)
	{
		One.SecondsSinceLastWave += SecondsBetweenWaveChecks;
		if (!Effects::NecroticBloomWaveIsDue(One.SecondsSinceLastWave, One.Waves))
		{
			continue;
		}

		// THE FLOOR'S OWN KINDS, as Grave Tide draws them -- its reading of "undead" -- asked once a
		// beat and only when a wave is due.
		if (!Population.IsSet())
		{
			Population = FCataclysmFloorPopulator::Populate(CurrentFloor->GetPlan(), ChooseEnemyScale(), FloorBrief);
		}
		const TArray<FIntPoint> Cells =
			NecroticBloomWaveCells(*CurrentFloor, One.Flower.Get()->GetActorLocation());
		if (Population->Enemies.IsEmpty() || Cells.IsEmpty())
		{
			// NOTHING TO PLACE IS NOT A WAVE, and the clock is left alone so the next beat asks again.
			continue;
		}

		int32 Placed = 0;
		for (int32 Which = 0; Which < Effects::NecroticBloomCreaturesPerWave; ++Which)
		{
			FCataclysmEnemyPlacement Placement =
				Population->Enemies[FMath::RandRange(0, Population->Enemies.Num() - 1)];
			Placement.Cell = Cells[FMath::RandRange(0, Cells.Num() - 1)];
			ACataclysmEnemyCharacter* Risen =
				SpawnPlacedCreature(Placement, FloorBrief.SightRadiusMultiplier, /*FixedRung=*/0);
			if (!Risen)
			{
				continue;
			}
			// THE FLOOR'S LIST, so a floor change disposes of them with the rest, and they pay and are
			// saved like any creature, as Grave Tide's do.
			FloorEnemies.Add(Risen);
			++Placed;
		}
		if (Placed <= 0)
		{
			continue;
		}
		++One.Waves;
		One.SecondsSinceLastWave = 0.0f;
		bSent = true;
		UE_LOG(LogCataclysm, Verbose, TEXT("Necrotic Bloom: a flower's wave %d of %d put %d creature%s on floor %d."),
			   One.Waves, Effects::NecroticBloomMostWaves, Placed, Placed == 1 ? TEXT("") : TEXT("s"),
			   FloorNumber);
	}

	const int32 Standing = NecroticBlooms.Num();
	if (bSent || Standing != NecroticBloomPanelFlowers)
	{
		NecroticBloomPanelFlowers = Standing;
		RefreshFloorModifierPanel();
	}
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::GoldenSpiresStanding() const
{
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (const FGoldenSpire& One : GoldenSpires)
	{
		ACataclysmEnemyCharacter* Spire = One.Spire.Get();
		if (IsValid(Spire) && !UCataclysmSkillEffects::IsDead(Spire))
		{
			Standing.Add(Spire);
		}
	}
	return Standing;
}

ACataclysmGroundZone* ACataclysmDungeonGameMode::GoldenSpireZoneOf(const ACataclysmEnemyCharacter* Spire) const
{
	for (const FGoldenSpire& One : GoldenSpires)
	{
		if (Spire && One.Spire.Get() == Spire)
		{
			return One.Zone.Get();
		}
	}
	return nullptr;
}

void ACataclysmDungeonGameMode::ForgetTheSpires()
{
	for (const FGoldenSpire& One : GoldenSpires)
	{
		if (ACataclysmEnemyCharacter* Spire = One.Spire.Get())
		{
			Spire->Destroy();
		}
		if (ACataclysmGroundZone* Zone = One.Zone.Get())
		{
			Zone->Destroy();
		}
	}
	GoldenSpires.Reset();
	GoldenSpiresPanelStanding = -1;
}

void ACataclysmDungeonGameMode::PlaceTheSpires()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::GoldenSpiresKey)))
	{
		return;
	}

	// TWO ON A FLOOR, ONE ON A HORDE ARENA, WHERE ETERNAL CHORUS'S SOURCES WOULD STAND, as ruled.
	const int32 Count = FloorBrief.bWaveWalksIn ? Effects::GoldenSpiresPerHordeArena
											: Effects::GoldenSpiresPerFloor;
	const TSubclassOf<ACataclysmEnemyCharacter> Class = ACataclysmSpireCharacter::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FIntPoint& Cell : EternalChorusCells(*CurrentFloor, Count))
	{
		const FVector Where = CurrentFloor->WorldOfCell(Cell)
			+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));
		ACataclysmEnemyCharacter* Spire =
			World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Spawn);
		if (!Spire)
		{
			continue;
		}
		// THE IMP'S HEALTH AT COMMON, a play-test value. It pays nothing and is not one of the floor's
		// creatures, as the other floor sources are not.
		Spire->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Spire->SetHealth(GoldenSpireHealth());
		Spire->SetRarityStep(0);
		Spire->bDiesUnpaid = true;
		Spire->bRaisedByARule = true;
		// FIELD MEDIC'S HEAL, UNCHANGED, as ruled: the creature's own aura pulse heals its allies
		// within that heal's radius while this is set, brain or none.
		Spire->bHealsAlliesForTheFloorRule = true;
		CreaturesRaisedByARule.Add(Spire);
		FGoldenSpire One;
		One.Spire = Spire;
		GoldenSpires.Add(One);
	}
	UE_LOG(LogCataclysm, Log, TEXT("Golden Spires: %d spire(s) placed on floor %d"),
		   GoldenSpires.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepGoldenSpires(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player))
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	const FName Type = DungeonGameModeTypeOfRow(Effects::GoldenSpiresKey);

	// A DESTROYED SPIRE'S ZONE GOES; A LIVING SPIRE'S ZONE IS DRAWN AGAIN whenever it is missing, which
	// is after every floor or wave, when the rules' zones go.
	TArray<FVector> Standing;
	for (FGoldenSpire& One : GoldenSpires)
	{
		ACataclysmEnemyCharacter* Tower = One.Spire.Get();
		if (!IsValid(Tower) || UCataclysmSkillEffects::IsDead(Tower))
		{
			if (ACataclysmGroundZone* Zone = One.Zone.Get())
			{
				Zone->Destroy();
			}
			One.Zone = nullptr;
			continue;
		}
		const FVector Where = Tower->GetActorLocation();
		Standing.Add(Where);
		if (!One.Zone.Get() && Source)
		{
			One.Zone = ACataclysmGroundZone::SpawnForTheFloor(
				Source, Where, Where, Effects::GoldenSpiresRadiusCm, 0.0f,
				/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type);
		}
	}
	GoldenSpires.RemoveAll([](const FGoldenSpire& One)
	{
		return !One.Spire.IsValid() || UCataclysmSkillEffects::IsDead(One.Spire.Get());
	});

	// EVERY CREATURE ON THE PLAYER'S OTHER SIDE: MORE DAMAGE WITHIN REACH OF ANY LIVING SPIRE, once
	// however many, and its own damage again elsewhere. The sweep Commander's Aura makes; the floor
	// sources themselves are left alone, since they do nothing.
	const float Near = 1.0f + Effects::GoldenSpiresDamageMorePercent / 100.0f;
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		ACataclysmEnemyCharacter* Creature = *It;
		if (!IsValid(Creature) || Creature->IsA<ACataclysmFloorSourceCharacter>()
			|| !UCataclysmTargeting::IsHostileTo(Creature, Player))
		{
			continue;
		}
		const FVector At = Creature->GetActorLocation();
		const bool bNear = Standing.ContainsByPredicate([&At](const FVector& Where)
		{
			return FVector::Dist2D(At, Where) <= Effects::GoldenSpiresRadiusCm;
		});
		Creature->SetSpireDamageMultiplier(bNear ? Near : 1.0f);
	}

	const int32 Count = GoldenSpires.Num();
	if (Count != GoldenSpiresPanelStanding)
	{
		GoldenSpiresPanelStanding = Count;
		RefreshFloorModifierPanel();
	}
}

int32 ACataclysmDungeonGameMode::LivingFloorEnemies() const
{
	int32 Living = 0;
	for (const TObjectPtr<ACataclysmEnemyCharacter>& Creature : FloorEnemies)
	{
		Living += (IsValid(Creature) && !UCataclysmSkillEffects::IsDead(Creature)) ? 1 : 0;
	}
	return Living;
}

void ACataclysmDungeonGameMode::NoteTheFloorsClearTime()
{
	if (!CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}
	FloorSecondsSincePlaced += SecondsBetweenWaveChecks;
	if (FloorClearedSeconds >= 0.0f || !FloorIsCleared())
	{
		return;
	}
	// ONCE A FLOOR OR WAVE, THE FIRST TIME NONE OF ITS CREATURES IS ALIVE.
	FloorClearedSeconds = FloorSecondsSincePlaced;
	UE_LOG(LogCataclysm, Log, TEXT("Floor %d cleared: %d floor cells, %.1f seconds after it was placed%s"),
		   FloorNumber, CurrentFloor->GetPlan().FloorCount(), FloorClearedSeconds,
		   FloorBrief.bWaveWalksIn ? TEXT(" (a Horde wave)") : TEXT(""));
}

void ACataclysmDungeonGameMode::StepTrialOfEndurance(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || FloorBrief.bWaveWalksIn)
	{
		// NO TIMER ON A HORDE FLOOR, as ruled: its next wave walks in before the last is cleared.
		return;
	}

	// THE CLOCK, UNTIL THE FLOOR IS CLEARED OR IT RUNS OUT. Cleared is asked first, so a floor cleared on the
	// beat it would have run out is cleared in time.
	if (!bTrialClearedInTime && !bTrialRanOut)
	{
		if (FloorIsCleared())
		{
			bTrialClearedInTime = true;
			UE_LOG(LogCataclysm, Log, TEXT("Trial of Endurance: floor %d cleared in time, %.1f seconds"),
				   FloorNumber, TrialSeconds);
			RefreshFloorModifierPanel();
			return;
		}
		TrialSeconds += SecondsBetweenWaveChecks;
		if (Effects::TrialOfEnduranceHasRunOut(TrialSeconds))
		{
			bTrialRanOut = true;
			UE_LOG(LogCataclysm, Log, TEXT("Trial of Endurance: floor %d ran out with %d creatures alive"),
				   FloorNumber, LivingFloorEnemies());
			RefreshFloorModifierPanel();
		}
	}

	if (bTrialRanOut)
	{
		// EVERY CREATURE ON THE PLAYER'S OTHER SIDE BUT A FLOOR SOURCE, later arrivals on the beat they are
		// first found: double damage through the map, and twice its own all-resistance BASE, by Soul Harvest's
		// route of writing the base and keeping what was written, which `SetRuleResistance` holds.
		for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
		{
			ACataclysmEnemyCharacter* Creature = *It;
			if (!IsValid(Creature) || Creature->IsA<ACataclysmFloorSourceCharacter>()
				|| !UCataclysmTargeting::IsHostileTo(Creature, Player))
			{
				continue;
			}
			Creature->SetTrialOfEnduranceDamageMultiplier(Effects::TrialOfEnduranceDamageMultiplier);

			// AND TWICE ITS OWN ALL-RESISTANCE, through the record every rule writes resistance through, so a coffin's
			// points beside it are doubled with it and neither rule overwrites the other.
			SetRuleResistance(Creature, TrialOfEnduranceResistanceSource, 0.0f,
							  Effects::TrialOfEnduranceResistanceMultiplier);
		}
	}

	const int32 Living = LivingFloorEnemies();
	if (Living != TrialPanelLiving)
	{
		TrialPanelLiving = Living;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::SetRuleResistance(ACataclysmEnemyCharacter* Creature, const TCHAR* Source,
												   float Added, float Multiplier)
{
	using Resist = UCataclysmAllResistanceAttributeSet;

	UAbilitySystemComponent* Abilities = IsValid(Creature) ? UCataclysmTargeting::AbilitySystemOf(Creature) : nullptr;
	if (!Abilities || !Abilities->HasAttributeSetForAttribute(Resist::GetAllResistanceAttribute()))
	{
		return;
	}
	const float Base = Abilities->GetNumericAttributeBase(Resist::GetAllResistanceAttribute());
	const bool bNothing = FMath::IsNearlyZero(Added) && FMath::IsNearlyEqual(Multiplier, 1.0f);

	FRuleResistance* Held = RuleResistances.Find(Creature);
	if (!Held)
	{
		// A RULE THAT DOES NOTHING TO A CREATURE NO RULE HAS WRITTEN leaves no record and writes nothing.
		if (bNothing)
		{
			return;
		}
		Held = &RuleResistances.Add(Creature);
		Held->Own = Base;
		Held->Applied = Base;
	}
	else if (!FMath::IsNearlyEqual(Base, Held->Applied, 0.01f) && !FMath::IsNearlyEqual(Base, Held->Own, 0.01f))
	{
		// ANOTHER WRITER'S CHANGE, kept: its own figure moves by that much before the rules are applied again.
		// A base back at its own figure is a recompute, and the rules are simply written over it again.
		Held->Own += Base - Held->Applied;
	}

	if (bNothing)
	{
		Held->AddedBySource.Remove(FName(Source));
		Held->MultipliedBySource.Remove(FName(Source));
	}
	else
	{
		Held->AddedBySource.Add(FName(Source), Added);
		Held->MultipliedBySource.Add(FName(Source), Multiplier);
	}

	// POINTS FIRST, THEN MULTIPLIERS, as ruled.
	float Sum = Held->Own;
	for (const TPair<FName, float>& Entry : Held->AddedBySource)
	{
		Sum += Entry.Value;
	}
	float Product = 1.0f;
	for (const TPair<FName, float>& Entry : Held->MultipliedBySource)
	{
		Product *= Entry.Value;
	}
	const float Wanted = Sum * Product;
	if (!FMath::IsNearlyEqual(Base, Wanted, 0.01f))
	{
		Abilities->SetNumericAttributeBase(Resist::GetAllResistanceAttribute(), Wanted);
	}
	Held->Applied = Wanted;
}

void ACataclysmDungeonGameMode::ForgetRuleResistances()
{
	using Resist = UCataclysmAllResistanceAttributeSet;

	for (const TPair<TWeakObjectPtr<ACataclysmEnemyCharacter>, FRuleResistance>& Entry : RuleResistances)
	{
		ACataclysmEnemyCharacter* Creature = Entry.Key.Get();
		UAbilitySystemComponent* Abilities = IsValid(Creature) ? UCataclysmTargeting::AbilitySystemOf(Creature) : nullptr;
		if (!Abilities || !Abilities->HasAttributeSetForAttribute(Resist::GetAllResistanceAttribute()))
		{
			continue;
		}
		// ITS OWN FIGURE BACK, with any other writer's change to it kept.
		const float Base = Abilities->GetNumericAttributeBase(Resist::GetAllResistanceAttribute());
		Abilities->SetNumericAttributeBase(Resist::GetAllResistanceAttribute(),
										   Entry.Value.Own + (Base - Entry.Value.Applied));
	}
	RuleResistances.Reset();
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::SarcophagiNow() const
{
	TArray<ACataclysmEnemyCharacter*> Now;
	for (const FSarcophagus& One : Sarcophagi)
	{
		if (ACataclysmEnemyCharacter* Coffin = One.Coffin.Get(); IsValid(Coffin))
		{
			Now.Add(Coffin);
		}
	}
	return Now;
}

ACataclysmGroundZone* ACataclysmDungeonGameMode::SarcophagusZoneOf(const ACataclysmEnemyCharacter* Coffin) const
{
	for (const FSarcophagus& One : Sarcophagi)
	{
		if (Coffin && One.Coffin.Get() == Coffin)
		{
			return One.Zone.Get();
		}
	}
	return nullptr;
}

int32 ACataclysmDungeonGameMode::SarcophagusDeathsBeside(const ACataclysmEnemyCharacter* Coffin) const
{
	for (const FSarcophagus& One : Sarcophagi)
	{
		if (Coffin && One.Coffin.Get() == Coffin)
		{
			return One.Deaths;
		}
	}
	return 0;
}

bool ACataclysmDungeonGameMode::SarcophagusLordCame(const ACataclysmEnemyCharacter* Coffin) const
{
	for (const FSarcophagus& One : Sarcophagi)
	{
		if (Coffin && One.Coffin.Get() == Coffin)
		{
			return One.bLordCame;
		}
	}
	return false;
}

void ACataclysmDungeonGameMode::ForgetTheSarcophagi()
{
	for (const FSarcophagus& One : Sarcophagi)
	{
		if (ACataclysmEnemyCharacter* Coffin = One.Coffin.Get())
		{
			Coffin->Destroy();
		}
		if (ACataclysmGroundZone* Zone = One.Zone.Get())
		{
			Zone->Destroy();
		}
	}
	Sarcophagi.Reset();
}

void ACataclysmDungeonGameMode::PlaceTheSarcophagi()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::ObsidianSarcophagiKey)))
	{
		return;
	}

	// TWO ON A FLOOR AND ONE ON A HORDE ARENA, KEPT, where Eternal Chorus's picker puts its sources.
	const int32 Count = FloorBrief.bWaveWalksIn ? Effects::ObsidianSarcophagiPerHordeArena
											: Effects::ObsidianSarcophagiPerFloor;
	const TSubclassOf<ACataclysmEnemyCharacter> Class = ACataclysmSarcophagusCharacter::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FIntPoint& Cell : EternalChorusCells(*CurrentFloor, Count))
	{
		const FVector Where = CurrentFloor->WorldOfCell(Cell)
			+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));
		ACataclysmEnemyCharacter* Coffin =
			World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Spawn);
		if (!Coffin)
		{
			continue;
		}
		// INDESTRUCTIBLE, AS THE ROW SAYS: The Reaper's flag, so a blow resolves and none of it reaches health.
		// The Imp's health at Common, a play-test value, paying nothing and not one of the floor's creatures.
		Coffin->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Coffin->SetHealth(SarcophagusHealth());
		Coffin->SetRarityStep(0);
		Coffin->bCannotBeHurt = true;
		Coffin->bDiesUnpaid = true;
		Coffin->bRaisedByARule = true;
		CreaturesRaisedByARule.Add(Coffin);
		FSarcophagus One;
		One.Coffin = Coffin;
		Sarcophagi.Add(One);
	}
	UE_LOG(LogCataclysm, Log, TEXT("Obsidian Sarcophagi: %d coffin(s) placed on floor %d"), Sarcophagi.Num(),
		   FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepObsidianSarcophagi(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player))
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	const FName Type = DungeonGameModeTypeOfRow(Effects::ObsidianSarcophagiKey);

	// EACH COFFIN'S ZONE DRAWN AGAIN WHENEVER IT IS MISSING, which is after every floor or wave. It does no damage.
	TArray<FVector> Coffins;
	for (FSarcophagus& One : Sarcophagi)
	{
		ACataclysmEnemyCharacter* Coffin = One.Coffin.Get();
		if (!IsValid(Coffin))
		{
			continue;
		}
		const FVector Where = Coffin->GetActorLocation();
		Coffins.Add(Where);
		if (!One.Zone.Get() && Source)
		{
			One.Zone = ACataclysmGroundZone::SpawnForTheFloor(
				Source, Where, Where, Effects::ObsidianSarcophagiRadiusCm, 0.0f,
				/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type);
		}
	}

	// EVERY CREATURE ON THE PLAYER'S OTHER SIDE BUT A FLOOR SOURCE, near a coffin or not, every beat, as Golden
	// Spires writes its damage: once however many coffins are near.
	const float Near = 1.0f + Effects::ObsidianSarcophagiDamageMorePercent / 100.0f;
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		ACataclysmEnemyCharacter* Creature = *It;
		if (!IsValid(Creature) || Creature->IsA<ACataclysmFloorSourceCharacter>()
			|| !UCataclysmTargeting::IsHostileTo(Creature, Player))
		{
			continue;
		}
		const FVector At = Creature->GetActorLocation();
		const bool bNear = Coffins.ContainsByPredicate([&At](const FVector& Where)
		{
			return FVector::Dist2D(At, Where) <= Effects::ObsidianSarcophagiRadiusCm;
		});
		Creature->SetObsidianSarcophagiDamageMultiplier(bNear ? Near : 1.0f);
		SetRuleResistance(Creature, ObsidianSarcophagiResistanceSource,
						  bNear ? Effects::ObsidianSarcophagiResistancePoints : 0.0f, 1.0f);
	}
}

void ACataclysmDungeonGameMode::NoteDeathForObsidianSarcophagi(const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::ObsidianSarcophagiKey)) || !CurrentFloor
		|| !CurrentFloor->IsBuilt())
	{
		return;
	}

	// A PAID DEATH OF ONE OF THE FLOOR'S CREATURES, WHOEVER KILLED IT, as ruled.
	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Fallen || !Fallen->PaysForItsDeath() || !FloorEnemies.Contains(Fallen))
	{
		return;
	}

	bool bChanged = false;
	for (FSarcophagus& One : Sarcophagi)
	{
		ACataclysmEnemyCharacter* Coffin = One.Coffin.Get();
		if (!IsValid(Coffin)
			|| FVector::Dist2D(Coffin->GetActorLocation(), Notice.Location) > Effects::ObsidianSarcophagiRadiusCm)
		{
			continue;
		}
		++One.Deaths;
		bChanged = true;
		if (!Effects::ObsidianSarcophagiLordIsDue(One.Deaths, One.bLordCame))
		{
			continue;
		}

		// THE VAMPIRE LORD, ONCE A COFFIN: a creature of the floor's own kinds at Demon Prince's rung, as Grave
		// Tide draws them, on a floor cell beside the coffin as Necrotic Bloom's waves stand beside their flower,
		// seeing across the floor. It pays and is one of the floor's creatures.
		One.bLordCame = true;
		const FCataclysmFloorPopulation Population =
			FCataclysmFloorPopulator::Populate(CurrentFloor->GetPlan(), ChooseEnemyScale(), FloorBrief);
		const TArray<FIntPoint> Cells = NecroticBloomWaveCells(*CurrentFloor, Coffin->GetActorLocation());
		if (Population.Enemies.IsEmpty() || Cells.IsEmpty())
		{
			UE_LOG(LogCataclysm, Log, TEXT("Obsidian Sarcophagi: a coffin on floor %d had no room for its lord"),
				   FloorNumber);
			continue;
		}
		FCataclysmEnemyPlacement Placement =
			Population.Enemies[FMath::RandRange(0, Population.Enemies.Num() - 1)];
		Placement.Cell = Cells[FMath::RandRange(0, Cells.Num() - 1)];
		if (ACataclysmEnemyCharacter* Lord = SpawnPlacedCreature(
				Placement, Effects::VengefulWraithsSightMultiplier, Effects::ObsidianSarcophagiLordRung))
		{
			Lord->bIsAVampireLord = true;
			FloorEnemies.Add(Lord);
			UE_LOG(LogCataclysm, Log, TEXT("Obsidian Sarcophagi: a Vampire Lord (%s) came on floor %d"),
				   CataclysmDungeonCreatureName(Placement.Creature), FloorNumber);
		}
	}
	if (bChanged)
	{
		RefreshFloorModifierPanel();
	}
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::PlagueBeaconsStanding() const
{
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (const FPlagueBeacon& One : PlagueBeacons)
	{
		ACataclysmEnemyCharacter* Beacon = One.Beacon.Get();
		if (IsValid(Beacon) && !UCataclysmSkillEffects::IsDead(Beacon))
		{
			Standing.Add(Beacon);
		}
	}
	return Standing;
}

void ACataclysmDungeonGameMode::ForgetTheBeacons()
{
	for (const FPlagueBeacon& One : PlagueBeacons)
	{
		if (ACataclysmEnemyCharacter* Beacon = One.Beacon.Get())
		{
			Beacon->Destroy();
		}
	}
	PlagueBeacons.Reset();
	PestilentPanelStanding = -1;
}

void ACataclysmDungeonGameMode::CountThePlagueBeaconsLeftStanding()
{
	int32 Counted = 0;
	for (FPlagueBeacon& One : PlagueBeacons)
	{
		// A DESTROYED BEACON ADDS NOTHING, and a Horde arena's beacon, kept for the waves after its first,
		// is counted once.
		const ACataclysmEnemyCharacter* Beacon = One.Beacon.Get();
		if (!IsValid(Beacon) || UCataclysmSkillEffects::IsDead(Beacon) || One.bCounted)
		{
			continue;
		}
		One.bCounted = true;
		++Counted;
	}
	if (Counted > 0)
	{
		PestilentBeaconsLeftStanding += Counted;
		UE_LOG(LogCataclysm, Log, TEXT("Pestilent Empowerment: %d beacon(s) left standing, %d in this dungeon"),
			   Counted, PestilentBeaconsLeftStanding);
	}
}

void ACataclysmDungeonGameMode::PlaceTheBeacons()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::PestilentEmpowermentKey)))
	{
		return;
	}

	// TWO ON A FLOOR, ONE ON A HORDE ARENA, WHERE ETERNAL CHORUS'S SOURCES WOULD STAND, as ruled.
	const int32 Count = FloorBrief.bWaveWalksIn ? Effects::PestilentEmpowermentBeaconsPerHordeArena
											: Effects::PestilentEmpowermentBeaconsPerFloor;
	const TSubclassOf<ACataclysmEnemyCharacter> Class = ACataclysmBeaconCharacter::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FIntPoint& Cell : EternalChorusCells(*CurrentFloor, Count))
	{
		const FVector Where = CurrentFloor->WorldOfCell(Cell)
			+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));
		ACataclysmEnemyCharacter* Beacon =
			World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Spawn);
		if (!Beacon)
		{
			continue;
		}
		// THE IMP'S HEALTH AT COMMON, a play-test value. It pays nothing and is not one of the floor's
		// creatures, as the other floor sources are not.
		Beacon->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Beacon->SetHealth(PlagueBeaconHealth());
		Beacon->SetRarityStep(0);
		Beacon->bDiesUnpaid = true;
		Beacon->bRaisedByARule = true;
		CreaturesRaisedByARule.Add(Beacon);
		FPlagueBeacon One;
		One.Beacon = Beacon;
		PlagueBeacons.Add(One);
	}
	UE_LOG(LogCataclysm, Log, TEXT("Pestilent Empowerment: %d beacon(s) placed on floor %d"),
		   PlagueBeacons.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::VoidPortalsNow() const
{
	TArray<ACataclysmEnemyCharacter*> Now;
	for (const FVoidPortal& One : VoidPortals)
	{
		if (ACataclysmEnemyCharacter* Portal = One.Portal.Get(); IsValid(Portal))
		{
			Now.Add(Portal);
		}
	}
	return Now;
}

ACataclysmGroundZone* ACataclysmDungeonGameMode::VoidPortalZoneOf(const ACataclysmEnemyCharacter* Portal) const
{
	for (const FVoidPortal& One : VoidPortals)
	{
		if (Portal && One.Portal.Get() == Portal)
		{
			return One.Zone.Get();
		}
	}
	return nullptr;
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::AbominationsOf(const ACataclysmEnemyCharacter* Portal) const
{
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (const FVoidPortal& One : VoidPortals)
	{
		if (!Portal || One.Portal.Get() != Portal)
		{
			continue;
		}
		for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Sent : One.Sent)
		{
			ACataclysmEnemyCharacter* Creature = Sent.Get();
			if (IsValid(Creature) && !UCataclysmSkillEffects::IsDead(Creature))
			{
				Standing.Add(Creature);
			}
		}
	}
	return Standing;
}

void ACataclysmDungeonGameMode::ForgetThePortals()
{
	for (const FVoidPortal& One : VoidPortals)
	{
		if (ACataclysmEnemyCharacter* Portal = One.Portal.Get())
		{
			Portal->Destroy();
		}
		if (ACataclysmGroundZone* Zone = One.Zone.Get())
		{
			Zone->Destroy();
		}
		for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Sent : One.Sent)
		{
			if (ACataclysmEnemyCharacter* Creature = Sent.Get())
			{
				Creature->Destroy();
			}
		}
	}
	VoidPortals.Reset();
	VoidPortalsPanelStanding = -1;
}

void ACataclysmDungeonGameMode::PlaceThePortals()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::PortalUnleashingKey)))
	{
		return;
	}

	// TWO ON A FLOOR AND ONE ON A HORDE ARENA, KEPT, where Eternal Chorus's picker puts its sources.
	const int32 Count = FloorBrief.bWaveWalksIn ? Effects::PortalUnleashingPerHordeArena
											: Effects::PortalUnleashingPerFloor;
	const TSubclassOf<ACataclysmEnemyCharacter> Class = ACataclysmPortalCharacter::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FIntPoint& Cell : EternalChorusCells(*CurrentFloor, Count))
	{
		const FVector Where = CurrentFloor->WorldOfCell(Cell)
			+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));
		ACataclysmEnemyCharacter* Portal =
			World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Spawn);
		if (!Portal)
		{
			continue;
		}
		// CANNOT BE HURT, the ruled reading of "unstable": The Reaper's flag, so a blow resolves and none of it
		// reaches health. The Imp's health at Common, paying nothing and not one of the floor's creatures.
		Portal->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Portal->SetHealth(VoidPortalHealth());
		Portal->SetRarityStep(0);
		Portal->bCannotBeHurt = true;
		Portal->bDiesUnpaid = true;
		Portal->bRaisedByARule = true;
		CreaturesRaisedByARule.Add(Portal);
		FVoidPortal One;
		One.Portal = Portal;
		VoidPortals.Add(One);
	}
	UE_LOG(LogCataclysm, Log, TEXT("Portal Unleashing: %d portal(s) placed on floor %d"), VoidPortals.Num(),
		   FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepPortalUnleashing()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	const FName Type = DungeonGameModeTypeOfRow(Effects::PortalUnleashingKey);

	TOptional<FCataclysmFloorPopulation> Population;
	for (FVoidPortal& One : VoidPortals)
	{
		ACataclysmEnemyCharacter* Portal = One.Portal.Get();
		if (!IsValid(Portal))
		{
			continue;
		}
		const FVector Where = Portal->GetActorLocation();

		// ITS ZONE DRAWN AGAIN WHENEVER IT IS MISSING, which is after every floor or wave. It does no damage.
		if (!One.Zone.Get() && Source)
		{
			One.Zone = ACataclysmGroundZone::SpawnForTheFloor(
				Source, Where, Where, Effects::PortalUnleashingRadiusCm, 0.0f,
				/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type);
		}

		// WHAT IT SENT THAT IS DEAD OR GONE IS FORGOTTEN, so only the standing count against the cap. In play a
		// dead creature is destroyed on the next tick; a test world keeps it.
		One.Sent.RemoveAll([](const TWeakObjectPtr<ACataclysmEnemyCharacter>& Sent)
		{
			return !Sent.IsValid() || UCataclysmSkillEffects::IsDead(Sent.Get());
		});

		// THE CLOCK RUNS WHILE THE CAP IS FULL, so a creature killed after ten seconds is replaced on the next beat.
		One.SecondsSinceLastSent += SecondsBetweenWaveChecks;
		if (!Effects::PortalUnleashingSendsNow(One.SecondsSinceLastSent, One.Sent.Num()))
		{
			continue;
		}

		// THE FLOOR'S OWN KINDS, as Necrotic Bloom draws them, asked once a beat and only when one is due.
		if (!Population.IsSet())
		{
			Population = FCataclysmFloorPopulator::Populate(CurrentFloor->GetPlan(), ChooseEnemyScale(), FloorBrief);
		}
		const TArray<FIntPoint> Cells = NecroticBloomWaveCells(*CurrentFloor, Where);
		if (Population->Enemies.IsEmpty() || Cells.IsEmpty())
		{
			// NOTHING TO PLACE, and the clock is left alone so the next beat asks again.
			continue;
		}
		FCataclysmEnemyPlacement Placement = Population->Enemies[FMath::RandRange(0, Population->Enemies.Num() - 1)];
		Placement.Cell = Cells[FMath::RandRange(0, Cells.Num() - 1)];
		ACataclysmEnemyCharacter* Sent = SpawnPlacedCreature(Placement, FloorBrief.SightRadiusMultiplier, /*FixedRung=*/0);
		if (!Sent)
		{
			continue;
		}
		// IT PAYS NOTHING, IS RAISED BY THE RULE AND IS NOT ONE OF THE FLOOR'S CREATURES: a portal never stops, so
		// a paid creature would be unlimited loot, and the floor's "cleared" counts the floor's creatures only.
		Sent->bIsAnAbomination = true;
		Sent->bDiesUnpaid = true;
		Sent->bRaisedByARule = true;
		CreaturesRaisedByARule.Add(Sent);
		One.Sent.Add(Sent);
		One.SecondsSinceLastSent = 0.0f;
		UE_LOG(LogCataclysm, Verbose, TEXT("Portal Unleashing: a portal sent a %s on floor %d; %d of its own stand"),
			   CataclysmDungeonCreatureName(Placement.Creature), FloorNumber, One.Sent.Num());
	}

	int32 Standing = 0;
	for (const FVoidPortal& One : VoidPortals)
	{
		Standing += One.Sent.Num();
	}
	if (Standing != VoidPortalsPanelStanding)
	{
		VoidPortalsPanelStanding = Standing;
		RefreshFloorModifierPanel();
	}
}

TArray<ACataclysmGroundZone*> ACataclysmDungeonGameMode::RawSewageMarksNow() const
{
	TArray<ACataclysmGroundZone*> Now;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Mark : RawSewageMarks)
	{
		if (ACataclysmGroundZone* Zone = Mark.Get(); IsValid(Zone))
		{
			Now.Add(Zone);
		}
	}
	return Now;
}

void ACataclysmDungeonGameMode::ForgetTheRivers()
{
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Mark : RawSewageMarks)
	{
		if (ACataclysmGroundZone* Zone = Mark.Get())
		{
			Zone->Destroy();
		}
	}
	RawSewageMarks.Reset();
	RawSewageMarkPoints.Reset();
	bRawSewageInARiver = false;
	RawSewageSecondsInARiver = 0.0f;
}

void ACataclysmDungeonGameMode::PlaceTheRivers()
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!CurrentFloor || !CurrentFloor->IsBuilt() || !FloorBrief.Modifiers.Contains(FName(Effects::RawSewageKey)))
	{
		return;
	}

	// EACH RIVER A STRAIGHT LINE OF WINGS OF THE HOST'S MARKS ACROSS THE FLOOR, through a cell Eternal Chorus's
	// picker gives and at a random heading, keeping the marks that stand clear of the entrance.
	const int32 Count = FloorBrief.bWaveWalksIn ? Effects::RawSewageRiversPerHordeArena : Effects::RawSewageRiversPerFloor;
	const FVector Entrance = CurrentFloor->EntranceWorld();
	for (const FIntPoint& Cell : EternalChorusCells(*CurrentFloor, Count))
	{
		const float Heading = FMath::FRandRange(0.0f, PI);
		for (const FVector& Where : WingsOfTheHostFeathers(
				 *CurrentFloor, CurrentFloor->WorldOfCell(Cell), FVector(FMath::Cos(Heading), FMath::Sin(Heading), 0.0f)))
		{
			if (FVector::Dist2D(Where, Entrance) > Effects::RawSewageDryAroundTheEntranceCm)
			{
				RawSewageMarkPoints.Add(Where);
			}
		}
	}
	RawSewageMarks.SetNum(RawSewageMarkPoints.Num());
	UE_LOG(LogCataclysm, Log, TEXT("Raw Sewage: %d river mark(s) on floor %d"), RawSewageMarkPoints.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepRawSewage(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	const FName Type = DungeonGameModeTypeOfRow(Effects::RawSewageKey);

	if (FloorBrief.Modifiers.Contains(FName(Effects::RawSewageKey)) && CurrentFloor && CurrentFloor->IsBuilt())
	{
		// THE MARKS DRAWN AGAIN WHENEVER THEY ARE MISSING, which is after every floor or wave. They do no damage.
		bool bInARiver = false;
		const FVector Feet = Player->GetActorLocation();
		for (int32 Index = 0; Index < RawSewageMarkPoints.Num(); ++Index)
		{
			ACataclysmGroundZone* Mark = RawSewageMarks.IsValidIndex(Index) ? RawSewageMarks[Index].Get() : nullptr;
			if (!Mark && Source)
			{
				const FVector Where = RawSewageMarkPoints[Index];
				Mark = ACataclysmGroundZone::SpawnForTheFloor(
					Source, Where, Where, Effects::WingsOfTheHostFeatherRadiusCm, 0.0f,
					/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type);
				if (RawSewageMarks.IsValidIndex(Index))
				{
					RawSewageMarks[Index] = Mark;
				}
			}
			bInARiver |= Mark && Mark->Covers(Feet);
		}

		// ENTERING ADDS A STACK, AND EACH FURTHER `RawSewageSecondsPerStack` IN A RIVER ANOTHER, never past the most.
		if (bInARiver && !bRawSewageInARiver)
		{
			RawSewageStacks = Effects::RawSewageStacksAfterAdding(RawSewageStacks);
			RawSewageSecondsInARiver = 0.0f;
		}
		else if (bInARiver)
		{
			RawSewageSecondsInARiver += SecondsBetweenWaveChecks;
			if (RawSewageSecondsInARiver >= Effects::RawSewageSecondsPerStack)
			{
				RawSewageSecondsInARiver -= Effects::RawSewageSecondsPerStack;
				RawSewageStacks = Effects::RawSewageStacksAfterAdding(RawSewageStacks);
			}
		}
		else
		{
			RawSewageSecondsInARiver = 0.0f;
		}
		bRawSewageInARiver = bInARiver;
	}
	else
	{
		bRawSewageInARiver = false;
		RawSewageSecondsInARiver = 0.0f;
	}

	// THE BURN, ONCE A SECOND ON ANY FLOOR WHILE A STACK IS HELD: Plague Convergence's pattern, a share of maximum
	// health dealt as damage over time typed as the row, which pestilence resistance meets. Not the Disease ailment.
	if (RawSewageStacks > 0)
	{
		RawSewageSecondsSinceBurn += SecondsBetweenWaveChecks;
		if (RawSewageSecondsSinceBurn >= 1.0f)
		{
			RawSewageSecondsSinceBurn = 0.0f;
			const float Burn = AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute())
				* Effects::RawSewagePercentPerSecond(RawSewageStacks) / 100.0f;
			if (Source && Burn > 0.0f && !UCataclysmSkillEffects::IsDead(Player))
			{
				FCataclysmHitDelivery Delivery;
				Delivery.bIsDamageOverTime = true;
				Delivery.DamageType = Type;
				UCataclysmSkillEffects::ApplyDirectDamage(Source, Player, Burn, Delivery);
			}
		}
	}
	else
	{
		RawSewageSecondsSinceBurn = 0.0f;
	}

	// THE DISEASE KEYWORD WHILE ANY STACK IS HELD, as ruled: the row says "disease stacks", so the player carries a
	// debuff every reader of the player's debuffs sees -- the Masochist's count, Wound Channeling and Contagion.
	const bool bWantTag = RawSewageStacks > 0;
	if (bWantTag != bRawSewageTagged)
	{
		const FGameplayTag Disease =
			UGameplayTagsManager::Get().RequestGameplayTag(FName(TEXT("Keyword.DoT.Disease")), /*ErrorIfNotFound=*/false);
		if (Disease.IsValid())
		{
			if (bWantTag)
			{
				AbilitySystem->AddLooseGameplayTag(Disease);
			}
			else
			{
				AbilitySystem->RemoveLooseGameplayTag(Disease);
			}
		}
		bRawSewageTagged = bWantTag;
	}

	if (RawSewageStacks != RawSewagePanelStacks)
	{
		RawSewagePanelStacks = RawSewageStacks;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::NoteDeathForRawSewage(const FCataclysmDeathNotice& Notice)
{
	if (RawSewageStacks <= 0)
	{
		return;
	}
	// A FLOOR'S BOSS OR THE PLAYER, as Wasting Sickness's stacks end. The next beat takes the tag off.
	const bool bThePlayer = Cast<ACataclysmPlayerCharacter>(Notice.Victim) != nullptr;
	if (!bThePlayer && !DiedAsAFloorsBoss(Notice.Victim))
	{
		return;
	}
	UE_LOG(LogCataclysm, Log, TEXT("Raw Sewage: %d stack(s) cleansed by %s's death"), RawSewageStacks,
		   bThePlayer ? TEXT("the player") : TEXT("a floor's boss"));
	RawSewageStacks = 0;
	RawSewageSecondsInARiver = 0.0f;
	RefreshFloorModifierPanel();
}

TArray<ACataclysmGroundZone*> ACataclysmDungeonGameMode::LocustSheltersNow() const
{
	TArray<ACataclysmGroundZone*> Now;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Shelter : LocustShelters)
	{
		if (ACataclysmGroundZone* Zone = Shelter.Get(); IsValid(Zone))
		{
			Now.Add(Zone);
		}
	}
	return Now;
}

void ACataclysmDungeonGameMode::ForgetTheLocusts()
{
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Shelter : LocustShelters)
	{
		if (ACataclysmGroundZone* Zone = Shelter.Get())
		{
			Zone->Destroy();
		}
	}
	LocustShelters.Reset();
	LocustShelterCells.Reset();
	if (ACataclysmGroundZone* Swarm = SwarmOfLocusts.Get())
	{
		Swarm->Destroy();
	}
	SwarmOfLocusts = nullptr;
	bSwarmOfLocustsTravelling = false;
	SwarmOfLocustsSecondsIntoIt = 0.0f;
	SwarmOfLocustsSecondsSinceLast = 0.0f;
	SwarmOfLocustsSecondsSinceBurn = 0.0f;
	SwarmOfLocustsPanelSecond = -1;
}

void ACataclysmDungeonGameMode::PlaceTheShelters()
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!CurrentFloor || !CurrentFloor->IsBuilt() || !FloorBrief.Modifiers.Contains(FName(Effects::SwarmOfLocustsKey)))
	{
		return;
	}
	// TWO ON A FLOOR AND ONE ON A HORDE ARENA, KEPT, where Eternal Chorus's picker puts its sources.
	LocustShelterCells = EternalChorusCells(
		*CurrentFloor, FloorBrief.bWaveWalksIn ? Effects::SwarmOfLocustsSheltersPerHordeArena
											   : Effects::SwarmOfLocustsSheltersPerFloor);
	LocustShelters.SetNum(LocustShelterCells.Num());
	UE_LOG(LogCataclysm, Log, TEXT("Swarm of Locusts: %d shelter(s) on floor %d"), LocustShelterCells.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepSwarmOfLocusts(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	const FVector Feet = Player->GetActorLocation();

	// THE SHELTERS DRAWN AGAIN WHENEVER THEY ARE MISSING, which is after every floor or wave. In Celestial's colours,
	// as Void Parasite's light is, so they do not read as more of the swarm.
	bool bSheltered = false;
	for (int32 Index = 0; Index < LocustShelterCells.Num(); ++Index)
	{
		ACataclysmGroundZone* Shelter = LocustShelters.IsValidIndex(Index) ? LocustShelters[Index].Get() : nullptr;
		if (!Shelter && Source)
		{
			const FVector Where = CurrentFloor->WorldOfCell(LocustShelterCells[Index]);
			Shelter = ACataclysmGroundZone::SpawnForTheFloor(
				Source, Where, Where, Effects::SwarmOfLocustsShelterRadiusCm, 0.0f,
				/*bAffectsEveryone=*/false, /*InDrawnAsType=*/FName(TEXT("Celestial")));
			if (LocustShelters.IsValidIndex(Index))
			{
				LocustShelters[Index] = Shelter;
			}
		}
		bSheltered |= Shelter && Shelter->Covers(Feet);
	}

	if (ACataclysmGroundZone* Swarm = SwarmOfLocusts.Get())
	{
		SwarmOfLocustsSecondsIntoIt += SecondsBetweenWaveChecks;

		// ITS WARNING OVER, IT TRAVELS. The zone moves itself from its own tick in play.
		if (!bSwarmOfLocustsTravelling && SwarmOfLocustsSecondsIntoIt >= Effects::SwarmOfLocustsWarningSeconds)
		{
			Swarm->TravelAt(SwarmOfLocustsVelocity);
			bSwarmOfLocustsTravelling = true;
			SwarmOfLocustsSecondsSinceBurn = 0.0f;
			RefreshFloorModifierPanel();
		}

		// CROSSED: GONE, AND THE NEXT ONE'S CLOCK STARTS.
		if (SwarmOfLocustsSecondsIntoIt >= Effects::SwarmOfLocustsLastsSeconds())
		{
			Swarm->Destroy();
			SwarmOfLocusts = nullptr;
			bSwarmOfLocustsTravelling = false;
			SwarmOfLocustsSecondsSinceLast = 0.0f;
			RefreshFloorModifierPanel();
			return;
		}

		// THE BURN, ONCE A SECOND WHILE IT TRAVELS, FOR A PLAYER IT COVERS WHO IS IN NO SHELTER. The rule's own step
		// deals it, as Infested Veins' burn is dealt, so a shelter can stop it.
		if (bSwarmOfLocustsTravelling)
		{
			SwarmOfLocustsSecondsSinceBurn += SecondsBetweenWaveChecks;
			if (SwarmOfLocustsSecondsSinceBurn >= 1.0f)
			{
				SwarmOfLocustsSecondsSinceBurn = 0.0f;
				const float Burn =
					Effects::SwarmOfLocustsBurn(AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()));
				if (Source && Burn > 0.0f && Swarm->Covers(Feet) && !bSheltered && !UCataclysmSkillEffects::IsDead(Player))
				{
					FCataclysmHitDelivery Delivery;
					Delivery.bIsArea = true;
					Delivery.bIsDamageOverTime = true;
					Delivery.DamageType = DungeonGameModeTypeOfRow(Effects::SwarmOfLocustsKey);
					UCataclysmSkillEffects::ApplyDirectDamage(Source, Player, Burn, Delivery);
				}
			}
		}
		return;
	}

	SwarmOfLocustsSecondsSinceLast += SecondsBetweenWaveChecks;
	const int32 Second = FMath::CeilToInt(Effects::SwarmOfLocustsSecondsBetween - SwarmOfLocustsSecondsSinceLast);
	if (Second != SwarmOfLocustsPanelSecond)
	{
		SwarmOfLocustsPanelSecond = Second;
		RefreshFloorModifierPanel();
	}
	if (!Effects::SwarmOfLocustsIsDue(SwarmOfLocustsSecondsSinceLast) || !Source)
	{
		return;
	}

	// A SWARM: AWAY FROM THE PLAYER AT A RANDOM ANGLE, AIMED THROUGH WHERE THE PLAYER STANDS NOW. It does no damage of
	// its own; it lasts its warning and its travel.
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const FVector Where(Feet.X + Effects::SwarmOfLocustsAppearsAwayCm * FMath::Cos(Angle),
						Feet.Y + Effects::SwarmOfLocustsAppearsAwayCm * FMath::Sin(Angle), Feet.Z);
	ACataclysmGroundZone* Swarm = ACataclysmGroundZone::Spawn(
		Source, Where, Effects::SwarmOfLocustsRadiusCm, Effects::SwarmOfLocustsLastsSeconds() + 1.0f, 0.0f,
		DungeonGameModeTypeOfRow(Effects::SwarmOfLocustsKey));
	if (!Swarm)
	{
		return;
	}
	FVector Toward = Feet - Where;
	Toward.Z = 0.0f;
	SwarmOfLocustsVelocity = Toward.GetSafeNormal() * Effects::SwarmOfLocustsSpeedCmPerSecond;
	SwarmOfLocusts = Swarm;
	bSwarmOfLocustsTravelling = false;
	SwarmOfLocustsSecondsIntoIt = 0.0f;
	UE_LOG(LogCataclysm, Log, TEXT("Swarm of Locusts: a swarm appeared %.0f cm from the player on floor %d"),
		   Effects::SwarmOfLocustsAppearsAwayCm, FloorNumber);
	RefreshFloorModifierPanel();
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::AbyssalRiftCreaturesStanding() const
{
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& Sent : AbyssalRiftCreatures)
	{
		ACataclysmEnemyCharacter* Creature = Sent.Get();
		if (IsValid(Creature) && !UCataclysmSkillEffects::IsDead(Creature))
		{
			Standing.Add(Creature);
		}
	}
	return Standing;
}

void ACataclysmDungeonGameMode::ForgetTheRift()
{
	if (ACataclysmEnemyCharacter* Rift = AbyssalRift.Get())
	{
		Rift->Destroy();
	}
	if (ACataclysmGroundZone* Zone = AbyssalRiftZone.Get())
	{
		Zone->Destroy();
	}
	AbyssalRift = nullptr;
	AbyssalRiftZone = nullptr;
	AbyssalRiftState = ERiftState::Waiting;
	AbyssalRiftSecondsOpen = 0.0f;
	AbyssalRiftWavesSent = 0;
	AbyssalRiftCreatures.Reset();
	AbyssalRiftPanelSecond = -1;
}

void ACataclysmDungeonGameMode::PlaceTheRift()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::AbyssalRiftsKey)))
	{
		return;
	}
	// NONE ON A HORDE ARENA, as ruled: its waves already come to the player.
	if (FloorBrief.bWaveWalksIn)
	{
		RefreshFloorModifierPanel();
		return;
	}
	const TArray<FIntPoint> Cells = EternalChorusCells(*CurrentFloor, 1);
	if (Cells.IsEmpty())
	{
		return;
	}
	const TSubclassOf<ACataclysmEnemyCharacter> Class = ACataclysmRiftCharacter::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Where = CurrentFloor->WorldOfCell(Cells[0]) + FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));
	ACataclysmEnemyCharacter* Rift = World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Spawn);
	if (!Rift)
	{
		return;
	}
	// IT CANNOT BE HURT: it is closed by killing what it sends, not by striking it. The Imp's health at Common,
	// paying nothing and not one of the floor's creatures.
	Rift->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Rift->SetHealth(AbyssalRiftHealth());
	Rift->SetRarityStep(0);
	Rift->bCannotBeHurt = true;
	Rift->bDiesUnpaid = true;
	Rift->bRaisedByARule = true;
	CreaturesRaisedByARule.Add(Rift);
	AbyssalRift = Rift;
	AbyssalRiftState = ERiftState::Waiting;
	UE_LOG(LogCataclysm, Log, TEXT("Abyssal Rifts: a rift placed on floor %d"), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::CloseTheRift(bool bInTime)
{
	if (bInTime)
	{
		++AbyssalRiftSuccesses;
	}
	if (ACataclysmEnemyCharacter* Rift = AbyssalRift.Get())
	{
		Rift->Destroy();
	}
	if (ACataclysmGroundZone* Zone = AbyssalRiftZone.Get())
	{
		Zone->Destroy();
	}
	AbyssalRift = nullptr;
	AbyssalRiftZone = nullptr;
	AbyssalRiftState = ERiftState::Closed;
	UE_LOG(LogCataclysm, Log, TEXT("Abyssal Rifts: the rift on floor %d closed %s; %d closed in time this dungeon"),
		   FloorNumber, bInTime ? TEXT("in time") : TEXT("too late"), AbyssalRiftSuccesses);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepAbyssalRifts(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	// THE REWARD WRITTEN ON THE PLAYER WHEN THE SUCCESSES CHANGED, on any floor, as Chaos Touched's stacks are.
	if (AbyssalRiftSuccesses != AbyssalRiftSuccessesApplied)
	{
		AbyssalRiftSuccessesApplied = AbyssalRiftSuccesses;
		ApplyChangingFloorEffects(Player, AbilitySystem);
		RefreshFloorModifierPanel();
	}

	ACataclysmEnemyCharacter* Rift = AbyssalRift.Get();
	if (!Rift || !CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::AbyssalRiftsKey)))
	{
		return;
	}
	const FVector At = Rift->GetActorLocation();

	// ITS ZONE DRAWN AGAIN WHENEVER IT IS MISSING. It does no damage.
	if (!AbyssalRiftZone.Get())
	{
		if (ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World))
		{
			AbyssalRiftZone = ACataclysmGroundZone::SpawnForTheFloor(
				Source, At, At, Effects::AbyssalRiftsZoneRadiusCm, 0.0f,
				/*bAffectsEveryone=*/false, /*InDrawnAsType=*/DungeonGameModeTypeOfRow(Effects::AbyssalRiftsKey));
		}
	}

	// IT OPENS WHEN THE PLAYER FIRST COMES NEAR.
	if (AbyssalRiftState == ERiftState::Waiting)
	{
		if (FVector::Dist2D(At, Player->GetActorLocation()) > Effects::AbyssalRiftsOpensWithinCm)
		{
			return;
		}
		AbyssalRiftState = ERiftState::Open;
		AbyssalRiftSecondsOpen = 0.0f;
		AbyssalRiftWavesSent = 0;
		UE_LOG(LogCataclysm, Log, TEXT("Abyssal Rifts: the rift on floor %d opened"), FloorNumber);
	}
	if (AbyssalRiftState != ERiftState::Open)
	{
		return;
	}

	// A WAVE WHEN ITS TIME HAS COME: the floor's own kinds, as Necrotic Bloom draws them, at the rung the successes so
	// far give, on cells beside the rift. They pay and are the floor's creatures.
	if (Effects::AbyssalRiftsWaveIsDue(AbyssalRiftSecondsOpen, AbyssalRiftWavesSent))
	{
		const FCataclysmFloorPopulation Population =
			FCataclysmFloorPopulator::Populate(CurrentFloor->GetPlan(), ChooseEnemyScale(), FloorBrief);
		const TArray<FIntPoint> Cells = NecroticBloomWaveCells(*CurrentFloor, At);
		int32 Placed = 0;
		for (int32 Which = 0; Which < Effects::AbyssalRiftsCreaturesPerWave && !Population.Enemies.IsEmpty() && !Cells.IsEmpty();
			 ++Which)
		{
			FCataclysmEnemyPlacement Placement = Population.Enemies[FMath::RandRange(0, Population.Enemies.Num() - 1)];
			Placement.Cell = Cells[FMath::RandRange(0, Cells.Num() - 1)];
			if (ACataclysmEnemyCharacter* Sent = SpawnPlacedCreature(
					Placement, FloorBrief.SightRadiusMultiplier, Effects::AbyssalRiftsRungFor(AbyssalRiftSuccesses)))
			{
				FloorEnemies.Add(Sent);
				AbyssalRiftCreatures.Add(Sent);
				++Placed;
			}
		}
		++AbyssalRiftWavesSent;
		UE_LOG(LogCataclysm, Log, TEXT("Abyssal Rifts: wave %d of %d sent %d creature(s) on floor %d"),
			   AbyssalRiftWavesSent, Effects::AbyssalRiftsWaves, Placed, FloorNumber);
		RefreshFloorModifierPanel();
	}

	// CLOSED IN TIME WHEN EVERY WAVE HAS COME AND EVERY CREATURE IT SENT IS DEAD; TOO LATE WHEN THE TIME RUNS OUT.
	const int32 Standing = AbyssalRiftCreaturesStanding().Num();
	if (AbyssalRiftWavesSent >= Effects::AbyssalRiftsWaves && Standing == 0)
	{
		CloseTheRift(/*bInTime=*/true);
		return;
	}
	if (Effects::AbyssalRiftsHasRunOut(AbyssalRiftSecondsOpen))
	{
		CloseTheRift(/*bInTime=*/false);
		return;
	}
	AbyssalRiftSecondsOpen += SecondsBetweenWaveChecks;

	const int32 Second = FMath::CeilToInt(Effects::AbyssalRiftsSecondsToClose - AbyssalRiftSecondsOpen) * 100 + Standing;
	if (Second != AbyssalRiftPanelSecond)
	{
		AbyssalRiftPanelSecond = Second;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::NoteDeathForAbyssalRifts(const FCataclysmDeathNotice& Notice)
{
	// THE PLAYER'S DEATH ENDS THE SUCCESSES AND THEIR MAGIC FIND, as ruled; the next beat takes the reward off.
	if (AbyssalRiftSuccesses > 0 && Cast<ACataclysmPlayerCharacter>(Notice.Victim))
	{
		UE_LOG(LogCataclysm, Log, TEXT("Abyssal Rifts: the player's death ended %d success(es)"), AbyssalRiftSuccesses);
		AbyssalRiftSuccesses = 0;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::NoteDeathForInfestedHoard(const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE PLAYER'S DEATH ENDS THE STACKS, as ruled.
	if (Cast<ACataclysmPlayerCharacter>(Notice.Victim))
	{
		if (InfestedHoardStacks > 0)
		{
			UE_LOG(LogCataclysm, Log, TEXT("The Infested Hoard: the player's death ended %d stack(s)"),
				   InfestedHoardStacks);
			InfestedHoardStacks = 0;
			InfestedHoardSecondsSinceDrain = 0.0f;
			RefreshFloorModifierPanel();
		}
		return;
	}

	// ONE OF THE FLOOR'S CREATURES, WHICH PAID FOR ITS DEATH, ON A FLOOR CARRYING THE ROW.
	ACataclysmEnemyCharacter* Victim = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	UWorld* World = GetWorld();
	if (!World || !Victim || !FloorBrief.Modifiers.Contains(FName(Effects::InfestedHoardKey))
		|| !FloorEnemies.Contains(Victim) || !Victim->PaysForItsDeath()
		|| !Effects::InfestedHoardDrops(DungeonGameModeInfestedHoardRoll(), InfestedHoardStacks))
	{
		return;
	}
	float MagicFind = 0.0f;
	float LootQuantity = UCataclysmDropRoll::BaselineLootQuantity;
	UCataclysmDropSpawner::PlayerLootStats(World, MagicFind, LootQuantity);
	FRandomStream Stream(Victim->GetUniqueID() ^ 0x1F35D0A7
		^ static_cast<int32>(World->GetTimeSeconds() * 1000.0f));
	const int32 Spawned = UCataclysmDropSpawner::SpawnOneInfestedDropFor(
		World, Victim->RarityStep, MagicFind, Victim->GetActorLocation(), Stream);
	UE_LOG(LogCataclysm, Log, TEXT("The Infested Hoard: %d infested drop(s) on floor %d at %d stack(s)"), Spawned,
		   FloorNumber, InfestedHoardStacks);
}

void ACataclysmDungeonGameMode::NoteLootTakenForInfestedHoard(const FCataclysmLootTakenNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// AN INFESTED DROP, TAKEN BY THE PLAYER BY HAND. Infested drops are never collected automatically, so this is
	// every infested take the player makes.
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
	if (!Notice.bInfested || !Notice.bByHand || !Player || Notice.Taker != Player)
	{
		return;
	}
	InfestedHoardStacks = Effects::InfestedHoardStacksAfterAdding(InfestedHoardStacks);
	UE_LOG(LogCataclysm, Log, TEXT("The Infested Hoard: an infested drop taken; %d stack(s)"), InfestedHoardStacks);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepInfestedHoard(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	// THE DRAIN, ONCE A SECOND WHILE A STACK IS HELD: Raw Sewage's burn, a share of maximum health dealt as damage
	// over time typed as the row, which pestilence resistance meets.
	if (InfestedHoardStacks > 0)
	{
		InfestedHoardSecondsSinceDrain += SecondsBetweenWaveChecks;
		if (InfestedHoardSecondsSinceDrain >= 1.0f)
		{
			InfestedHoardSecondsSinceDrain = 0.0f;
			const float Drain = AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute())
				* Effects::InfestedHoardPercentPerSecond(InfestedHoardStacks) / 100.0f;
			ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
			if (Source && Drain > 0.0f && !UCataclysmSkillEffects::IsDead(Player))
			{
				FCataclysmHitDelivery Delivery;
				Delivery.bIsDamageOverTime = true;
				Delivery.DamageType = DungeonGameModeTypeOfRow(Effects::InfestedHoardKey);
				UCataclysmSkillEffects::ApplyDirectDamage(Source, Player, Drain, Delivery);
			}
		}
	}
	else
	{
		InfestedHoardSecondsSinceDrain = 0.0f;
	}

	if (InfestedHoardStacks != InfestedHoardPanelStacks)
	{
		InfestedHoardPanelStacks = InfestedHoardStacks;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::StepPestilentEmpowerment(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player))
	{
		return;
	}

	// EVERY CREATURE ON THE PLAYER'S OTHER SIDE, the sweep March of Progress makes, at what the earlier
	// floors' standing beacons add; the floor sources themselves are left alone, since they do nothing.
	const float Multiplier = Effects::PestilentEmpowermentDamageMultiplier(PestilentBeaconsLeftStanding);
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		ACataclysmEnemyCharacter* Creature = *It;
		if (!IsValid(Creature) || Creature->IsA<ACataclysmFloorSourceCharacter>()
			|| !UCataclysmTargeting::IsHostileTo(Creature, Player))
		{
			continue;
		}
		Creature->SetPlagueBeaconsDamageMultiplier(Multiplier);
	}

	const int32 Standing = PlagueBeaconsStanding().Num();
	if (Standing != PestilentPanelStanding)
	{
		PestilentPanelStanding = Standing;
		RefreshFloorModifierPanel();
	}
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::InfestedVeinsStanding() const
{
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (const FInfestedVein& One : InfestedVeins)
	{
		ACataclysmEnemyCharacter* Vein = One.Vein.Get();
		if (One.SecondsSinceDestroyed < 0.0f && IsValid(Vein) && !UCataclysmSkillEffects::IsDead(Vein))
		{
			Standing.Add(Vein);
		}
	}
	return Standing;
}

ACataclysmGroundZone* ACataclysmDungeonGameMode::InfestedVeinZoneOf(const ACataclysmEnemyCharacter* Vein) const
{
	for (const FInfestedVein& One : InfestedVeins)
	{
		if (Vein && One.Vein.Get() == Vein)
		{
			return One.Zone.Get();
		}
	}
	return nullptr;
}

void ACataclysmDungeonGameMode::ForgetTheVeins()
{
	for (const FInfestedVein& One : InfestedVeins)
	{
		if (ACataclysmEnemyCharacter* Vein = One.Vein.Get())
		{
			Vein->Destroy();
		}
		if (ACataclysmGroundZone* Zone = One.Zone.Get())
		{
			Zone->Destroy();
		}
	}
	InfestedVeins.Reset();
	InfestedVeinsDestroyed = 0;
	bInfestedVeinsGuardiansCame = false;
	InfestedVeinsSecondsSinceBurn = 0.0f;
	InfestedVeinsPanelStanding = -1;
	InfestedVeinsPanelDestroyed = -1;
}

ACataclysmEnemyCharacter* ACataclysmDungeonGameMode::SpawnAVeinOn(FIntPoint Cell)
{
	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return nullptr;
	}
	const TSubclassOf<ACataclysmEnemyCharacter> Class = ACataclysmVeinCharacter::StaticClass();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Where = CurrentFloor->WorldOfCell(Cell)
		+ FVector(0.0f, 0.0f, DungeonGameModeStandingHeightOfClass(Class));
	ACataclysmEnemyCharacter* Vein =
		World->SpawnActor<ACataclysmEnemyCharacter>(Class, Where, FRotator::ZeroRotator, Spawn);
	if (!Vein)
	{
		return nullptr;
	}
	// THE IMP'S HEALTH AT COMMON, a play-test value. It pays nothing and is not one of the floor's
	// creatures, as the other floor sources are not.
	Vein->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
	Vein->SetHealth(InfestedVeinHealth());
	Vein->SetRarityStep(0);
	Vein->bDiesUnpaid = true;
	Vein->bRaisedByARule = true;
	CreaturesRaisedByARule.Add(Vein);
	return Vein;
}

void ACataclysmDungeonGameMode::PlaceTheVeins()
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::InfestedVeinsKey)))
	{
		return;
	}

	// THREE ON A FLOOR, ONE ON A HORDE ARENA, ON FLOOR CELLS BESIDE A WALL, as ruled; fewer when the
	// floor has fewer such cells far enough apart.
	const int32 Count = FloorBrief.bWaveWalksIn ? Effects::InfestedVeinsPerHordeArena
											: Effects::InfestedVeinsPerFloor;
	for (const FIntPoint& Cell : InfestedVeinsCells(*CurrentFloor, Count))
	{
		FInfestedVein One;
		One.Cell = Cell;
		One.Vein = SpawnAVeinOn(Cell);
		if (One.Vein.IsValid())
		{
			InfestedVeins.Add(One);
		}
	}
	UE_LOG(LogCataclysm, Log, TEXT("Infested Veins: %d vein(s) placed on floor %d"), InfestedVeins.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepInfestedVeins(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	const FName Type = DungeonGameModeTypeOfRow(Effects::InfestedVeinsKey);
	const FVector Feet = Player->GetActorLocation();

	bool bInTheToxicGround = false;
	for (FInfestedVein& One : InfestedVeins)
	{
		ACataclysmEnemyCharacter* Vein = One.Vein.Get();

		// A LIVING VEIN: ITS ZONE DRAWN AGAIN whenever it is missing, which is after every floor or wave.
		if (One.SecondsSinceDestroyed < 0.0f && IsValid(Vein) && !UCataclysmSkillEffects::IsDead(Vein))
		{
			ACataclysmGroundZone* Zone = One.Zone.Get();
			if (!Zone && Source)
			{
				const FVector Where = Vein->GetActorLocation();
				Zone = ACataclysmGroundZone::SpawnForTheFloor(
					Source, Where, Where, Effects::InfestedVeinsRadiusCm, 0.0f,
					/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type);
				One.Zone = Zone;
			}
			bInTheToxicGround |= Zone && Zone->Covers(Feet);
			continue;
		}

		// A VEIN JUST DESTROYED: ITS ZONE GOES, IT IS COUNTED, and at the threshold the guardians come.
		if (One.SecondsSinceDestroyed < 0.0f)
		{
			if (ACataclysmGroundZone* Zone = One.Zone.Get())
			{
				Zone->Destroy();
			}
			One.Zone = nullptr;
			One.SecondsSinceDestroyed = 0.0f;
			++InfestedVeinsDestroyed;
			if (Effects::InfestedVeinsGuardiansAreDue(InfestedVeinsDestroyed, bInfestedVeinsGuardiansCame))
			{
				bInfestedVeinsGuardiansCame = true;
				// THE FLOOR'S OWN KINDS, as Grave Tide draws them, at the Elite rung, on floor cells
				// beside the vein as Necrotic Bloom's waves stand beside their flower.
				const FCataclysmFloorPopulation Population = FCataclysmFloorPopulator::Populate(
					CurrentFloor->GetPlan(), ChooseEnemyScale(), FloorBrief);
				const TArray<FIntPoint> Cells =
					NecroticBloomWaveCells(*CurrentFloor, CurrentFloor->WorldOfCell(One.Cell));
				int32 Placed = 0;
				for (int32 Which = 0; Which < Effects::InfestedVeinsGuardians
									  && !Population.Enemies.IsEmpty() && !Cells.IsEmpty(); ++Which)
				{
					FCataclysmEnemyPlacement Placement =
						Population.Enemies[FMath::RandRange(0, Population.Enemies.Num() - 1)];
					Placement.Cell = Cells[FMath::RandRange(0, Cells.Num() - 1)];
					if (ACataclysmEnemyCharacter* Guardian = SpawnPlacedCreature(
							Placement, FloorBrief.SightRadiusMultiplier, Effects::InfestedVeinsGuardianRung))
					{
						// THE FLOOR'S LIST: they pay and are saved like any creature.
						FloorEnemies.Add(Guardian);
						++Placed;
					}
				}
				UE_LOG(LogCataclysm, Log, TEXT("Infested Veins: %d guardian(s) came on floor %d"), Placed, FloorNumber);
			}
			continue;
		}

		// A DESTROYED VEIN GROWS BACK ON ITS CELL, at full health; its zone is drawn on the next beat.
		One.SecondsSinceDestroyed += SecondsBetweenWaveChecks;
		if (Effects::InfestedVeinRegrowIsDue(One.SecondsSinceDestroyed))
		{
			if (ACataclysmEnemyCharacter* Regrown = SpawnAVeinOn(One.Cell))
			{
				One.Vein = Regrown;
				One.SecondsSinceDestroyed = -1.0f;
			}
		}
	}

	// THE BURN, ONCE A SECOND, FOR A PLAYER IN A LIVING VEIN'S ZONE AT THAT BEAT, once however many
	// zones cover them, as Necrotic Ground burns. Typed by the row.
	InfestedVeinsSecondsSinceBurn += SecondsBetweenWaveChecks;
	if (InfestedVeinsSecondsSinceBurn >= 1.0f)
	{
		InfestedVeinsSecondsSinceBurn = 0.0f;
		const float Burn = Effects::InfestedVeinsBurn(AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()));
		if (bInTheToxicGround && Source && Burn > 0.0f)
		{
			FCataclysmHitDelivery Delivery;
			Delivery.bIsArea = true;
			Delivery.bIsDamageOverTime = true;
			Delivery.DamageType = Type;
			UCataclysmSkillEffects::ApplyDirectDamage(Source, Player, Burn, Delivery);
		}
	}

	const int32 Standing = InfestedVeinsStanding().Num();
	if (Standing != InfestedVeinsPanelStanding || InfestedVeinsDestroyed != InfestedVeinsPanelDestroyed)
	{
		InfestedVeinsPanelStanding = Standing;
		InfestedVeinsPanelDestroyed = InfestedVeinsDestroyed;
		RefreshFloorModifierPanel();
	}
}

TArray<ACataclysmEnemyCharacter*> ACataclysmDungeonGameMode::VoidlingsNow() const
{
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& One : Voidlings)
	{
		ACataclysmEnemyCharacter* Voidling = One.Get();
		if (IsValid(Voidling) && !UCataclysmSkillEffects::IsDead(Voidling))
		{
			Standing.Add(Voidling);
		}
	}
	return Standing;
}

ACataclysmGroundZone* ACataclysmDungeonGameMode::VoidParasiteLightNow() const
{
	return VoidParasiteLight.Get();
}

void ACataclysmDungeonGameMode::ForgetTheVoidParasite()
{
	for (const TWeakObjectPtr<ACataclysmEnemyCharacter>& One : Voidlings)
	{
		if (ACataclysmEnemyCharacter* Voidling = One.Get())
		{
			Voidling->Destroy();
		}
	}
	Voidlings.Reset();
	if (ACataclysmGroundZone* Light = VoidParasiteLight.Get())
	{
		Light->Destroy();
	}
	VoidParasiteLight = nullptr;
	VoidParasiteLightCell = FIntPoint(-1, -1);

	// THE STACKS AND NOT WHAT WAS APPLIED: the next beat sees the two differ and takes the figure off the
	// character, on a floor carrying the row or not.
	VoidParasiteStacks = 0;
	VoidParasitePanelStacks = -1;
}

void ACataclysmDungeonGameMode::PlaceTheLight()
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!CurrentFloor || !CurrentFloor->IsBuilt()
		|| !FloorBrief.Modifiers.Contains(FName(Effects::VoidParasiteKey)))
	{
		return;
	}

	// ONE A FLOOR, AND ONE ON A HORDE ARENA, KEPT, at least `EternalChorusApartCm` from the entrance by
	// Eternal Chorus's picker, so a player does not start the floor standing in it.
	const TArray<FIntPoint> Cells = EternalChorusCells(*CurrentFloor, Effects::VoidParasiteLightZonesPerFloor);
	if (!Cells.IsEmpty())
	{
		VoidParasiteLightCell = Cells[0];
	}
	UE_LOG(LogCataclysm, Log, TEXT("Void Parasite: %d light zone(s) on floor %d"), Cells.Num(), FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepVoidParasite(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	if (FloorBrief.Modifiers.Contains(FName(Effects::VoidParasiteKey)) && CurrentFloor && CurrentFloor->IsBuilt())
	{
		const FVector Feet = Player->GetActorLocation();

		// THE LIGHT ZONE DRAWN AGAIN whenever it is missing, which is after a Horde arena's every wave. It
		// does no damage, and it is drawn in Celestial's colours because the row calls it light and the
		// row's own Void colours would draw it as more of the void.
		ACataclysmGroundZone* Light = VoidParasiteLight.Get();
		if (!Light && VoidParasiteLightCell != FIntPoint(-1, -1))
		{
			if (ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World))
			{
				const FVector Where = CurrentFloor->WorldOfCell(VoidParasiteLightCell);
				Light = ACataclysmGroundZone::SpawnForTheFloor(
					Source, Where, Where, Effects::VoidParasiteLightRadiusCm, 0.0f,
					/*bAffectsEveryone=*/false, /*InDrawnAsType=*/FName(TEXT("Celestial")));
				VoidParasiteLight = Light;
			}
		}

		// EACH VOIDLING WITHIN REACH ATTACHES: it is gone, paying nothing because it did not die, and the
		// player carries one more stack, never past the most. A killed voidling is forgotten; in play it
		// is destroyed on the next tick, and a test world keeps it.
		for (auto Entry = Voidlings.CreateIterator(); Entry; ++Entry)
		{
			ACataclysmEnemyCharacter* Voidling = Entry->Get();
			if (!IsValid(Voidling) || UCataclysmSkillEffects::IsDead(Voidling))
			{
				Entry.RemoveCurrent();
				continue;
			}
			if (FVector::Dist2D(Voidling->GetActorLocation(), Feet) <= Effects::VoidParasiteAttachCm)
			{
				VoidParasiteStacks = Effects::VoidParasiteStacksAfterAttaching(VoidParasiteStacks);
				Entry.RemoveCurrent();
				Voidling->Destroy();
				UE_LOG(LogCataclysm, Log, TEXT("Void Parasite: a voidling attached on floor %d; %d attached"),
					   FloorNumber, VoidParasiteStacks);
			}
		}

		// STANDING IN THE LIGHT CLEARS EVERY STACK AT ONCE. The zone stays.
		if (VoidParasiteStacks > 0 && Light && Light->Covers(Feet))
		{
			UE_LOG(LogCataclysm, Log, TEXT("Void Parasite: the light cleared %d voidling(s) on floor %d"),
				   VoidParasiteStacks, FloorNumber);
			VoidParasiteStacks = 0;
		}
	}

	// THE PLAYER'S STATS WRITTEN AGAIN WHEN THE STACKS CHANGED, on any floor, as Chaos Touched's are.
	if (VoidParasiteStacks != VoidParasiteStacksApplied)
	{
		VoidParasiteStacksApplied = VoidParasiteStacks;
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}
	if (VoidParasiteStacks != VoidParasitePanelStacks)
	{
		VoidParasitePanelStacks = VoidParasiteStacks;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::StepDivineWrath(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !Player || !AbilitySystem || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	DivineWrathSecondsSinceLast += SecondsBetweenWaveChecks;

	if (ACataclysmGroundZone* Beam = DivineWrathBeam.Get())
	{
		// IT CHASES: aimed again at where the player stands now, every beat.
		Beam->TravelAt(Effects::DivineWrathVelocity(Beam->GetActorLocation(),
													 Player->GetActorLocation()));

		// AND IT DESTROYS THE CREATURES IT COVERS, by the game mode and not by the zone, which
		// burns only its owner's enemies. The ordinary death, with an emptied last blow so no
		// killer is named; it pays as any death does. Never a floor's boss -- a beam that ended
		// one for nothing would skip the floor -- and never a creature that cannot be hurt.
		TArray<ACataclysmEnemyCharacter*> Covered;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : FloorEnemies)
		{
			if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy) && !Enemy->bCannotBeHurt
				&& !DiedAsAFloorsBoss(Enemy) && Beam->Covers(Enemy->GetActorLocation()))
			{
				Covered.Add(Enemy);
			}
		}
		for (ACataclysmEnemyCharacter* Enemy : Covered)
		{
			if (UCataclysmAbilitySystemComponent* Its = Cast<UCataclysmAbilitySystemComponent>(
					UCataclysmTargeting::AbilitySystemOf(Enemy)))
			{
				Its->RecordLastBlow(FCataclysmLastBlow());
				Its->SetNumericAttributeBase(Vital::GetHealthAttribute(), 0.0f);
				++DivineWrathDestroyed;
			}
		}
		return;
	}

	if (!Effects::DivineWrathIsDue(DivineWrathSecondsSinceLast))
	{
		return;
	}

	const float Burn = Effects::DivineWrathBurn(
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()));
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source || Burn <= 0.0f)
	{
		return;
	}

	// A BEAM, AWAY FROM THE PLAYER AT A RANDOM ANGLE, setting off towards them at once.
	const FVector Centre = Player->GetActorLocation();
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const FVector Where(Centre.X + Effects::DivineWrathAppearsAwayCm * FMath::Cos(Angle),
						Centre.Y + Effects::DivineWrathAppearsAwayCm * FMath::Sin(Angle), Centre.Z);
	ACataclysmGroundZone* Beam = ACataclysmGroundZone::Spawn(
		Source, Where, Effects::DivineWrathRadiusCm, Effects::DivineWrathBeamSeconds, Burn,
		DungeonGameModeTypeOfRow(Effects::DivineWrathKey));
	if (!Beam)
	{
		// THE CLOCK IS NOT RESET ON A FAILED SPAWN, so the next beat tries again.
		return;
	}
	Beam->BurnsOnceASecondAs = FName(Effects::DivineWrathKey);
	Beam->TravelAt(Effects::DivineWrathVelocity(Where, Centre));
	DivineWrathBeam = Beam;
	DivineWrathSecondsSinceLast = 0.0f;
	UE_LOG(LogCataclysm, Log, TEXT("Divine Wrath: a beam appeared %.0f cm from the player on floor %d"),
		   Effects::DivineWrathAppearsAwayCm, FloorNumber);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::AddAChaosTouch()
{
	using Effects = UCataclysmDungeonModifierEffects;

	const int32 Kind = Effects::ChaosTouchedKindToAdd(
		Effects::ChaosTouchedKindFor(DungeonGameModeChaosTouchedRoll()), ChaosTouchedStacks);
	if (Kind == Effects::ChaosTouchedAddsNothing || !ChaosTouchedStacks.IsValidIndex(Kind))
	{
		UE_LOG(LogCataclysm, Log,
			   TEXT("Chaos Touched: floor %d adds nothing; every kind is at its cap"), FloorNumber);
		return;
	}
	ChaosTouchedStacks[Kind] += 1;
	UE_LOG(LogCataclysm, Log, TEXT("Chaos Touched: floor %d adds kind %d, now %d stack(s)"),
		   FloorNumber, Kind, ChaosTouchedStacks[Kind]);
}

void ACataclysmDungeonGameMode::StepChaosTouched(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (ChaosTouchedStacks == ChaosTouchedApplied)
	{
		return;
	}
	ChaosTouchedApplied = ChaosTouchedStacks;
	ApplyChangingFloorEffects(Player, AbilitySystem);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteDeathForChaosTouched(const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// ON ANY FLOOR: the stacks are the dungeon's.
	bool bHeld = false;
	for (const int32 Held : ChaosTouchedStacks)
	{
		bHeld |= Held > 0;
	}
	if (!bHeld)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;

	// THE PLAYER'S OWN DEATH CLEARS EVERY STACK, BUFFS AS WELL, and at once, for the reason the
	// starvation curse's listener gives.
	if (Player && Notice.Victim == Player)
	{
		ChaosTouchedStacks = {0, 0, 0, 0, 0, 0, 0, 0};
		ChaosTouchedApplied = {0, 0, 0, 0, 0, 0, 0, 0};
		ApplyChangingFloorEffects(
			Player, Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent()));
		RefreshFloorModifierPanel();
		return;
	}

	// A FLOOR'S BOSS CLEANSES THE DEBUFFS ONLY; the buffs stay. Ruled 2026-09-24.
	if (DiedAsAFloorsBoss(Notice.Victim))
	{
		for (int32 Kind = Effects::ChaosTouchedFirstDebuff; Kind < Effects::ChaosTouchedKinds;
			 ++Kind)
		{
			ChaosTouchedStacks[Kind] = 0;
		}
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::NoteDeathForSoulHarvest(const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::SoulHarvestKey)))
	{
		return;
	}

	// EVERY DEATH, WHOEVER DEALT IT, but not a risen creature's second: its first death
	// already released its soul. Ruled 2026-09-24.
	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Fallen || !Fallen->PaysForItsDeath())
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!World || !Player)
	{
		return;
	}

	// THE NEAREST LIVING CREATURE WITHIN REACH, found the way Blood-Forged Champions finds
	// its champion. The creature that died is still in the sphere and is skipped.
	ACataclysmEnemyCharacter* Nearest = nullptr;
	float NearestAway = TNumericLimits<float>::Max();
	for (AActor* Found : UCataclysmTargeting::FindEnemiesInSphere(
			 World, Player, Notice.Location, Effects::SoulHarvestRadiusCm()))
	{
		ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Found);
		if (!IsValid(Creature) || Creature == Fallen || UCataclysmSkillEffects::IsDead(Creature))
		{
			continue;
		}
		const float Away = FVector::Dist(Creature->GetActorLocation(), Notice.Location);
		if (Away < NearestAway)
		{
			NearestAway = Away;
			Nearest = Creature;
		}
	}

	for (auto Entry = SoulHarvestHeld.CreateIterator(); Entry; ++Entry)
	{
		if (Entry->Key.IsStale())
		{
			Entry.RemoveCurrent();
		}
	}

	if (!Nearest)
	{
		return;
	}

	// A CREATURE AT THE CAP TAKES NOTHING MORE, and the soul is not passed on: it went to the
	// nearest, and the nearest was full.
	FSoulHarvestHeld& Held = SoulHarvestHeld.FindOrAdd(Nearest);
	const int32 Souls = Effects::SoulHarvestSoulsAfterFeeding(Held.Souls);
	if (Souls == Held.Souls)
	{
		return;
	}
	Held.Souls = Souls;
	++SoulHarvestGiven;
	ApplySoulHarvestFigures(Nearest, /*bFreshBlock=*/false);
	UE_LOG(LogCataclysm, Log, TEXT("Soul Harvest: %s died and %s now holds %d soul(s)"),
		   *Fallen->GetName(), *Nearest->GetName(), Souls);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::ApplySoulHarvestFigures(ACataclysmEnemyCharacter* Creature,
														 bool bFreshBlock)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;
	using Resist = UCataclysmAllResistanceAttributeSet;

	FSoulHarvestHeld* Held = IsValid(Creature) ? SoulHarvestHeld.Find(Creature) : nullptr;
	UAbilitySystemComponent* Abilities =
		Held ? UCataclysmTargeting::AbilitySystemOf(Creature) : nullptr;
	if (!Held || !Abilities)
	{
		return;
	}

	// AFTER A RUNG CHANGE NOTHING A SOUL ADDED IS STILL ON THE CREATURE, so there is nothing
	// to take off before finding its own figures.
	if (bFreshBlock)
	{
		Held->HealthAdded = 0.0f;
		Held->DamageAdded = 0.0f;
		Held->ResistanceAdded = 0.0f;
	}

	// ITS OWN FIGURES ARE WHAT IS THERE NOW LESS WHAT THE SOULS ADDED.
	const float OwnMaximum =
		Abilities->GetNumericAttribute(Vital::GetMaxHealthAttribute()) - Held->HealthAdded;
	const float OwnDamage =
		Abilities->GetNumericAttribute(Combat::GetAttackDamageAttribute()) - Held->DamageAdded;
	const float OwnResistance =
		Abilities->GetNumericAttribute(Resist::GetAllResistanceAttribute())
		- Held->ResistanceAdded;

	const float HealthAdded = Effects::SoulHarvestHealthAdded(OwnMaximum, Held->Souls);
	const float DamageAdded = Effects::SoulHarvestDamageAdded(OwnDamage, Held->Souls);
	const float ResistanceAdded = Effects::SoulHarvestResistanceAdded(Held->Souls);

	// THE MAXIMUM FIRST, BECAUSE THE CLAMP ON HEALTH READS IT. A new soul raises health by
	// what it raised the maximum by: it arrives as health, not as a wound. After a rung change
	// health is left as the rung change left it, because the health the creature carried
	// already held what its souls had given.
	const float Health = Abilities->GetNumericAttribute(Vital::GetHealthAttribute());
	Abilities->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), OwnMaximum + HealthAdded);
	if (!bFreshBlock)
	{
		Abilities->SetNumericAttributeBase(Vital::GetHealthAttribute(),
										   Health + (HealthAdded - Held->HealthAdded));
	}
	Abilities->SetNumericAttributeBase(Combat::GetAttackDamageAttribute(), OwnDamage + DamageAdded);
	Abilities->SetNumericAttributeBase(Resist::GetAllResistanceAttribute(),
									   OwnResistance + ResistanceAdded);

	Held->HealthAdded = HealthAdded;
	Held->DamageAdded = DamageAdded;
	Held->ResistanceAdded = ResistanceAdded;
}

int32 ACataclysmDungeonGameMode::SoulHarvestSoulsOn(const ACataclysmEnemyCharacter* Creature) const
{
	for (const TPair<TWeakObjectPtr<ACataclysmEnemyCharacter>, FSoulHarvestHeld>& Entry :
		 SoulHarvestHeld)
	{
		if (Entry.Key.Get() == Creature)
		{
			return Entry.Value.Souls;
		}
	}
	return 0;
}

void ACataclysmDungeonGameMode::OnLootTaken(const FCataclysmLootTakenNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE INFESTED HOARD FIRST, AND ON ITS OWN TEST: a take reaches every rule that wants it,
	// and Trick or Treat's returns below are about its own row. Issues #1820 and #41.
	NoteLootTakenForInfestedHoard(Notice);

	// A CLICKED DROP, TAKEN BY THE PLAYER, ON A FLOOR CARRYING THE ROW. A material swept up
	// by walking near it is not a choice the player made. Ruled 2026-09-23.
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
	if (!FloorBrief.Modifiers.Contains(FName(Effects::TrickOrTreatKey)) || !Notice.bByHand
		|| !Player || Notice.Taker != Player)
	{
		return;
	}

	// A DROP A RAISED CREATURE DROPPED ROLLS NOTHING, so a trick's pair cannot start
	// another trick and the chain ends after one link at any loot quantity. Ruled
	// 2026-09-23; the entry gives the figures.
	if (Notice.bMarked)
	{
		return;
	}

	++TrickOrTreatPickups;
	if (Effects::TrickOrTreatRaisesEnemies(DungeonGameModeTrickOrTreatRoll()))
	{
		RaiseTheTrickOrTreatPair(Notice.Where);
	}
	else
	{
		// A SECOND TREAT RESTARTS THE CLOCK AND ADDS NOTHING: the haste is on or off.
		TrickOrTreatHasteUntilSeconds =
			static_cast<float>(World->GetTimeSeconds()) + Effects::TrickOrTreatHasteSeconds;
	}
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::RaiseTheTrickOrTreatPair(const FVector& Where)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// THE FLOOR'S KINDS: every kind the floor placed, standing or slain, and never the
	// Gatekeeper, which is the floor's boss rather than one of its creatures. An Imp when the
	// floor placed nothing else.
	TArray<ECataclysmDungeonCreature> Kinds;
	for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : FloorEnemies)
	{
		const ECataclysmDungeonCreature Kind = DungeonGameModeKindOf(Enemy.Get());
		if (Kind != ECataclysmDungeonCreature::Count
			&& Kind != ECataclysmDungeonCreature::Gatekeeper)
		{
			Kinds.AddUnique(Kind);
		}
	}
	if (Kinds.IsEmpty())
	{
		Kinds.Add(ECataclysmDungeonCreature::Imp);
	}

	for (int32 Index = 0; Index < Effects::TrickOrTreatEnemies; ++Index)
	{
		FCataclysmEnemyPlacement Placement;
		Placement.Cell = CurrentFloor->CellOfWorld(Where);
		Placement.Creature = Kinds[FMath::RandRange(0, Kinds.Num() - 1)];
		ACataclysmEnemyCharacter* Raised =
			SpawnPlacedCreature(Placement, FloorBrief.SightRadiusMultiplier);
		if (!Raised)
		{
			continue;
		}
		FloorEnemies.Add(Raised);
		Raised->bRaisedByARule = true;
		CreaturesRaisedByARule.Add(Raised);
		++TrickOrTreatRaised;
	}
}

bool ACataclysmDungeonGameMode::TrickOrTreatIsHasting() const
{
	const UWorld* World = GetWorld();
	return World && TrickOrTreatHasteUntilSeconds >= 0.0f
		&& World->GetTimeSeconds() < TrickOrTreatHasteUntilSeconds;
}

void ACataclysmDungeonGameMode::StepTrickOrTreat(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	const float Wanted = TrickOrTreatIsHasting()
		? UCataclysmDungeonModifierEffects::TrickOrTreatHastePercent
		: 0.0f;
	if (!TrickOrTreatIsHasting())
	{
		TrickOrTreatHasteUntilSeconds = -1.0f;
	}
	if (Wanted == TrickOrTreatHasteApplied)
	{
		return;
	}
	TrickOrTreatHasteApplied = Wanted;
	ApplyChangingFloorEffects(Player, AbilitySystem);
	RefreshFloorModifierPanel();
}

bool ACataclysmDungeonGameMode::DiedAsAFloorsBoss(const AActor* Died)
{
	const ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Died);
	return Creature
		&& (Creature->IsBoss() || Creature->IsA<ACataclysmGatekeeperCharacter>());
}

void ACataclysmDungeonGameMode::AddAStarvationCurse()
{
	using Effects = UCataclysmDungeonModifierEffects;

	// ONE KIND PER FLOOR, drawn evenly between the row's two examples; a draw for a kind at
	// its cap goes to the other, and a floor adds nothing only when both are full.
	const int32 Kind = Effects::StarvationCurseKindToAdd(
		Effects::StarvationCurseKindFor(DungeonGameModeStarvationCurseRoll()),
		StarvationCurseMovementStacks, StarvationCurseHealthStacks);
	if (Kind == Effects::StarvationCurseAddsNothing)
	{
		UE_LOG(LogCataclysm, Log,
			   TEXT("Starvation Curse: floor %d adds nothing; both kinds are at their cap"),
			   FloorNumber);
		return;
	}
	int32& Stacks = Kind == Effects::StarvationCurseSlowsMovement
		? StarvationCurseMovementStacks
		: StarvationCurseHealthStacks;
	// ONE STACK. `StarvationCurseKindToAdd` above is what keeps each kind at its cap.
	Stacks += 1;
	UE_LOG(LogCataclysm, Log,
		   TEXT("Starvation Curse: floor %d adds %s; %d movement and %d health stacks held"),
		   FloorNumber,
		   Kind == Effects::StarvationCurseSlowsMovement ? TEXT("slower movement")
														 : TEXT("less maximum health"),
		   StarvationCurseMovementStacks, StarvationCurseHealthStacks);
}

void ACataclysmDungeonGameMode::StepStarvationCurse(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (StarvationCurseMovementStacks == StarvationCurseMovementApplied
		&& StarvationCurseHealthStacks == StarvationCurseHealthApplied)
	{
		return;
	}
	StarvationCurseMovementApplied = StarvationCurseMovementStacks;
	StarvationCurseHealthApplied = StarvationCurseHealthStacks;
	ApplyChangingFloorEffects(Player, AbilitySystem);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteDeathForStarvationCurse(
	const FCataclysmDeathNotice& Notice)
{
	// ON ANY FLOOR, not only one carrying the row: the stacks are the dungeon's.
	if (StarvationCurseMovementStacks == 0 && StarvationCurseHealthStacks == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;

	// THE PLAYER'S OWN DEATH CLEARS IT, AND AT ONCE, for the reason Wasting Sickness's
	// listener gives: `Revive` refills the vitals by reading the maximums.
	if (Player && Notice.Victim == Player)
	{
		StarvationCurseMovementStacks = 0;
		StarvationCurseHealthStacks = 0;
		StarvationCurseMovementApplied = 0;
		StarvationCurseHealthApplied = 0;
		ApplyChangingFloorEffects(
			Player, Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent()));
		RefreshFloorModifierPanel();
		return;
	}

	// AND A FLOOR'S BOSS CLEANSES BOTH. Left to the beat, which puts the change on the
	// character within a quarter of a second.
	if (DiedAsAFloorsBoss(Notice.Victim))
	{
		StarvationCurseMovementStacks = 0;
		StarvationCurseHealthStacks = 0;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::FeedTheFinalBoss(ACataclysmEnemyCharacter* Boss)
{
	if (!IsValid(Boss))
	{
		return;
	}
	NothingIsForgottenBoss = Boss;
	ApplyNothingIsForgottenFigures(Boss);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::ApplyNothingIsForgottenFigures(ACataclysmEnemyCharacter* Creature)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;
	using Combat = UCataclysmCombatAttributeSet;

	if (!IsValid(Creature) || NothingIsForgottenBoss.Get() != Creature)
	{
		return;
	}
	UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Creature);
	if (!Abilities)
	{
		return;
	}

	// HEALTH, UNCAPPED, onto its maximum, and it arrives full: the void's gift is not a
	// wound. The maximum is written first, because the clamp on health reads it.
	const float Maximum = Abilities->GetNumericAttribute(Vital::GetMaxHealthAttribute())
		+ NothingIsForgottenHealth;
	Abilities->SetNumericAttributeBase(Vital::GetMaxHealthAttribute(), Maximum);
	Abilities->SetNumericAttributeBase(Vital::GetHealthAttribute(), Maximum);

	// DAMAGE, UP TO THE CAP of the boss's own. Ruled under the owner's delegation.
	const float Own = Abilities->GetNumericAttribute(Combat::GetAttackDamageAttribute());
	const float Added = Effects::NothingIsForgottenDamageAdded(NothingIsForgottenDamage, Own);
	Abilities->SetNumericAttributeBase(Combat::GetAttackDamageAttribute(), Own + Added);

	NothingIsForgottenHealthGiven = NothingIsForgottenHealth;
	NothingIsForgottenDamageGiven = Added;
	UE_LOG(LogCataclysm, Log,
		   TEXT("Nothing Is Forgotten: the final boss %s takes %.0f health and %.0f damage "
				"of the %.0f the void held"),
		   *Creature->GetName(), NothingIsForgottenHealth, Added, NothingIsForgottenDamage);
}

void ACataclysmDungeonGameMode::RaiseTheUnstablePortalsWarden()
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// AN ABYSSAL WARDEN BESIDE THE PORTAL, AT THE MINI-BOSS RUNG, its modifiers drawn for
	// that rung as any creature's are. The rung is set before they are drawn; see
	// `SpawnPlacedCreature`.
	FCataclysmEnemyPlacement Placement;
	Placement.Cell = CurrentFloor->CellOfWorld(CurrentFloor->ExitWorld());
	Placement.Creature = ECataclysmDungeonCreature::AbyssalWarden;
	ACataclysmEnemyCharacter* Warden = SpawnPlacedCreature(
		Placement, FloorBrief.SightRadiusMultiplier, Effects::UnstablePortalMiniBossRung);
	if (!Warden)
	{
		return;
	}
	FloorEnemies.Add(Warden);
	Warden->bRaisedByARule = true;
	CreaturesRaisedByARule.Add(Warden);
}

int32 ACataclysmDungeonGameMode::BloodGatesPlacedCount() const
{
	// THE PLAYER'S KILLS PLUS THE UNMARKED STILL STANDING, and not every death: a
	// creature that died to anything but the player leaves both this count and the
	// target, so once none stands the gate is open. See `BloodGatesKey`.
	int32 Standing = 0;
	for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : FloorEnemies)
	{
		if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy)
			&& Enemy->PaysForItsDeath()
			&& !CreaturesRaisedByARule.Contains(Enemy.Get()))
		{
			++Standing;
		}
	}
	return BloodGatesSlain + Standing;
}

bool ACataclysmDungeonGameMode::BloodGatesSealTheStairs() const
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE LAST FLOOR IS NOT SEALED: its stairs lead out of the dungeon, and the row
	// seals "doors leading to the next level". Ruled under the owner's delegation.
	return FloorBrief.Modifiers.Contains(FName(Effects::BloodGatesKey))
		&& !IsOnTheLastFloor()
		&& !Effects::BloodGatesAreOpen(BloodGatesSlain, BloodGatesPlacedCount());
}

void ACataclysmDungeonGameMode::NoteDeathForBloodGates(
	const FCataclysmDeathNotice& Notice)
{
	if (!FloorBrief.Modifiers.Contains(FName(UCataclysmDungeonModifierEffects::BloodGatesKey)))
	{
		return;
	}

	// "THE PLAYER HAS SLAIN": the killer on the notice, the question Vengeful Wraiths
	// asks. A minion's kill is the minion's unless its summoner holds Conduit, which
	// `UCataclysmCombatEvents::NoteBlow` decides in one place (issue #1515). A MARKED
	// creature is the floor's dead brought back, and is not counted.
	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
	// A WARDEN THE UNSTABLE PORTAL RAISED IS NOT THE FLOOR'S TO COUNT, slain or standing,
	// so it cannot seal again stairs the player had opened. Issues #1820 and #41.
	if (Fallen && Fallen->PaysForItsDeath() && Player && Notice.Killer == Player
		&& !CreaturesRaisedByARule.Contains(Fallen))
	{
		++BloodGatesSlain;
	}

	RefreshFloorModifierPanel();
}

bool ACataclysmDungeonGameMode::GoDownOneFloor(APawn* PawnToMove)
{
	// A DUNGEON FROM THE EMPIRE MAP HAS A BOTTOM. Reaching it is beating the
	// dungeon rather than finding another floor, and the stairs stop there.
	// Nothing moves the player anywhere, because there is nowhere to go: the
	// capital hub is issue #48. Issue #1092.
	if (IsOnTheLastFloor())
	{
		ClearEmpireDungeon();
		return false;
	}

	// FROM THE FLOOR ACTUALLY BEING WALKED rather than from the setting, because
	// those are not always the same number: `Cataclysm.DungeonFloor` can pin one.
	if (!GoToFloor(ChooseFloorNumber() + 1, PawnToMove))
	{
		return false;
	}

	++FloorsDescended;

	// AND A FLOOR COSTS TIME. One day as a starting rate, and less than that once
	// a city upgrade or the empire tree has shortened the walk -- see
	// `FCataclysmDungeon::WalkDaysPerFloor`, which is the rate this charges.
	// Until issue #1092 nothing in the game spent any of it.
	//
	// WHAT DOES NOT MOVE WHEN THE WALK GETS SHORTER: the floor count, what the
	// dungeon is worth, and when it bites. Depth and reward are the same axis;
	// depth and time are not, once a player has invested in separating them.
	//
	// HERE AND NOT IN `GoToFloor`, because that is also how the first floor is
	// built when play begins and how `Cataclysm.DungeonFloor` jumps to a floor to
	// look at it. Neither is a floor the player walked down to. The day for floor
	// 1 is spent by `EnterEmpireDungeon`.
	SpendFloorTimeInTheEmpire();

	return true;
}

// ---------------------------------------------------------------------------
// Walking a dungeon that stands on the empire map, issue #1092
// ---------------------------------------------------------------------------

void ACataclysmDungeonGameMode::SetEmpireRunForTests(UCataclysmEmpireRun* Run)
{
	EmpireRunForTests = Run;
}

UCataclysmEmpireRun* ACataclysmDungeonGameMode::EmpireRun() const
{
	// THE GAME'S OWN RUN FIRST, ALWAYS. The test seam is only reached when there
	// is no game instance of this project's class at all, which is the case a
	// headless test is in and nothing in a running game ever is.
	//
	// NOT STARTING ONE. A player pressing Play in `L_Dungeon` to look at a floor
	// is not beginning a campaign, and a run started here would be one nothing
	// else knows about.
	if (UCataclysmEmpireRun* Run =
			UCataclysmGameInstance::EmpireRunFor(this, /*bStartIfNone*/ false))
	{
		return Run;
	}

	return EmpireRunForTests;
}

const FCataclysmDungeon* ACataclysmDungeonGameMode::BoundDungeon() const
{
	const UCataclysmEmpireRun* Run = EmpireRun();

	return (Run && EmpireDungeonId != INDEX_NONE)
		? Run->FindDungeon(EmpireDungeonId) : nullptr;
}

int32 ACataclysmDungeonGameMode::EmpireDungeonFloors() const
{
	const FCataclysmDungeon* Dungeon = BoundDungeon();
	return Dungeon ? Dungeon->Floors : 0;
}

bool ACataclysmDungeonGameMode::IsOnTheLastFloor() const
{
	const int32 Floors = EmpireDungeonFloors();

	// NO DUNGEON MEANS NO BOTTOM, which is what keeps the stairs descending for
	// ever in the sandbox.
	return Floors > 0 && ChooseFloorNumber() >= Floors;
}

void ACataclysmDungeonGameMode::SpendFloorTimeInTheEmpire()
{
	UCataclysmEmpireRun* Run = EmpireRun();
	if (Run == nullptr)
	{
		return;
	}

	const FCataclysmDungeon* Dungeon = BoundDungeon();

	// A WHOLE DAY WHEN NOTHING IS BOUND. Pressing Play puts the player on a
	// generated floor with no empire dungeon behind it, and there is then no
	// rate to read; a floor costs the ordinary day.
	if (Dungeon == nullptr || Run->Clock == nullptr)
	{
		Run->AdvanceDay();
		return;
	}

	// OTHERWISE THE DUNGEON'S OWN RATE, which is one day a floor unless a city
	// upgrade lowered it. `SpendDays` advances a whole day for each whole day
	// that accumulates, so timers move and dungeons resolve exactly as they
	// would have.
	Run->SpendFloorTime(Dungeon->WalkDaysPerFloor());
}

bool ACataclysmDungeonGameMode::EnterEmpireDungeon(int32 DungeonId)
{
	UCataclysmEmpireRun* Run = EmpireRun();
	if (!Run)
	{
		return false;
	}

	const FCataclysmDungeon* Dungeon = Run->FindDungeon(DungeonId);
	if (!Dungeon)
	{
		return false;
	}

	// THE DUNGEON'S OWN NUMBERS, NOT THE SETTINGS'. `TotalFloors` and
	// `DungeonSeed` were stand-ins for exactly these; this header used to say
	// there was no dungeon object to take them from.
	//
	// THE SEED IS DERIVED FROM THE DUNGEON'S NUMBER rather than stored on it,
	// because a dungeon is a strategy-layer record and how a floor is carved is
	// not its business. Any two dungeons get different floors and one dungeon
	// gets the same floors twice, which is all a seed has to do.
	EmpireDungeonId = DungeonId;
	DungeonSeed = FMath::Max(1, DungeonId + 1);
	TotalFloors = FMath::Max(1, Dungeon->Floors);
	DungeonType = Dungeon->Type;

	// AND WHAT IT DOES DIFFERENTLY. `UCataclysmEnemyScore` reads this back off
	// the game mode and adds the sub-type's weight to every creature on the
	// floor, so a Sacrificial dungeon's enemies really are worth more than a
	// plain one's. Until the surge scheduler rolled a sub-type this was left at
	// whatever the settings said, which in a real run was always `None`.
	DungeonSubType = Dungeon->SubType;

	// AND WHAT ITS MODIFIERS ADD. `UCataclysmEnemyScore` reads this back off the
	// game mode and adds it to every creature on the floor, which is how a
	// dungeon modifier makes a dungeon harder --
	// `docs/Cataclysm_GDD_v2.md` section VIII. Until issue #41's modifier slice
	// this was hard-zeroed in the score model itself.
	DungeonModifierScore = Dungeon->ModifierScore;

	// AND WHICH ONES THEY ARE, AND WHAT ELSE THIS DUNGEON COULD HAVE DRAWN.
	// Both are needed by rules that decide a floor's modifiers rather than a
	// dungeon's: a Volatile dungeon re-draws them for every floor and needs the
	// pool, and the Unstable Dimensions modifier adds one per floor and needs to
	// know whether the dungeon is carrying it. `FCataclysmDungeonFloorRules` is
	// where both rules live. Issue #41.
	//
	// NARROWED ONCE, HERE, rather than on every floor.
	// `UCataclysmEmpireRun::ModifierPool` is the whole 117-row table and
	// `PoolFor` cuts it to the Cataclysms this run is facing, which does not
	// change while the player is inside one dungeon.
	DungeonModifiers = Dungeon->Modifiers;
	DungeonModifierPool = UCataclysmDungeonModifierRules::PoolFor(
		Run->ModifierPool, Run->ActiveCataclysms);

	// AND THE PLAYER STARTS AT ITS ENTRANCE. Without this the floor being walked
	// is whatever the last dungeon left behind, and entering a shallower one
	// while standing deep in a deeper one makes `IsOnTheLastFloor` true straight
	// away -- so the new dungeon is beaten without a single floor being walked.
	// Found by a test that walked four dungeons in a row and spent 16 days doing
	// it.
	GoToFloor(1);

	// AND ITS TIMER STOPS. The one dungeon the player is standing in does not
	// count down; every other one does. See
	// `UCataclysmDayClock::bTimerTicksWhileRunning`.
	if (Run->Clock)
	{
		Run->Clock->EnterDungeon(DungeonId);
	}

	// FLOOR 1 IS A FLOOR, AND A FLOOR COSTS A DAY TO BEGIN WITH. Walking N
	// floors costs N days at that starting rate: one for arriving and one for
	// each descent. `SpendFloorTimeInTheEmpire` charges the dungeon's own
	// `WalkDaysPerFloor`, which a city upgrade can have lowered.
	SpendFloorTimeInTheEmpire();

	return true;
}

void ACataclysmDungeonGameMode::LeaveEmpireDungeon()
{
	if (UCataclysmEmpireRun* Run = EmpireRun())
	{
		if (Run->Clock)
		{
			Run->Clock->LeaveDungeon();
		}
	}

	EmpireDungeonId = INDEX_NONE;

	// AND THE DUNGEON'S MODIFIERS GO WITH IT. `EnterEmpireDungeon` overwrites
	// `TotalFloors`, `DungeonType` and `DungeonSubType` on the way in, so those
	// are replaced by the next dungeon rather than lingering -- but a player who
	// LEAVES the empire and walks a plain floor would otherwise still be
	// fighting creatures carrying the last dungeon's modifier score.
	//
	// ALL FOUR AND NOT ONLY THE SCORE. Since issue #41's sub-type slice the
	// number the score model actually reads is the FLOOR's, not the dungeon's,
	// so clearing the dungeon's alone would leave the last floor's modifiers in
	// force on a plain floor. The row keys and the pool go too, or the next
	// dungeon's floors would draw from the Cataclysms the last run faced.
	DungeonModifierScore = 0.0f;
	DungeonModifiers.Reset();
	DungeonModifierPool.Reset();
	FloorBrief = FCataclysmFloorBrief();

	// AND THE ARMOUR MARCH OF PROGRESS PAID FOR GOES WITH THE RUN. Issues #1820 and #41.
	// It is the one thing a floor rule grants that outlives a floor change, because the
	// row pays for "the Commander in each level" and never takes it back -- so this is
	// the only place it can end. The call below takes it off the character: the brief is
	// empty now, so the applier writes an armour modifier of nothing.
	MarchOfProgressCommandersKilled = 0;

	// AND NO DEATH OF THIS DUNGEON ECHOES INTO THE NEXT. Issues #1820 and #41.
	EchoesThisFloor.Reset();
	EchoesFromLastFloor.Reset();
	ForgetThePlagueHarbingers();
	ForgetTheChoruses();
	ForgetTheBlooms();
	ForgetTheSpires();

	// AND PESTILENT EMPOWERMENT'S BEACONS AND THE COUNT CARRIED BETWEEN FLOORS, beside Echoes' reset:
	// no beacon of this dungeon strengthens the next one. Issues #1820 and #41.
	ForgetTheBeacons();
	PestilentBeaconsLeftStanding = 0;
	ForgetThePortals();

	// AND ABYSSAL RIFTS' SUCCESSES END WITH THE DUNGEON, as ruled; the call below takes their magic find off.
	ForgetTheRift();
	AbyssalRiftSuccesses = 0;
	AbyssalRiftSuccessesApplied = 0;
	ForgetTheLocusts();

	// AND RAW SEWAGE'S RIVERS AND STACKS: the stacks are the dungeon's and end with it. The beat takes the disease
	// tag off the player, since the step runs while it is held. Issues #1820 and #41.
	ForgetTheRivers();
	RawSewageStacks = 0;
	RawSewageSecondsInARiver = 0.0f;
	bRawSewageInARiver = false;
	ForgetTheVeins();
	ForgetTheVoidParasite();
	ForgetTheSarcophagi();

	// AND WHAT THEY WERE DOING TO THE PLAYER STOPS. The brief is empty now, so
	// this takes Starvation's and Dehydration's share back off the player's
	// maximums, gives back the resistance The Nihil's Embrace had taken, stops
	// Forced March counting, and hides the floor panel. Issue #41, slice 2.
	ApplyFloorRulesToPlayer();
}

TArray<FName> ACataclysmDungeonGameMode::ChooseModifiers(float& OutScore) const
{
	TArray<FString> NotUnderstood;
	const TArray<FName> Asked = DungeonGameModeModifiersAskedFor(NotUnderstood);
	if (Asked.IsEmpty())
	{
		OutScore = DungeonModifierScore;
		return DungeonModifiers;
	}

	// THE TYPED MODIFIERS' OWN DANGER, read from the table the way the empire
	// draw reads it, so a creature on a floor carrying typed modifiers is worth
	// what the same floor carrying drawn ones would be.
	const UDataTable* Table = UCataclysmDungeonModifierTable::LoadDungeonModifierTable();
	OutScore = 0.0f;
	for (const FName Key : Asked)
	{
		if (const FCataclysmDungeonModifierRow* Row =
				UCataclysmDungeonModifierTable::FindRow(Table, Key))
		{
			OutScore += Row->Weight;
		}
	}

	return Asked;
}

bool ACataclysmDungeonGameMode::ApplyFloorRulesTo(
	UCataclysmAbilitySystemComponent* AbilitySystem,
	UCataclysmEquipmentComponent* Equipment) const
{
	// THE FLOOR'S LIST AND THE FLOOR'S NUMBER, both from the brief. The floor's
	// list rather than the dungeon's, so a Volatile dungeon that re-draws
	// Starvation onto one floor starves the player on that floor only; and the
	// brief's number, which for a Horde dungeon is the wave.
	return UCataclysmDungeonModifierEffects::ApplyToCharacter(
		UCataclysmDungeonModifierEffects::PlayerEffectsFor(
			FloorBrief.Modifiers, FloorBrief.FloorNumber),
		AbilitySystem, Equipment);
}

void ACataclysmDungeonGameMode::StepInfernalRain(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	// WHAT IS STILL ALIGHT, ASKED RATHER THAN REMEMBERED. A patch destroys itself
	// when its life ends, so a weak pointer going invalid IS the expiry and
	// nothing has to be told about it.
	InfernalRainPatches.RemoveAll([](const TWeakObjectPtr<ACataclysmGroundZone>& Patch)
	{
		return !Patch.IsValid();
	});

	// THE ONE PLACE THIS CLOCK MOVES. See the field's comment: a second writer is
	// the fault that was repaired in the creature auras earlier today.
	InfernalRainSecondsSinceLastPatch += SecondsBetweenWaveChecks;
	if (!UCataclysmDungeonModifierEffects::InfernalRainPatchIsDue(
			InfernalRainSecondsSinceLastPatch, InfernalRainPatches.Num()))
	{
		return;
	}

	// EVERYTHING THE PATCH NEEDS IS IN HAND BEFORE ANYTHING IS CREATED, and the
	// order is deliberate. `ACataclysmFloorHazardSource::ForFloor` SPAWNS the
	// source when a floor has none, so asking it first and then finding a reason
	// not to place a patch would leave an actor on the floor that nothing uses.
	// The two reasons are a table that will not load and a player with no maximum
	// health, and both are cheap to check.

	// THE TYPE COMES OUT OF THE ROW AND IS NOT WRITTEN HERE. Every row of
	// game/Data/DungeonModifiers.csv carries a CataclysmType -- this one is
	// Demonic -- so reading it means the damage cannot disagree with the modifier
	// that placed it, and a row retyped in the workbook retypes its hazard with no
	// code change. A constant here would be this file's opinion of the data.
	//
	// AN UNREADABLE TABLE PLACES NOTHING RATHER THAN GUESSING. An empty type is
	// untyped damage, which meets none of the player's eight resistances -- harsher
	// than the row intends, and the fault the first commit on this branch exists to
	// fix -- so a missing table must not silently produce one.
	const FCataclysmDungeonModifierRow* Row = UCataclysmDungeonModifierTable::FindRow(
		UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
		FName(UCataclysmDungeonModifierEffects::InfernalRainKey));
	if (!Row)
	{
		return;
	}

	// A SHARE OF THE PLAYER'S OWN MAXIMUM HEALTH, READ THROUGH THE ABILITY SYSTEM
	// the same way `StepForcedMarch` reads it below, so the two rules cannot
	// disagree about what a character's maximum health is.
	//
	// `InfernalRainDamagePerSecond` answers zero for a character with no maximum,
	// and nothing is spawned for a reading nobody can have. The check below is not
	// belt and braces: without it this rule would lay patches that do nothing,
	// three at a time, for ever, and the cap would count them.
	//
	// AND IT IS THIS RULE'S JOB RATHER THAN THE PATCH'S. `ACataclysmGroundZone`
	// skips a sweep only when a patch neither damages nor applies an effect, which
	// changed with issue #1701 so that Singularity Wells can have a well that slows
	// without damaging. Infernal Rain's patches carry no effect, so a zero-damage
	// one would do nothing -- but relying on the patch to notice is relying on a
	// rule that is about other patches.
	const float PerSecond =
		UCataclysmDungeonModifierEffects::InfernalRainDamagePerSecond(
			AbilitySystem->GetNumericAttribute(
				UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
	if (PerSecond <= 0.0f)
	{
		return;
	}

	// WHERE IT FALLS. "In combat zones" read as "near the player", which is the
	// only thing this rule can locate on every beat. Flattened to the player's own
	// height so a patch is on the floor they are standing on rather than at a
	// height picked from a random vector.
	//
	// NOT ON THE PLAYER'S FEET, AND NOT TOUCHING THEM EITHER, WHICH IS WHY THE
	// NEAREST DISTANCE IS PAST THE PATCH'S OWN RADIUS RATHER THAN AT IT. A patch
	// that covers a standing player the instant it is laid damages them before
	// they can react, and the row describes ground to get off rather than an
	// unavoidable hit. `UCataclysmTargeting::IsInLine` decides who is inside with
	// `<=`, so a centre at exactly the radius DOES clip them; one centimetre past
	// it is what makes "outside when laid" true rather than almost always true.
	// Path of Exile 2's players complain about exactly this in its own
	// burning-ground modifier -- a patch that damages instantly on appearing.
	const FVector Centre = Player->GetActorLocation();
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Away = FMath::FRandRange(
		UCataclysmDungeonModifierEffects::InfernalRainRadiusCm + 1.0f,
		UCataclysmDungeonModifierEffects::InfernalRainFallsWithinCm);
	const FVector Where(Centre.X + Away * FMath::Cos(Angle),
						Centre.Y + Away * FMath::Sin(Angle),
						Centre.Z);

	// THE SOURCE IS MADE LAST, because `ForFloor` spawns one when the floor has
	// none and everything that could refuse has now been asked.
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}
	// TYPED BY ITS ROW, ON THE PATCH ITSELF. Issue #1924.
	ACataclysmGroundZone* Patch = ACataclysmGroundZone::Spawn(
		Source, Where, UCataclysmDungeonModifierEffects::InfernalRainRadiusCm,
		UCataclysmDungeonModifierEffects::InfernalRainPatchSeconds, PerSecond,
		FName(*Row->CataclysmType));
	if (Patch)
	{
		// PATCHES THAT OVERLAP BURN ONCE A SECOND BETWEEN THEM. Issue #2074.
		Patch->BurnsOnceASecondAs = FName(UCataclysmDungeonModifierEffects::InfernalRainKey);
	}
	if (!Patch)
	{
		// THE CLOCK IS NOT RESET ON A FAILED SPAWN, so the next beat tries again
		// rather than waiting a whole cadence for a patch that never existed.
		return;
	}

	InfernalRainPatches.Add(Patch);
	InfernalRainSecondsSinceLastPatch = 0.0f;
}

void ACataclysmDungeonGameMode::StepSingularityWells(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	using Effects = UCataclysmDungeonModifierEffects;

	// WHAT IS STILL THERE, ASKED RATHER THAN REMEMBERED. A well is destroyed with
	// the rest of the floor's contents, so a weak pointer going invalid IS that.
	SingularityWells.RemoveAll([](const TWeakObjectPtr<ACataclysmGroundZone>& Well)
	{
		return !Well.IsValid();
	});

	// THE SLOW FIRST, FROM WHAT EXISTS, ON EVERY BEAT. This is the half that must
	// not sit inside the placement branch: the beat a player walks out of a well
	// is a beat on which nothing is placed, and a slow left behind would follow
	// them around the floor.
	//
	// EACH WELL IS ASKED WHETHER IT COVERS THE PLAYER. `Covers` is the same test
	// the well's own sweep makes, so what slows a character and what damages them
	// cannot disagree about where the well is.
	const FVector Feet = Player->GetActorLocation();
	bool bInsideAWell = false;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Well : SingularityWells)
	{
		if (Well.IsValid() && Well->Covers(Feet))
		{
			bInsideAWell = true;
			break;
		}
	}

	// ONE WELL OR THREE MAKE NO DIFFERENCE, and that is deliberate rather than an
	// oversight. The row states one figure, 40%, and says nothing about standing
	// in two at once; stacking it would reach 120% on three overlapping wells,
	// which the pipeline floors at -99 anyway and which the row does not ask for.
	const float Wanted = bInsideAWell ? Effects::SingularityWellsSlowPercent : 0.0f;
	if (!FMath::IsNearlyEqual(Wanted, SingularityWellsSlowApplied))
	{
		SingularityWellsSlowApplied = Wanted;
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}

	// AND NOW WHETHER TO PLACE ANOTHER. The cap is asked inside the predicate,
	// before its clock, so a floor at its limit does not swallow the count.
	SingularityWellsSecondsSinceLastWell += SecondsBetweenWaveChecks;
	if (!Effects::SingularityWellIsDue(SingularityWellsSecondsSinceLastWell,
									   SingularityWells.Num()))
	{
		return;
	}

	// EVERYTHING THAT CAN REFUSE IS ASKED BEFORE ANYTHING IS CREATED, because
	// `ACataclysmFloorHazardSource::ForFloor` SPAWNS the source when a floor has
	// none, and an actor nothing uses would be left behind otherwise.
	//
	// THE TYPE COMES OUT OF THE ROW. Every row of game/Data/DungeonModifiers.csv
	// carries a CataclysmType -- this one is Void -- so the damage cannot disagree
	// with the modifier that placed it, and a row retyped in the design workbook
	// retypes its wells with no code change. An unreadable table places nothing
	// rather than guessing: an empty type is untyped damage, which meets none of
	// the player's eight resistances.
	const FCataclysmDungeonModifierRow* Row = UCataclysmDungeonModifierTable::FindRow(
		UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
		FName(Effects::SingularityWellsKey));
	if (!Row)
	{
		return;
	}

	// A SHARE OF THE PLAYER'S OWN MAXIMUM HEALTH, read through the ability system
	// the same way the rules above read it.
	const float PerSecond = Effects::SingularityWellDamagePerSecond(
		AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
	if (PerSecond <= 0.0f)
	{
		return;
	}

	// WHERE IT APPEARS: near the player, past its own radius, at their height.
	//
	// NEAR THE PLAYER BECAUSE OTHERWISE NOBODY MEETS IT, and that is measured
	// rather than assumed. Three wells of this radius cover 0.33% of a 160 by 160
	// metre floor; placed within this distance they cover 18.8% of the circle
	// around the player and leave 81% of it clear.
	//
	// PAST THE RADIUS RATHER THAN AT IT, for the reason `StepInfernalRain` gives:
	// `UCataclysmTargeting::IsInLine` decides who is inside with `<=`, so a well
	// centred at exactly the radius covers a player standing still. A well that
	// slows on arrival is worse than a patch that burns on arrival, because the
	// player cannot step out as quickly.
	const FVector Centre = Player->GetActorLocation();
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Away = FMath::FRandRange(Effects::SingularityWellsRadiusCm + 1.0f,
										 Effects::SingularityWellsFallsWithinCm);
	const FVector Where(Centre.X + Away * FMath::Cos(Angle),
						Centre.Y + Away * FMath::Sin(Angle),
						Centre.Z);

	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}
	// IT LASTS THE FLOOR, WHICH THE ROW NEITHER STATES NOR CONTRADICTS.
	// `SpawnForTheFloor` exists for the hazard rows of issue #1605 that state no
	// duration, and "pulsing void orbs" reads as a feature of the floor rather
	// than a passing strike. The cap of three is what keeps that from becoming a
	// floor that is slow everywhere.
	//
	// START AND END THE SAME POINT MAKES IT ROUND, which is how `Spawn` builds a
	// circle too: a segment of no length is a circle at that point.
	// TYPED BY ITS ROW, ON THE WELL ITSELF. Issue #1924.
	ACataclysmGroundZone* Well = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Where, Where, Effects::SingularityWellsRadiusCm, PerSecond,
		/*bAffectsEveryone=*/false, /*InDrawnAsType=*/NAME_None,
		FName(*Row->CataclysmType));
	if (Well)
	{
		// WELLS THAT OVERLAP BURN ONCE A SECOND BETWEEN THEM. Issue #2074.
		Well->BurnsOnceASecondAs = FName(Effects::SingularityWellsKey);
	}
	if (!Well)
	{
		// THE CLOCK IS NOT RESET ON A FAILED SPAWN, so the next beat tries again
		// rather than waiting a whole cadence for a well that never existed.
		return;
	}

	SingularityWells.Add(Well);
	SingularityWellsSecondsSinceLastWell = 0.0f;
}

void ACataclysmDungeonGameMode::StepFloorRulesThatChange()
{
	// NOTHING TO DO ON A FLOOR CARRYING NONE OF THEM, which is almost every
	// floor, and this is what that costs: three tests of a short array.
	const bool bForcedMarch = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::ForcedMarchKey));
	const bool bNihilsEmbrace = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::NihilsEmbraceKey));
	const bool bDeathsEmbrace = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::DeathsEmbraceKey));

	// AND THE ONE RULE HERE THAT CHANGES THE FLOOR RATHER THAN THE PLAYER.
	// Issues #1605 and #41. Everything else in this function puts a stat
	// modifier on the character; this one drops a patch of burning ground and
	// touches no stat at all.
	const bool bInfernalRain = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::InfernalRainKey));
	// AND THE SECOND RULE THAT CHANGES THE FLOOR RATHER THAN ONLY THE PLAYER.
	// Issues #1605 and #41. It does both: it places actors AND it moves a stat.
	const bool bSingularityWells = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::SingularityWellsKey));
	// AND WITHERED GROUND, WHICH PLACES NOTHING HERE. Its patches are placed by
	// a death; this beat only asks whether the player is standing on one.
	// Issue #41.
	const bool bWitheredGround = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::WitheredGroundKey));
	// AND MORTAL DECAY, WHICH TAKES HEALTH RATHER THAN MOVING A STAT OR PLACING
	// ANYTHING. Issues #1786 and #41. Forced March is the only other rule here
	// of that shape.
	const bool bMortalDecay = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::MortalDecayKey));
	// AND SUFFERING AURA, MORTAL DECAY'S SHAPE: it takes health and mana directly and
	// moves no stat. Issues #1820 and #41.
	const bool bSufferingAura = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::SufferingAuraKey));
	// AND WASTING SICKNESS, WHOSE BEAT DECIDES NOTHING. Issues #1786 and #41. Its
	// stacks move on events; this beat only puts them back on the player after a
	// floor change took them off.
	const bool bWastingSickness = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::WastingSicknessKey));
	// AND GRASPING TENTACLES, WHICH PLACES ACTORS AND MOVES A STAT, the shape
	// Singularity Wells has. Issues #1786 and #41.
	const bool bGraspingTentacles = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::GraspingTentaclesKey));
	// AND THE EDICT OF SILENCE, WHICH PLACES NOTHING AND MOVES ONE STAT ON A
	// CLOCK. Issues #1786 and #41.
	const bool bEdictOfSilence = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::EdictOfSilenceKey));
	// AND DIRGE RESONANCE, WHICH CHANGES CREATURES AND NEVER THE PLAYER, on the Edict's
	// clock. Issues #1820 and #41.
	const bool bDirgeResonance = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::DirgeResonanceKey));
	const bool bArtilleryStrike = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::ArtilleryStrikeKey));
	const bool bHallowedGroundfall = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::HallowedGroundfallKey));
	// AND FUNGAL OVERGROWTH, WHICH PLACES NOTHING HERE. Its mushrooms are placed
	// by a death, so this beat only asks whether the player is standing on one --
	// which is Withered Ground's shape above. Issues #1820 and #41.
	const bool bFungalOvergrowth = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::FungalOvergrowthKey));
	// AND HOLY REPERCUSSIONS, WHOSE BEAT DECIDES NOTHING, which is Wasting
	// Sickness's shape: its count moves on a blow and this only puts the
	// reduction back after a floor change took it off. Issues #1820 and #41.
	const bool bHolyRepercussions = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::HolyRepercussionsKey));
	// AND LEECH SPORES, WHICH PLACES NOTHING HERE. Its clouds are placed by a
	// death; this beat only asks whether the player is touching one. Issues #1820
	// and #41.
	const bool bLeechSpores = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::LeechSporesKey));
	const bool bBloodAltar = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::BloodAltarKey));
	// AND NECROTIC GROUND, WHICH SPAWNS ACTORS AND MOVES A STAT, Singularity Wells'
	// shape. Issues #1820 and #41.
	const bool bNecroticGround = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::NecroticGroundKey));
	// AND RAVENOUS HOARD, WHICH CHANGES CREATURES AND NOT THE PLAYER. Issues #1820
	// and #41.
	const bool bRavenousHoard = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::RavenousHoardKey));
	// AND GRAVE TIDE, WHICH PUTS CREATURES ON THE FLOOR. Issues #1820 and #41.
	const bool bGraveTide = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::GraveTideKey));
	// AND VOLATILE EVOLUTION, WHICH TURNS A WOUNDED CREATURE INTO A RARER ONE. Issues
	// #1820 and #41.
	const bool bVolatileEvolution = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::VolatileEvolutionKey));
	// AND ROYAL GUARD, WHICH CALLS TWO MORE CREATURES TO A HURT ONE. Issues #1820, #41.
	const bool bRoyalGuard = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::RoyalGuardKey));
	// AND THE THIRD RULE THAT PLACES ACTORS ON THE FLOOR. Issues #1820, #41. It lays
	// ground the way Infernal Rain does, and unlike Infernal Rain it deals the damage
	// itself rather than leaving it to what it laid.
	const bool bJudgmentZones = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::JudgmentZonesKey));
	// AND MARCH OF PROGRESS, WHICH CHANGES CREATURES AND THE PLAYER BOTH. Issues #1820
	// and #41. Every creature's damage rises with the floor, and the player's armour
	// rises with the commanders they have killed.
	const bool bMarchOfProgress = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::MarchOfProgressKey));
	// AND COMMANDER'S AURA, WHICH CHANGES CREATURES AND NEVER THE PLAYER. Issues #1820
	// and #41. Every creature at Elite or above buffs its neighbours.
	const bool bCommandersAura = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::CommandersAuraKey));
	// AND ANTI-MAGIC ZONES, WHICH LAYS GROUND AND LOCKS THE PLAYER'S SPELLS ON IT. Issues
	// #1820 and #41.
	const bool bAntiMagicZones = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::AntiMagicZonesKey));
	// AND THE STARVATION CURSE, ON ANY FLOOR WHERE ITS STACKS ARE NOT WHAT IS ON THE
	// CHARACTER, not only on floors carrying the row: its stacks are the dungeon's and a
	// floor change takes them off. Issues #1820 and #41.
	// AND TRICK OR TREAT, ON ANY FLOOR WHILE A HASTE IS ON OR ITS CLOCK RUNS, as well as on
	// floors carrying the row. Issues #1820 and #41.
	// AND CHAOS TOUCHED, ON ANY FLOOR WHERE ITS STACKS ARE NOT WHAT IS ON THE CHARACTER.
	// Issues #1820 and #41.
	const bool bChaosTouched = ChaosTouchedStacks != ChaosTouchedApplied;
	// AND THE REAPER, UNTIL IT HAS COME, AND NEVER ON A HORDE WAVE. Issues #1820 and #41.
	const bool bTheReaper = FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::TheReaperKey))
		&& !FloorBrief.bOneWave && !bTheReaperRaised;
	// AND BLOOD BOND, UNTIL THIS FLOOR HAS BONDED. Issues #1820 and #41.
	const bool bBloodBond = FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::BloodBondKey))
		&& !bBloodBondFormed;
	// AND PLAGUE CONVERGENCE, NEVER ON A HORDE WAVE. Issues #1820 and #41.
	const bool bPlagueConvergence = FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::PlagueConvergenceKey))
		&& !FloorBrief.bOneWave;
	// AND DIVINE WRATH, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bDivineWrath = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::DivineWrathKey));
	// AND ECHOES OF THE PAST, UNTIL THIS FLOOR'S ECHOES HAVE GONE, HORDE WAVES INCLUDED.
	// Issues #1820 and #41.
	const bool bEchoes = FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::EchoesOfThePastKey))
		&& EchoesStage < 3;
	// AND PLAGUE HARBINGERS, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820
	// and #41.
	const bool bPlagueHarbingers = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::PlagueHarbingersKey));
	// AND WINGS OF THE HOST, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820
	// and #41.
	const bool bWingsOfTheHost = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::WingsOfTheHostKey));
	// AND ETERNAL CHORUS, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bEternalChorus = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::EternalChorusKey));
	// AND NECROTIC BLOOM, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bNecroticBloom = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::NecroticBloomKey));
	// AND GOLDEN SPIRES, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bGoldenSpires = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::GoldenSpiresKey));
	// AND PESTILENT EMPOWERMENT, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bPestilentEmpowerment = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::PestilentEmpowermentKey));
	// AND PORTAL UNLEASHING, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bPortalUnleashing = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::PortalUnleashingKey));
	// AND THE INFESTED HOARD, ON EVERY FLOOR CARRYING IT AND WHILE ANY STACK IS HELD. Issues #1820 and #41.
	const bool bInfestedHoard = FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::InfestedHoardKey))
		|| InfestedHoardStacks > 0;
	// AND ABYSSAL RIFTS, ON EVERY FLOOR CARRYING IT, AND ON ANY FLOOR WHERE ITS REWARD IS NOT WHAT IS ON THE CHARACTER.
	// Issues #1820 and #41.
	const bool bAbyssalRifts = FloorBrief.Modifiers.Contains(FName(UCataclysmDungeonModifierEffects::AbyssalRiftsKey))
		|| AbyssalRiftSuccesses != AbyssalRiftSuccessesApplied;
	// AND SWARM OF LOCUSTS, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bSwarmOfLocusts =
		FloorBrief.Modifiers.Contains(FName(UCataclysmDungeonModifierEffects::SwarmOfLocustsKey));
	// AND RAW SEWAGE, ON EVERY FLOOR CARRYING IT, AND ON ANY FLOOR WHILE A STACK OR THE TAG IS HELD: the stacks
	// are the dungeon's and burn on every floor. Issues #1820 and #41.
	const bool bRawSewage = FloorBrief.Modifiers.Contains(FName(UCataclysmDungeonModifierEffects::RawSewageKey))
		|| RawSewageStacks > 0 || bRawSewageTagged;
	// AND INFESTED VEINS, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bInfestedVeins = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::InfestedVeinsKey));
	// AND TRIAL OF ENDURANCE, ON EVERY FLOOR CARRYING IT; A HORDE FLOOR HAS NO TIMER. Issues #1820 and #41.
	const bool bTrialOfEndurance = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::TrialOfEnduranceKey));
	// AND VOID PARASITE, ON EVERY FLOOR CARRYING IT, AND ON ANY FLOOR WHERE ITS STACKS ARE NOT WHAT IS ON
	// THE CHARACTER, as Chaos Touched is stepped. Issues #1820 and #41.
	const bool bVoidParasite = FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::VoidParasiteKey))
		|| VoidParasiteStacks != VoidParasiteStacksApplied;
	// AND OBSIDIAN SARCOPHAGI, ON EVERY FLOOR CARRYING IT, HORDE WAVES INCLUDED. Issues #1820 and #41.
	const bool bObsidianSarcophagi = FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::ObsidianSarcophagiKey));
	const bool bTrickOrTreat = FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::TrickOrTreatKey))
		|| TrickOrTreatHasteApplied > 0.0f || TrickOrTreatHasteUntilSeconds >= 0.0f;
	const bool bStarvationCurse =
		StarvationCurseMovementStacks != StarvationCurseMovementApplied
		|| StarvationCurseHealthStacks != StarvationCurseHealthApplied;
	if (!bForcedMarch && !bNihilsEmbrace && !bDeathsEmbrace && !bInfernalRain
		&& !bSingularityWells && !bWitheredGround && !bMortalDecay && !bSufferingAura
		&& !bWastingSickness && !bGraspingTentacles && !bEdictOfSilence && !bDirgeResonance
		&& !bArtilleryStrike && !bHallowedGroundfall && !bFungalOvergrowth
		&& !bHolyRepercussions && !bLeechSpores && !bBloodAltar && !bNecroticGround
		&& !bRavenousHoard && !bGraveTide && !bVolatileEvolution && !bRoyalGuard
		&& !bJudgmentZones && !bMarchOfProgress && !bCommandersAura
		&& !bAntiMagicZones && !bStarvationCurse && !bTrickOrTreat && !bChaosTouched
		&& !bTheReaper && !bBloodBond && !bPlagueConvergence && !bDivineWrath
		&& !bEchoes && !bPlagueHarbingers
		&& !bWingsOfTheHost && !bEternalChorus && !bNecroticBloom && !bGoldenSpires && !bPortalUnleashing
		&& !bInfestedHoard
		&& !bAbyssalRifts
		&& !bSwarmOfLocusts
		&& !bRawSewage
		&& !bPestilentEmpowerment && !bInfestedVeins && !bTrialOfEndurance && !bVoidParasite
		&& !bObsidianSarcophagi)
	{
		return;
	}

	// THE SAME ROUTE `ApplyFloorRulesToPlayer` TAKES to the player's character, so
	// the two cannot disagree about whose floor rules these are.
	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	UCataclysmAbilitySystemComponent* AbilitySystem = Player
		? Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent())
		: nullptr;
	if (!Player || !AbilitySystem)
	{
		return;
	}

	if (bForcedMarch)
	{
		StepForcedMarch(Player, AbilitySystem);
	}
	if (bNihilsEmbrace)
	{
		StepNihilsEmbrace(Player, AbilitySystem);
	}

	// A FLOOR CARRYING BOTH OF THE STAT RULES REFRESHES TWICE ON THE RARE BEAT
	// WHERE BOTH MOVE, and the result is right either way: each sets its own
	// field before asking for the apply, and the apply reads every field. The
	// second refresh is the one that stands and it carries both changes. Issue
	// #41, slice 5.
	if (bDeathsEmbrace)
	{
		StepDeathsEmbrace(Player, AbilitySystem);
	}

	// LAST, AND IT NEEDS NO ABILITY SYSTEM. The three above put stat modifiers on
	// the character and share one applier; this one spawns an actor. It is called
	// after them so that a floor carrying both kinds does its stat work in one
	// pass before anything else happens on the beat.
	if (bInfernalRain)
	{
		StepInfernalRain(Player, AbilitySystem);
	}

	// AND SINGULARITY WELLS LAST, for the same reason Infernal Rain is late: it
	// spawns an actor. It differs from every rule above in doing two things on one
	// beat -- it places wells and it sets a stat -- so it calls the shared applier
	// itself when the slow changes, rather than only writing a field.
	if (bSingularityWells)
	{
		StepSingularityWells(Player, AbilitySystem);
	}

	// AND JUDGMENT ZONES, LAST OF THE THREE THAT PLACE ACTORS. It is here for the reason
	// Infernal Rain is late -- a floor carrying both does its stat work in one pass first
	// -- and after Singularity Wells because it does more on a beat than either: it lays
	// ground, works out what the player standing in it owes, and takes it.
	if (bJudgmentZones)
	{
		StepJudgmentZones(Player, AbilitySystem);
	}

	// AND ANTI-MAGIC ZONES BESIDE IT, the fourth rule that places actors, for the same
	// reason: a floor carrying a stat rule above does its stat work first. It is after
	// Judgment Zones only because it was written after it; the two share no field.
	if (bAntiMagicZones)
	{
		StepAntiMagicZones(Player, AbilitySystem);
	}

	// AND WITHERED GROUND, WHICH ONLY READS. It spawns nothing on the beat, so
	// its position in this order does not matter the way the two above it do.
	// It is last because it was written last. Issue #41.
	if (bWitheredGround)
	{
		StepWitheredGround(Player, AbilitySystem);
	}

	// AND FUNGAL OVERGROWTH, WHICH ONLY READS, so its position in this order is
	// free the way Withered Ground's is: it places nothing on the beat and shares
	// no field with any rule here. Issues #1820 and #41.
	if (bFungalOvergrowth)
	{
		StepFungalOvergrowth(Player, AbilitySystem);
	}

	// AND HOLY REPERCUSSIONS, WHICH IS FREE WHERE NOTHING HAS CHANGED. Issues
	// #1820 and #41. It compares two integers and returns on almost every beat,
	// so its position in this order is free the way Wasting Sickness's is.
	if (bHolyRepercussions)
	{
		StepHolyRepercussions(Player, AbilitySystem);
	}

	// AND LEECH SPORES. Its position is free: it moves no floor-effects field and
	// asks for no refresh, so nothing above can undo it and it undoes nothing.
	if (bLeechSpores)
	{
		StepLeechSpores(Player, AbilitySystem);
	}
	if (bBloodAltar)
	{
		StepBloodAltar(Player, AbilitySystem);
	}

	// AND MORTAL DECAY, WHICH ASKS FOR NO STAT REFRESH AT ALL. Issues #1786 and
	// #41. It takes health directly, so its position in this order is free the
	// way Withered Ground's is: it neither reads nor writes any field the rules
	// above share, and nothing it does can be undone by the applier they call.
	if (bMortalDecay)
	{
		StepMortalDecay(Player, AbilitySystem);
	}

	// AND SUFFERING AURA, FREE IN THIS ORDER FOR MORTAL DECAY'S REASON. Issues #1820 and
	// #41. When a floor carries both, both take their share, and the two add.
	if (bSufferingAura)
	{
		StepSufferingAura(Player, AbilitySystem);
	}

	// AND WASTING SICKNESS LAST, WHICH IS FREE WHERE NOTHING HAS CHANGED. Issues
	// #1786 and #41. It compares two integers and returns on almost every beat.
	if (bWastingSickness)
	{
		StepWastingSickness(Player, AbilitySystem);
	}

	// AND THE STARVATION CURSE, THE SAME SHAPE AND AS CHEAP. Issues #1820 and #41.
	if (bStarvationCurse)
	{
		StepStarvationCurse(Player, AbilitySystem);
	}

	// AND TRICK OR TREAT'S HASTE, ON AND OFF BY ITS CLOCK. Issues #1820 and #41.
	if (bTrickOrTreat)
	{
		StepTrickOrTreat(Player, AbilitySystem);
	}

	// AND CHAOS TOUCHED, THE SAME SHAPE AS THE STARVATION CURSE. Issues #1820 and #41.
	if (bChaosTouched)
	{
		StepChaosTouched(Player, AbilitySystem);
	}

	// AND THE REAPER, WHICH SPAWNS A CREATURE AND TOUCHES NO STAT. Issues #1820 and #41.
	if (bTheReaper)
	{
		StepTheReaper();
	}

	// AND BLOOD BOND, WHICH READS WHERE THE CREATURES STAND. Issues #1820 and #41.
	if (bBloodBond)
	{
		StepBloodBond(Player);
	}

	// AND PLAGUE CONVERGENCE, WHICH SPAWNS CREATURES AND BURNS THE PLAYER. Issues #1820, #41.
	if (bPlagueConvergence)
	{
		StepPlagueConvergence(Player, AbilitySystem);
	}

	// AND DIVINE WRATH, WHICH PLACES A ZONE AND KILLS CREATURES. Issues #1820 and #41.
	if (bDivineWrath)
	{
		StepDivineWrath(Player, AbilitySystem);
	}

	// AND ECHOES OF THE PAST, WHICH SPAWNS CREATURES AND DRIVES THEIR ONE ATTACK. Issues #1820
	// and #41.
	if (bEchoes)
	{
		StepEchoesOfThePast(Player);
	}

	// AND PLAGUE HARBINGERS, WHICH PLACES ZONES. Issues #1820 and #41.
	if (bPlagueHarbingers)
	{
		StepPlagueHarbingers(Player, AbilitySystem);
	}

	// AND WINGS OF THE HOST, WHICH PLACES ZONES. Issues #1820 and #41.
	if (bWingsOfTheHost)
	{
		StepWingsOfTheHost(Player);
	}

	// AND ETERNAL CHORUS, WHICH PLACES ZONES AND CHANGES THE PLAYER'S STATS. Issues #1820 and #41.
	if (bEternalChorus)
	{
		StepEternalChorus(Player, AbilitySystem);
	}

	// AND NECROTIC BLOOM, WHICH SPAWNS CREATURES. Issues #1820 and #41.
	if (bNecroticBloom)
	{
		StepNecroticBloom();
	}

	// AND GOLDEN SPIRES, WHICH PLACES ZONES AND CHANGES CREATURES' DAMAGE. Issues #1820 and #41.
	if (bGoldenSpires)
	{
		StepGoldenSpires(Player);
	}

	// AND PESTILENT EMPOWERMENT, WHICH CHANGES CREATURES' DAMAGE. Issues #1820 and #41.
	if (bPestilentEmpowerment)
	{
		StepPestilentEmpowerment(Player);
	}

	// AND PORTAL UNLEASHING, WHICH DRAWS ZONES AND SPAWNS CREATURES. Issues #1820 and #41.
	if (bPortalUnleashing)
	{
		StepPortalUnleashing();
	}

	// AND THE INFESTED HOARD, WHICH DRAINS THE PLAYER WHILE A STACK IS HELD. Issues #1820 and #41.
	if (bInfestedHoard)
	{
		StepInfestedHoard(Player, AbilitySystem);
	}

	// AND ABYSSAL RIFTS, WHICH OPENS A RIFT, SENDS WAVES AND PAYS THE PLAYER. Issues #1820 and #41.
	if (bAbyssalRifts)
	{
		StepAbyssalRifts(Player, AbilitySystem);
	}

	// AND SWARM OF LOCUSTS, WHICH SENDS A ZONE ACROSS THE FLOOR AND HURTS THE PLAYER IT COVERS. Issues #1820 and #41.
	if (bSwarmOfLocusts)
	{
		StepSwarmOfLocusts(Player, AbilitySystem);
	}

	// AND RAW SEWAGE, WHICH DRAWS RIVERS, ADDS STACKS, BURNS AND TAGS THE PLAYER. Issues #1820 and #41.
	if (bRawSewage)
	{
		StepRawSewage(Player, AbilitySystem);
	}

	// AND INFESTED VEINS, WHICH PLACES ZONES, SPAWNS CREATURES AND HURTS THE PLAYER. Issues #1820 and #41.
	if (bInfestedVeins)
	{
		StepInfestedVeins(Player, AbilitySystem);
	}

	// AND TRIAL OF ENDURANCE, WHICH CHANGES CREATURES' DAMAGE AND RESISTANCE ONCE RUN OUT. Issues #1820 and #41.
	if (bTrialOfEndurance)
	{
		StepTrialOfEndurance(Player);
	}

	// AND VOID PARASITE, WHICH DRAWS A ZONE, REMOVES CREATURES AND MOVES THE PLAYER'S STATS. Issues #1820
	// and #41.
	if (bVoidParasite)
	{
		StepVoidParasite(Player, AbilitySystem);
	}

	// AND OBSIDIAN SARCOPHAGI, WHICH CHANGES CREATURES' DAMAGE AND RESISTANCE NEAR ITS COFFINS. After the trial,
	// so a creature first found by both on one beat is written once for each. Issues #1820 and #41.
	if (bObsidianSarcophagi)
	{
		StepObsidianSarcophagi(Player);
	}

	// AND GRASPING TENTACLES, WHICH SPAWNS AN ACTOR, so it is late for the reason
	// Infernal Rain and Singularity Wells are late: a floor carrying both kinds
	// does its stat work in one pass before anything else happens on the beat.
	// Issues #1786 and #41.
	if (bGraspingTentacles)
	{
		StepGraspingTentacles(Player, AbilitySystem);
	}

	// AND THE EDICT OF SILENCE, WHOSE POSITION HERE IS FREE. Issues #1786 and
	// #41. It spawns nothing and shares no field with any rule above it, so
	// nothing it does can be undone by them and nothing they do can be undone by
	// it.
	if (bEdictOfSilence)
	{
		StepEdictOfSilence(Player, AbilitySystem);
	}

	// AND DIRGE RESONANCE, WHICH TOUCHES NOTHING THE PLAYER'S RULES SHARE, so its place in
	// this order is free. Issues #1820 and #41.
	if (bDirgeResonance)
	{
		StepDirgeResonance();
	}

	// AND THE ARTILLERY STRIKE, WHICH SPAWNS AN ACTOR, so it is late for the
	// reason the three above it are: a floor carrying both kinds does its stat
	// work in one pass before anything else happens on the beat. Issues #1820
	// and #41.
	if (bArtilleryStrike)
	{
		StepArtilleryStrike(Player, AbilitySystem);
	}

	// AND HALLOWED GROUNDFALL, WHICH SPAWNS ACTORS, so it is late for the reason
	// the four above it are. Issues #1820 and #41.
	if (bHallowedGroundfall)
	{
		StepHallowedGroundfall(Player, AbilitySystem);
	}

	// AND NECROTIC GROUND, WHICH SPAWNS ACTORS, so it is late for the reason every rule
	// above it that spawns actors is. It also moves a stat, and calls the shared
	// applier itself when it does, the way Singularity Wells does. Issues #1820 and
	// #41.
	if (bNecroticGround)
	{
		StepNecroticGround(Player, AbilitySystem);
	}

	// AND RAVENOUS HOARD, WHOSE POSITION IS FREE. Issues #1820 and #41. It writes no
	// field any rule above shares and asks for no refresh of the player's stats: it
	// changes creatures' attack damage and nothing else.
	if (bRavenousHoard)
	{
		StepRavenousHoard(Player);
	}

	// AND MARCH OF PROGRESS BESIDE IT, WHOSE POSITION IS FREE FOR THE SAME REASON.
	// Issues #1820 and #41. It writes a creature multiplier no other rule writes, and
	// the only player field it touches is its own. It reads no creature's health and
	// places nothing, so no rule above or below it is disturbed by where it sits.
	if (bMarchOfProgress)
	{
		StepMarchOfProgress(Player, AbilitySystem);
	}

	// AND COMMANDER'S AURA BESIDE IT, WHOSE POSITION IS FREE FOR THE SAME REASON. Issues
	// #1820 and #41. It grants a status effect to creatures and writes no field any rule
	// above it shares. It reads a creature's rung and its neighbours' positions, and no
	// rule on this beat writes either.
	if (bCommandersAura)
	{
		StepCommandersAura(Player);
	}

	// AND VOLATILE EVOLUTION BESIDE IT, WHOSE POSITION IS FREE FOR THE SAME REASON.
	// Issues #1820 and #41. It reads a creature's health and writes that creature's
	// rarity, health and energy shield; no rule above it reads any of those on the beat.
	// Before Grave Tide below, so a creature placed by a wave is offered its first
	// mutation on the next beat rather than on the beat it arrived, which is the order
	// every rule here already follows.
	if (bVolatileEvolution)
	{
		StepVolatileEvolution(Player);
	}

	// AND GRAVE TIDE LAST, BECAUSE IT SPAWNS CREATURES. Issues #1820 and #41. A creature
	// placed on this beat is counted by every rule above it on the next one, which is
	// the order Necrotic Ground's patches already follow.
	if (bGraveTide)
	{
		StepGraveTide();
	}

	// AND ROYAL GUARD AFTER IT, THE SECOND RULE THAT SPAWNS CREATURES. Issues #1820 and
	// #41. A guard that arrives on this beat is counted by every rule above it on the
	// next one, which is the order Grave Tide's waves already follow. It reads a
	// creature's health and rung and writes neither, so no rule above it is disturbed.
	if (bRoyalGuard)
	{
		StepRoyalGuard(Player);
	}
}

void ACataclysmDungeonGameMode::StepForcedMarch(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	// THE SECONDS SINCE THE PLAYER LAST MOVED ARE THE WHOLE STATE. Moving puts
	// them back to nothing, which clears every stack without anything being
	// stored. A character that cannot be asked reads a negative wait and takes
	// nothing.
	const int32 Stacks = UCataclysmDungeonModifierEffects::ForcedMarchStacksAfter(
		AbilitySystem->SecondsSinceMoved());
	if (Stacks <= 0)
	{
		return;
	}

	// A SHARE OF MAXIMUM HEALTH, FOR THIS BEAT'S LENGTH. The row's figure is per
	// second and this runs four times a second, so each beat takes a quarter of
	// it.
	const float Maximum = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	const float Share =
		UCataclysmDungeonModifierEffects::ForcedMarchSharePerSecond(Stacks);
	const float Amount = Maximum * Share / 100.0f * SecondsBetweenWaveChecks;
	if (Amount <= 0.0f)
	{
		return;
	}

	// NOT A HIT, WHICH IS THE WHOLE POINT. The damage comes from the floor rather
	// than from an attacker, so no evasion roll, no block, no armour, no
	// resistance, no critical strike and no ailment touch it. The player is its
	// own instigator because nothing else dealt it.
	UCataclysmSkillEffects::ReduceHealthDirectly(Player, Player, Amount);
}

void ACataclysmDungeonGameMode::StepEdictOfSilence(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	const UWorld* World = GetWorld();
	if (!World || !Player || !AbilitySystem)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	// THE CLOCK ADVANCES WHETHER OR NOT A SILENCE IS RUNNING, so "every 90
	// seconds" is a cycle of ninety with fifteen of it silent rather than fifteen
	// separated by ninety. The header's constants carry that reading.
	EdictOfSilenceSecondsSinceLast += SecondsBetweenWaveChecks;

	// A SILENCE ALREADY RUNNING IS NOT RESTARTED. The cadence cannot come due
	// inside one while the silence is shorter than the gap, which a static
	// assertion requires -- but asking first means a change to either figure
	// shortens the cycle rather than silently making it permanent.
	if (EdictOfSilencedUntilSeconds <= Now
		&& Effects::EdictOfSilenceIsDue(EdictOfSilenceSecondsSinceLast))
	{
		EdictOfSilencedUntilSeconds = Now + Effects::EdictOfSilenceLastsSeconds;
		EdictOfSilenceSecondsSinceLast = 0.0f;
	}

	// ONLY WHEN SOMETHING CHANGED, which is the guard every beat-driven rule here
	// keeps: the apply rewrites the character's whole standing stat line, and
	// this changes twice in ninety seconds rather than four times a second.
	const float Wanted =
		Effects::SkillsLockedWhile(EdictOfSilencedUntilSeconds > Now);
	if (!FMath::IsNearlyEqual(Wanted, EdictOfSilenceLockApplied))
	{
		EdictOfSilenceLockApplied = Wanted;
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}
}

void ACataclysmDungeonGameMode::StepDirgeResonance()
{
	using Effects = UCataclysmDungeonModifierEffects;

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	DirgeResonanceSecondsSinceLast += SecondsBetweenWaveChecks;
	if (Effects::DirgeResonanceIsDue(DirgeResonanceSecondsSinceLast))
	{
		DirgeResonanceSecondsSinceLast = 0.0f;
		DirgeResonanceHastedUntilSeconds = Now + Effects::DirgeResonanceHasteSeconds;

		// THE TAG IS LOOKED UP ONCE, and a missing one says so rather than hasting
		// nothing in silence. The route Commander's Aura takes.
		const FGameplayTag Haste = UCataclysmSkillShapes::StatusTagFor(TEXT("Commander"));
		if (!Haste.IsValid())
		{
			UE_LOG(LogCataclysm, Warning,
				   TEXT("Dirge Resonance cannot haste: there is no Status.Buff.Commander tag."));
		}
		else
		{
			// EVERY LIVING CREATURE, MARKED ONES INCLUDED: the row says "all enemies".
			// The creature is its own instigator, as Hallowed Groundfall's grant has it:
			// nothing on the floor is doing the hasting.
			int32 Hasted = 0;
			for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : FloorEnemies)
			{
				if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy))
				{
					UCataclysmSkillEffects::ApplyTagForDuration(
						Enemy, Enemy, Haste, Effects::DirgeResonanceHasteSeconds);
					++Hasted;
				}
			}
			UE_LOG(LogCataclysm, Log,
				   TEXT("Dirge Resonance: the crescendo hastes %d creature(s) for %.0f s"),
				   Hasted, Effects::DirgeResonanceHasteSeconds);
		}
	}

	// THE PANEL SHOWS WHOLE SECONDS, so it is refreshed when that second changes.
	const bool bHasted = DirgeResonanceHastedUntilSeconds > Now;
	const int32 Shown = FMath::CeilToInt(bHasted
		? DirgeResonanceHastedUntilSeconds - Now
		: Effects::DirgeResonanceEverySeconds - DirgeResonanceSecondsSinceLast);
	const int32 Signed = bHasted ? -Shown : Shown;
	if (Signed != DirgeResonanceShownSeconds)
	{
		DirgeResonanceShownSeconds = Signed;
		RefreshFloorModifierPanel();
	}
}

bool ACataclysmDungeonGameMode::DropsAreChaotic() const
{
	return FloorBrief.Modifiers.Contains(
		FName(UCataclysmDungeonModifierEffects::ChaoticLootKey));
}

void ACataclysmDungeonGameMode::ChooseTheScarceSlot(
	UCataclysmEquipmentComponent* Equipment) const
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!Equipment)
	{
		return;
	}
	if (!FloorBrief.Modifiers.Contains(FName(Effects::ScarcityKey)))
	{
		Equipment->SetDisabledSlot(ECataclysmGearSlot::Count);
		return;
	}

	// THE NON-WEAPON SLOTS THAT HOLD AN ITEM, in the order the slots are listed, so the
	// seeded pick lands on the same slot for the same gear.
	TArray<ECataclysmGearSlot> Worn;
	for (const ECataclysmGearSlot Slot : UCataclysmGearSlots::AllSlots())
	{
		if (!UCataclysmGearSlots::IsWeaponSlot(Slot) && Equipment->EquippedAt(Slot))
		{
			Worn.Add(Slot);
		}
	}

	const int32 Pick = Effects::ScarcityPick(Worn.Num(), ChooseSeed(), FloorBrief.FloorNumber);
	Equipment->SetDisabledSlot(Worn.IsValidIndex(Pick) ? Worn[Pick]
													   : ECataclysmGearSlot::Count);
}

void ACataclysmDungeonGameMode::StepArtilleryStrike(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	// A CIRCLE ON THE GROUND IS COUNTED DOWN AND NOTHING ELSE HAPPENS. The
	// cadence is not advanced while one is in the air, so the thirty seconds
	// between strikes is thirty seconds between LANDINGS plus the warning, not
	// thirty seconds between circles appearing. Either reading is defensible and
	// the row does not say; this one is recorded in docs/DECISIONS.md.
	if (ACataclysmGroundZone* Circle = ArtilleryStrikeCircle.Get())
	{
		ArtilleryStrikeWarningSoFar += SecondsBetweenWaveChecks;
		if (!Effects::ArtilleryStrikeHasLanded(ArtilleryStrikeWarningSoFar))
		{
			return;
		}

		// WHAT IS STANDING IN IT AT THE MOMENT IT LANDS, asked of the same
		// search `ACataclysmGroundZone` uses for a zone that burns everyone --
		// `FindEveryoneInLine` with the start and the end in one place, which is
		// how every point-shaped zone in this file is already spawned. Asking
		// the same question the circle would ask is what stops the drawn circle
		// and the hit list disagreeing.
		//
		// EVERYONE AND NOT THE OTHER SIDE, which is the row's own sentence.
		// THE FLOOR'S HAZARD SOURCE IS WHAT DEALS IT, NOT THE CIRCLE, and that is
		// not a preference. `UCataclysmSkillEffects::ApplyDirectDamage` returns
		// false when the INSTIGATOR has no ability system, and a ground zone has
		// none -- naming the circle made every strike land for nothing. The zone
		// itself names the same source actor when it deals its own damage, so
		// this is the shape that already works rather than a new one.
		AActor* Firing = ACataclysmFloorHazardSource::ForFloor(World);
		if (!Firing)
		{
			return;
		}

		const FVector Where = Circle->GetActorLocation();
		const TArray<AActor*> Inside = UCataclysmTargeting::FindEveryoneInLine(
			World, Firing, Where, Where, Effects::ArtilleryStrikeRadiusCm);

		// AN AREA HIT BUT NOT A DAMAGE-OVER-TIME ONE, and the second half is a
		// judgement this rule makes differently from the zone beside it.
		// `ACataclysmGroundZone` marks its own sweeps as both, because a patch
		// of fire catches whoever stands in it and keeps burning. A shell is one
		// blow: it cannot be evaded, which is what `bIsArea` says, and an energy
		// shield should absorb it exactly as it absorbs any other blow, which is
		// what leaving `bIsDamageOverTime` false says.
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		// AND TYPED BY THE ROW, since the source carries no type. Issue #1924.
		Delivery.DamageType = DungeonGameModeTypeOfRow(Effects::ArtilleryStrikeKey);

		for (AActor* Target : Inside)
		{
			const UAbilitySystemComponent* Abilities =
				UCataclysmTargeting::AbilitySystemOf(Target);
			if (!Abilities)
			{
				continue;
			}

			const float Damage = Effects::ArtilleryStrikeDamage(
				Abilities->GetNumericAttribute(
					UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
			if (Damage <= 0.0f)
			{
				continue;
			}

			// THE CIRCLE IS WHAT DEALT IT, which is the same actor the zone
			// would have named had it dealt the damage itself. WHATEVER THAT
			// MEANS FOR KILL CREDIT AND DROPS IS LEFT EXACTLY AS IT IS for every
			// other floor hazard; this rule adds no rule about it.
			UCataclysmSkillEffects::ApplyDirectDamage(Firing, Target, Damage,
													  Delivery);
		}

		Circle->Destroy();
		ArtilleryStrikeCircle = nullptr;
		ArtilleryStrikeWarningSoFar = 0.0f;
		return;
	}

	ArtilleryStrikeSecondsSinceLast += SecondsBetweenWaveChecks;
	if (!Effects::ArtilleryStrikeIsDue(ArtilleryStrikeSecondsSinceLast,
									   /*bOneInTheAir=*/false))
	{
		return;
	}

	// AWAY FROM THE PLAYER, for the reason Infernal Rain gives: a hazard that
	// only ever appears on top of somebody is not a thing to walk out of, and
	// the warning this rule spends three seconds on would buy nothing.
	const FVector Centre = Player->GetActorLocation();
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Away = FMath::FRandRange(0.0f, Effects::ArtilleryStrikeLandsWithinCm);
	const FVector Where(Centre.X + Away * FMath::Cos(Angle),
						Centre.Y + Away * FMath::Sin(Angle),
						Centre.Z);

	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}

	// A DAMAGE OF ZERO, WHICH IS THE WHOLE POINT OF THE CIRCLE. It marks a place
	// and hurts nobody; the shell above is what hurts. `Void_Grasping_Tentacles`
	// spawns a zone the same way and for the same reason -- it only has to be
	// somewhere, and the rule reads where it is on the beat.
	ACataclysmGroundZone* Circle = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Where, Where, Effects::ArtilleryStrikeRadiusCm, 0.0f);
	if (!Circle)
	{
		// THE CLOCK IS NOT RESET ON A FAILED SPAWN, the choice Infernal Rain
		// records: the next beat tries again rather than waiting another full
		// cadence for a circle that never appeared.
		return;
	}

	ArtilleryStrikeCircle = Circle;
	ArtilleryStrikeWarningSoFar = 0.0f;
	ArtilleryStrikeSecondsSinceLast = 0.0f;
}

void ACataclysmDungeonGameMode::StepHallowedGroundfall(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	// WHAT IS STILL BURNING, ASKED RATHER THAN REMEMBERED. A crater is destroyed
	// with the rest of the floor's contents and expires on its own clock, so a
	// weak pointer going invalid IS its crater being gone.
	HallowedGroundfallCratersBurning.RemoveAll(
		[](const TWeakObjectPtr<ACataclysmGroundZone>& Crater)
		{
			return !Crater.IsValid();
		});

	// THE EMPOWERMENT FIRST, BECAUSE IT IS ABOUT THE CRATERS THAT ARE ALREADY
	// THERE. Doing it after the bombardment would empower whatever happened to be
	// standing where a crater had just that instant appeared, which is not what
	// the row describes and would give a creature the buff a beat early.
	//
	// RE-APPLIED EVERY BEAT AND NOT ONCE, which is the Abyssal Aura's shape and
	// its stated reason: the effect is a single stack, so a second application
	// refreshes the one already there rather than adding another. A creature that
	// stays keeps it; one that walks out loses it when its second runs out.
	if (!HallowedGroundfallCratersBurning.IsEmpty())
	{
		const FGameplayTag Empowered =
			UCataclysmSkillShapes::StatusTagFor(TEXT("Commander"));
		if (Empowered.IsValid())
		{
			for (const TWeakObjectPtr<ACataclysmGroundZone>& Crater :
					HallowedGroundfallCratersBurning)
			{
				const FVector Where = Crater->GetActorLocation();

				// THE PLAYER'S ENEMIES, WHICH IS EVERY CREATURE AND NEVER THE
				// PLAYER. Asking this way is what keeps the row's two halves
				// apart without naming anybody: the crater burns the hazard
				// source's enemies, which is the player, and this empowers the
				// player's enemies, which is everything else.
				const TArray<AActor*> Standing =
					UCataclysmTargeting::FindEnemiesInLine(
						World, Player, Where, Where,
						Effects::HallowedGroundfallCraterRadiusCm);

				for (AActor* Creature : Standing)
				{
					UCataclysmSkillEffects::ApplyTagForDuration(
						Creature, Creature, Empowered,
						Effects::HallowedGroundfallEmpowerSeconds);
				}
			}
		}
	}

	HallowedGroundfallSecondsSinceLast += SecondsBetweenWaveChecks;
	if (!Effects::HallowedGroundfallIsDue(HallowedGroundfallSecondsSinceLast))
	{
		return;
	}

	const float PerSecond = Effects::HallowedGroundfallBurnPerSecond(
		AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
	if (PerSecond <= 0.0f)
	{
		return;
	}

	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}

	const FVector Centre = Player->GetActorLocation();
	for (int32 Which = 0; Which < Effects::HallowedGroundfallCraters; ++Which)
	{
		// AWAY FROM THE PLAYER, for the reason Infernal Rain gives: ground that
		// only ever appears underfoot is not ground to walk out of.
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const float Away = FMath::FRandRange(
			Effects::HallowedGroundfallCraterRadiusCm + 1.0f,
			Effects::HallowedGroundfallFallsWithinCm);
		const FVector Where(Centre.X + Away * FMath::Cos(Angle),
							Centre.Y + Away * FMath::Sin(Angle),
							Centre.Z);

		// A REAL DAMAGE, UNLIKE THE ARTILLERY STRIKE'S CIRCLE. That rule's circle
		// is a warning and hurts nobody; these craters burn from the moment they
		// land, which is what "leaving consecrated craters that burn players"
		// says. The zone does that itself and needs nothing from the beat.
		//
		// TYPED BY THE ROW, ON THE CRATER ITSELF. Until issue #1924 this rule typed
		// nothing, so its burn met no resistance unless another rule on the floor
		// had written the shared source's type.
		ACataclysmGroundZone* Crater = ACataclysmGroundZone::Spawn(
			Source, Where, Effects::HallowedGroundfallCraterRadiusCm,
			Effects::HallowedGroundfallCraterSeconds, PerSecond,
			DungeonGameModeTypeOfRow(Effects::HallowedGroundfallKey));
		if (Crater)
		{
			// CRATERS THAT OVERLAP BURN ONCE A SECOND BETWEEN THEM. Issue #2074.
			Crater->BurnsOnceASecondAs = FName(Effects::HallowedGroundfallKey);
			HallowedGroundfallCratersBurning.Add(Crater);
		}
	}

	// THE CLOCK IS RESET WHETHER OR NOT EVERY CRATER APPEARED, which differs from
	// Infernal Rain deliberately. That rule places ONE thing and leaves its clock
	// alone on a failure so the next beat tries again; a bombardment that managed
	// two craters out of three has happened, and re-running it on the next beat
	// would drop three more a quarter of a second later.
	HallowedGroundfallSecondsSinceLast = 0.0f;
}

void ACataclysmDungeonGameMode::StepGraspingTentacles(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !Player || !AbilitySystem)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	// WHAT IS STILL THERE, ASKED RATHER THAN REMEMBERED. A tentacle is destroyed
	// with the rest of the floor's contents, so a weak pointer going invalid IS
	// that. The cooldown goes with the entry, which is why the two travel
	// together in one struct rather than in parallel arrays that could slip.
	GraspingTentacles.RemoveAll(
		[](const FCataclysmGraspingTentacle& Tentacle)
		{
			return !Tentacle.Zone.IsValid();
		});

	// THE GRAB ALREADY IN FORCE IS RESOLVED FIRST, BEFORE LOOKING FOR A NEW ONE.
	// A player still held is not rolled for again: the row describes being
	// grabbed, not being grabbed harder, and rolling would silently extend the
	// hold past the figure the constant states.
	bool bGrabbed = GraspedUntilSeconds > Now;

	if (!bGrabbed)
	{
		// ONE ROLL PER TENTACLE THAT COVERS THE PLAYER, so standing where two
		// reaches overlap is twice as dangerous. `Covers` is the same test the
		// zone's own sweep makes, so what grabs a character and what the tentacle
		// is drawn as cannot disagree about where it is.
		const FVector Feet = Player->GetActorLocation();
		for (FCataclysmGraspingTentacle& Tentacle : GraspingTentacles)
		{
			if (!Tentacle.Zone.IsValid() || !Tentacle.Zone->Covers(Feet))
			{
				continue;
			}

			// ITS OWN COOLDOWN, NOT THE FLOOR'S. A player held by one tentacle is
			// not safe from the others, and one that has just let go cannot take
			// hold again at once.
			if (Tentacle.MayGrabAgainAtSeconds > Now)
			{
				continue;
			}

			if (DungeonGameModeGraspingTentaclesRoll()
				>= Effects::GraspingTentaclesGrabChancePercentPerBeat)
			{
				continue;
			}

			// THE GRAB AND THE COOLDOWN ARE BOTH SET FROM NOW, and the cooldown
			// starts when the grab ENDS rather than when it begins -- otherwise a
			// cooldown shorter than the grab would let the same tentacle take
			// hold again before it had let go. A static assertion keeps the two
			// figures in that order as well.
			GraspedUntilSeconds = Now + Effects::GraspingTentaclesGrabSeconds;
			Tentacle.MayGrabAgainAtSeconds =
				GraspedUntilSeconds + Effects::GraspingTentaclesGrabCooldownSeconds;
			bGrabbed = true;
			break;
		}
	}

	// ONLY WHEN SOMETHING CHANGED, which is the guard every beat-driven rule here
	// keeps: the apply rewrites the character's whole standing stat line, and a
	// grab begins and ends far less often than four times a second.
	const float Wanted = Effects::GraspMovementLessPercentWhile(bGrabbed);
	if (!FMath::IsNearlyEqual(Wanted, GraspMovementLessApplied))
	{
		GraspMovementLessApplied = Wanted;
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}

	// AND ONLY THEN IS ANOTHER PLACED. Placing before the roll would let a
	// tentacle appear and grab on the same beat, which is not "careful of getting
	// too close": the player had no chance to be careful of something that was
	// not there.
	GraspingTentaclesSecondsSinceLast += SecondsBetweenWaveChecks;
	if (!Effects::GraspingTentacleIsDue(GraspingTentaclesSecondsSinceLast,
										GraspingTentacles.Num()))
	{
		return;
	}

	// THE TYPE COMES OUT OF THE ROW, the way Infernal Rain and Singularity Wells
	// read theirs, so a row retyped in the design workbook retypes its tentacles
	// with no code change. An unreadable table places nothing rather than
	// guessing at a type.
	const FCataclysmDungeonModifierRow* Row = UCataclysmDungeonModifierTable::FindRow(
		UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
		FName(Effects::GraspingTentaclesKey));
	if (!Row)
	{
		return;
	}

	// PAST THE REACH RATHER THAN AT IT, for the reason `StepSingularityWells`
	// gives: `UCataclysmTargeting::IsInLine` decides who is inside with `<=`, so
	// one centred at exactly its reach covers a player standing still -- and a
	// tentacle that could grab on the beat it appeared is the case the ordering
	// above exists to prevent.
	const FVector Centre = Player->GetActorLocation();
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Away = FMath::FRandRange(Effects::GraspingTentaclesReachCm + 1.0f,
										 Effects::GraspingTentaclesAppearWithinCm);
	const FVector Where(Centre.X + Away * FMath::Cos(Angle),
						Centre.Y + Away * FMath::Sin(Angle),
						Centre.Z);

	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}
	// NO DAMAGE PER TICK. A tentacle grabs and does not burn: the row says
	// "restricting their movement" and says nothing about harm. Since issue #1701
	// a zone with no damage still sweeps, and this one does not even need that --
	// the grab is decided on the beat above rather than by the zone finding
	// anybody.
	//
	// IT LASTS THE FLOOR, which "appear all over the dungeon" reads as: a feature
	// of the place rather than something passing through it. The cap is what
	// keeps that from becoming a floor the player cannot cross.
	// DRAWN IN ITS ROW'S COLOURS, which it took from the shared source's type
	// until issue #1924 removed that field. It deals nothing, so the row's type
	// is its appearance and not a damage type.
	ACataclysmGroundZone* Tentacle = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Where, Where, Effects::GraspingTentaclesReachCm, 0.0f,
		/*bAffectsEveryone=*/false, FName(*Row->CataclysmType));
	if (!Tentacle)
	{
		// THE CLOCK IS NOT RESET ON A FAILED SPAWN, so the next beat tries again
		// rather than waiting a whole cadence for one that never existed.
		return;
	}

	GraspingTentaclesSecondsSinceLast = 0.0f;

	FCataclysmGraspingTentacle Placed;
	Placed.Zone = Tentacle;
	GraspingTentacles.Add(Placed);
}

void ACataclysmDungeonGameMode::StepWastingSickness(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	// ONLY WHEN THE COUNT HAS MOVED AWAY FROM WHAT IS ON THE CHARACTER, which is
	// the guard every beat-driven rule here keeps: the apply rewrites the whole
	// standing stat line, and this count changes on a blow rather than four times
	// a second.
	//
	// IT FIRES AFTER A FLOOR CHANGE AS WELL AS AFTER A BLOW, and that is not a
	// side effect. `ApplyFloorRulesToPlayer` puts the applied figure back to
	// nothing because the apply it makes has already taken the reduction off the
	// character; the stacks themselves survive, so the two differ and this puts
	// the reduction back.
	if (WastingSicknessStacks == WastingSicknessStacksApplied)
	{
		return;
	}

	WastingSicknessStacksApplied = WastingSicknessStacks;
	ApplyChangingFloorEffects(Player, AbilitySystem);
}

void ACataclysmDungeonGameMode::StepMortalDecay(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	// A WORLD-TIME STAMP AND NOT A COUNTDOWN, so a beat that does not run costs
	// the player nothing and a beat that runs late does not owe them anything.
	//
	// A NULL WORLD IS UNREACHABLE HERE AND THE FALLBACK IS STILL WRITTEN OUT.
	// `StepFloorRulesThatChange` finds the player THROUGH the world, so a beat
	// that got this far has one. What the fallback would do if that ever stopped
	// being true is worth stating rather than assuming, because it is not
	// uniformly safe in either direction: comparing the stamp against a `Now` of
	// zero answers "slowed" for any window ever opened and "not slowed"
	// otherwise, so it would be milder than the truth for a player who had
	// killed and harsher for one who had not. If this ever becomes reachable,
	// decide which reading the row wants rather than keeping this one.
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const bool bSlowed = MortalDecaySlowedUntilSeconds > Now;

	// THE FLOOR'S DEPTH, WHICH IS WHAT THE ROW'S "AS THEY PROGRESS THROUGH THE
	// DUNGEON" MEANS. `UCataclysmDungeonModifierEffects::MortalDecayKey` carries
	// the standing rule that settles it against the walk.
	//
	// THE BRIEF'S NUMBER AND NOT THIS OBJECT'S `FloorNumber`, which is the
	// reading `ApplyChangingFloorEffects` already takes: the brief belongs to
	// the floor being stood on.
	const float Rate = UCataclysmDungeonModifierEffects::MortalDecayPercentPerSecond(
		FloorBrief.FloorNumber, bSlowed);
	if (Rate <= 0.0f)
	{
		return;
	}

	// A SHARE OF MAXIMUM HEALTH, FOR THIS BEAT'S LENGTH. The rate is per second
	// and this runs four times a second, so each beat takes a quarter of it --
	// the same arithmetic `StepForcedMarch` does above, for the same reason.
	const float Maximum = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	const float Amount = Maximum * Rate / 100.0f * SecondsBetweenWaveChecks;
	if (Amount <= 0.0f)
	{
		return;
	}

	// NOT A HIT. The affliction comes from the floor rather than from an
	// attacker, so nothing in the mitigation order touches it and the player is
	// its own instigator because nothing else dealt it.
	UCataclysmSkillEffects::ReduceHealthDirectly(Player, Player, Amount);
}

void ACataclysmDungeonGameMode::StepSufferingAura(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	// HEALTH, AS A LOSS AND NOT A HIT, the route Mortal Decay takes, so it can kill the
	// way Mortal Decay can. Ruled under the owner's delegation, 2026-09-23.
	const float HealthLoss = Effects::SufferingAuraLossFor(
		AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()),
		Effects::SufferingAuraHealthPercentPerSecond, SecondsBetweenWaveChecks);
	UCataclysmSkillEffects::ReduceHealthDirectly(Player, Player, HealthLoss);

	// MANA, WRITTEN THE WAY A CAST SPENDS IT. The clamp in `PreAttributeChange` stops it
	// at zero, so nothing here checks the floor.
	const float ManaLoss = Effects::SufferingAuraLossFor(
		AbilitySystem->GetNumericAttribute(Vital::GetMaxManaAttribute()),
		Effects::SufferingAuraManaPercentPerSecond, SecondsBetweenWaveChecks);
	if (ManaLoss > 0.0f)
	{
		AbilitySystem->ApplyModToAttribute(Vital::GetManaAttribute(),
										   EGameplayModOp::Additive, -ManaLoss);
	}
}

void ACataclysmDungeonGameMode::StepNihilsEmbrace(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	const float Walked = FMath::Max(
		0.0f, AbilitySystem->MetresWalkedTotal() - MetresWalkedAtLastCleanse);
	const float Less =
		UCataclysmDungeonModifierEffects::NihilsEmbraceResistanceLost(Walked);

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	const float More = NihilsEmbraceRewardUntilSeconds > Now
		? UCataclysmDungeonModifierEffects::NihilsEmbraceRewardResistancePercent
		: 0.0f;

	// ONLY WHEN SOMETHING ACTUALLY CHANGED. Setting the modifiers is cheap, but
	// the refresh that follows rewrites the character's whole standing stat line,
	// and a player walking in a straight line changes this number once every ten
	// metres rather than four times a second.
	if (FMath::IsNearlyEqual(Less, ResistanceLessApplied)
		&& FMath::IsNearlyEqual(More, ResistanceMoreApplied))
	{
		return;
	}

	ResistanceLessApplied = Less;
	ResistanceMoreApplied = More;

	// THROUGH THE ONE APPLIER, WHICH THIS FUNCTION USED TO DO ITSELF. Issue #41,
	// slice 5. It built its own effects and set the two resistance fields, which
	// was correct while it was the only beat-driven rule and would have zeroed
	// Death's Embrace's field the moment a floor carried both.
	ApplyChangingFloorEffects(Player, AbilitySystem);
}

void ACataclysmDungeonGameMode::StepDeathsEmbrace(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	// THE TIME SPENT ON THIS FLOOR IS THE WHOLE STATE. Taking the stairs puts it
	// back to nothing, which is the row's "stacks reset when entering a new
	// floor" with nothing stored per stack -- the same shape Forced March uses,
	// where moving clears the stacks by clearing what they are counted from.
	DeathsEmbraceSecondsOnFloor += SecondsBetweenWaveChecks;

	const int32 Stacks =
		UCataclysmDungeonModifierEffects::DeathsEmbraceStacksAfter(
			DeathsEmbraceSecondsOnFloor);

	// ONLY WHEN THE COUNT ACTUALLY MOVED, which is once every ten seconds rather
	// than four times a second. The refresh inside the apply rewrites the
	// character's whole standing stat line, which is the argument
	// `StepNihilsEmbrace` makes above for the same guard.
	if (Stacks == DeathsEmbraceStacksApplied)
	{
		return;
	}

	DeathsEmbraceStacksApplied = Stacks;
	ApplyChangingFloorEffects(Player, AbilitySystem);
}

void ACataclysmDungeonGameMode::ApplyChangingFloorEffects(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (!Player)
	{
		return;
	}

	// THE FLOOR'S OWN EFFECTS FIRST, because applying replaces them wholesale: a
	// floor carrying Starvation and The Nihil's Embrace has to keep both.
	FCataclysmPlayerFloorEffects Effects =
		UCataclysmDungeonModifierEffects::PlayerEffectsFor(
			FloorBrief.Modifiers, FloorBrief.FloorNumber);

	// AND EVERY BEAT-DRIVEN FIELD FROM THIS OBJECT, WHICH IS WHY THERE IS ONE
	// APPLIER AND NOT ONE PER RULE. Issue #41, slice 5. The fields are read
	// unconditionally rather than behind a test of the floor's modifiers,
	// because a rule the floor does not carry leaves its field at nothing and
	// nothing is what the effects already hold.
	Effects.ResistanceLessPercent = ResistanceLessApplied;
	Effects.ResistanceMorePercent = ResistanceMoreApplied;
	// DEATH'S EMBRACE'S POINTS AND NECROTIC GROUND'S ADD, which is how this one stat
	// combines. At five Embrace stacks in the fog that is 100, and
	// `HealingReceivedReduction` is held between 0 and 100, so no health is restored.
	// Issues #1820 and #41.
	Effects.HealingReceivedLessPercent =
		UCataclysmDungeonModifierEffects::DeathsEmbraceHealingLessPercent(
			DeathsEmbraceStacksApplied)
		+ NecroticGroundHealingLessApplied;

	// AND THE SLOW SINGULARITY WELLS HAS IN FORCE. Issues #1605 and #41. Read
	// unconditionally like the rest: a floor without that row leaves the field at
	// nothing, and nothing is what the effects already hold.
	Effects.MovementSpeedLessPercent = SingularityWellsSlowApplied;

	// AND THE RECOVERY WITHERED GROUND IS TAKING. Read unconditionally like the
	// rest, for the same reason: a floor without that row leaves the field at
	// nothing, and nothing is what the effects already hold. Issue #41.
	//
	// PLAIN ASSIGNMENT IS SAFE ONLY WHILE NO PER-FLOOR RULE WRITES THIS FIELD.
	// `PlayerEffectsFor` above fills the three per-floor fields and leaves the
	// beat-driven ones alone, so these assignments land on nothing. A per-floor
	// rule that later wrote one of them would have its value overwritten here,
	// on a floor that also carried the beat rule and not otherwise -- a fault
	// that depends on which other modifier the floor rolled. Issue #1765.
	Effects.RecoveryLessPercent = WitheredGroundRecoveryLessApplied;

	// AND WHAT ETERNAL CHORUS DOES WITHIN EARSHOT, in two fields of its own. Issues #1820 and #41.
	Effects.ChorusCooldownLongerPercent = EternalChorusCooldownApplied;
	Effects.ChorusRegenLessPercent = EternalChorusRegenApplied;

	// AND WHAT WASTING SICKNESS'S STACKS TAKE OFF BOTH MAXIMUMS. Issues #1786
	// and #41. Read unconditionally like the rest: a floor without that row
	// carries no stacks, and nothing is what the effects already hold.
	//
	// ITS OWN TWO FIELDS AND NOT STARVATION'S AND DEHYDRATION'S, which is what
	// makes this assignment safe where the note above says plain assignment is
	// only safe while no per-floor rule writes the field. `PlayerEffectsFor`
	// writes `MaxHealthLessPercent` and `MaxManaLessPercent`; these two are
	// untouched by it, so the two rules compose instead of erasing each other.
	// Issue #1765.
	const float SicknessLess =
		UCataclysmDungeonModifierEffects::WastingSicknessMaximumsLessPercent(
			WastingSicknessStacksApplied);
	Effects.SicknessMaxHealthLessPercent = SicknessLess;
	Effects.SicknessMaxManaLessPercent = SicknessLess;

	// AND WHAT A TENTACLE'S GRAB IS TAKING. Issues #1786 and #41. Read
	// unconditionally like the rest: a floor without that row never grabs, and
	// nothing is what the effects already hold.
	//
	// ITS OWN FIELD AND NOT `MovementSpeedLessPercent`, which Singularity Wells
	// writes. Both rows are Void and a floor can carry both, so sharing would mean
	// whichever wrote second erased the first. Issue #1765.
	Effects.GraspMovementLessPercent = GraspMovementLessApplied;

	// AND WHAT THE STARVATION CURSE'S STACKS TAKE. Issues #1820 and #41. Their own two
	// fields, which the declaration explains; read unconditionally like the rest.
	Effects.CurseMovementLessPercent =
		UCataclysmDungeonModifierEffects::StarvationCurseLessPercent(
			StarvationCurseMovementApplied);
	Effects.CurseMaxHealthLessPercent =
		UCataclysmDungeonModifierEffects::StarvationCurseLessPercent(
			StarvationCurseHealthApplied);

	// AND A TREAT'S HASTE, on both of its fields. Issues #1820 and #41.
	Effects.TreatSpeedMorePercent = TrickOrTreatHasteApplied;
	Effects.TreatAttackSpeedMorePercent = TrickOrTreatHasteApplied;

	// AND CHAOS TOUCHED'S STACKS, on their own eight fields. Issues #1820 and #41.
	{
		using Effects_ = UCataclysmDungeonModifierEffects;
		const auto Percent = [this](int32 Kind)
		{
			return Effects_::ChaosTouchedPercentFor(
				ChaosTouchedApplied.IsValidIndex(Kind) ? ChaosTouchedApplied[Kind] : 0);
		};
		Effects.TouchedMaxHealthMorePercent = Percent(Effects_::ChaosTouchedHealthMore);
		Effects.TouchedSpeedMorePercent = Percent(Effects_::ChaosTouchedSpeedMore);
		Effects.TouchedAttackSpeedMorePercent = Percent(Effects_::ChaosTouchedAttackSpeedMore);
		Effects.TouchedResistanceMorePercent = Percent(Effects_::ChaosTouchedResistanceMore);
		Effects.TouchedMaxHealthLessPercent = Percent(Effects_::ChaosTouchedHealthLess);
		Effects.TouchedSpeedLessPercent = Percent(Effects_::ChaosTouchedSpeedLess);
		Effects.TouchedAttackSpeedLessPercent = Percent(Effects_::ChaosTouchedAttackSpeedLess);
		Effects.TouchedResistanceLessPercent = Percent(Effects_::ChaosTouchedResistanceLess);
	}

	// AND THE MAGIC FIND THE RIFTS CLOSED IN TIME HAVE EARNED. Issues #1820 and #41. Read unconditionally like the
	// rest: a player who has closed none is owed nothing.
	Effects.RiftMagicFindAdded =
		UCataclysmDungeonModifierEffects::AbyssalRiftsMagicFindFor(AbyssalRiftSuccessesApplied);

	// AND WHAT THE ATTACHED VOIDLINGS TAKE, on its own field. Issues #1820 and #41. Read unconditionally like
	// the rest: a player carrying none is owed nothing.
	Effects.ParasiteLessPercent =
		UCataclysmDungeonModifierEffects::VoidParasiteLessPercent(VoidParasiteStacksApplied);

	// AND WHAT A MUSHROOM UNDERFOOT IS DOING, IN BOTH DIRECTIONS. Issues #1820
	// and #41. Read unconditionally like the rest: a floor without that row
	// places no mushroom, and nothing is what the effects already hold.
	//
	// TWO OF ITS OWN FIELDS AND NOT `MovementSpeedLessPercent`, which Singularity
	// Wells writes, nor `GraspMovementLessPercent`, which a tentacle writes.
	// Three rows now move the same stat and each holds its own field, so a floor
	// carrying all three composes instead of erasing. Issue #1765.
	Effects.MushroomSpeedMorePercent = FungalOvergrowthSpeedMoreApplied;
	Effects.MushroomSpeedLessPercent = FungalOvergrowthSpeedLessApplied;

	// AND WHAT JUDGMENT IS TAKING OFF ONE RESISTANCE. Issues #1820 and #41. Read
	// unconditionally like the rest: a floor without that row carries no stacks,
	// and nothing is what the effects already hold.
	//
	// ITS OWN FIELD AND NOT `ResistanceLessPercent`, which The Nihil's Embrace
	// writes. That one is applied to all eight resistances in a loop; this is one
	// of them. The field's own declaration says why the two cannot share.
	Effects.JudgmentResistanceLessPercent =
		UCataclysmDungeonModifierEffects::HolyRepercussionsJudgmentLessPercent(
			JudgmentStacksApplied);

	// AND WHETHER THE EDICT OF SILENCE HAS THE PLAYER'S SKILLS LOCKED. Issues
	// #1786 and #41. Read unconditionally like the rest: a floor without that row
	// never sets it, and nothing is what the effects already hold.
	Effects.SkillsLockedValue = EdictOfSilenceLockApplied;

	// AND WHETHER AN ANTI-MAGIC ZONE HAS THE PLAYER'S SPELLS LOCKED. Issues #1820 and #41.
	// Read unconditionally like the rest. ITS OWN FIELD AND NOT `SkillsLockedValue`: the
	// Edict's lock reaches every skill and this one reaches spells, so sharing a field
	// would let whichever rule wrote second decide the scope for both. Issue #1765.
	Effects.SpellsLockedValue = AntiMagicZonesLockApplied;

	// AND THE ARMOUR MARCH OF PROGRESS HAS PAID THE PLAYER. Issues #1820 and #41. Read
	// unconditionally like the rest: a player who has killed no commanders is owed
	// nothing, and nothing is what the effects already hold.
	//
	// THE COUNT AND NOT THE APPLIED FIGURE, WHICH IS THE OPPOSITE OF EVERY LINE ABOVE.
	// Those fields hold what is standing on the character because their rules work out a
	// share on the beat. This one is worked out from a count that only a kill moves, so
	// the count is the truth and `MarchOfProgressArmourApplied` only records what the
	// last apply put on, so the beat can tell when the two differ.
	Effects.ArmourMorePercent =
		UCataclysmDungeonModifierEffects::MarchOfProgressArmourMorePercentFor(
			MarchOfProgressCommandersKilled);

	UCataclysmDungeonModifierEffects::ApplyToCharacter(Effects, AbilitySystem,
													  Player->GetEquipment());
}

void ACataclysmDungeonGameMode::OnSomethingDied(
	const FCataclysmDeathNotice& Notice)
{
	// ONE NOTICE, EVERY RULE THAT WANTS IT, EACH TESTING FOR ITS OWN ROW.
	// Issue #41. This function held The Nihil's Embrace's logic behind a single
	// early return on that rule's key, which is the shape that stops a second
	// listener from being added without rewriting the first.
	NoteDeathForNihilsEmbrace(Notice);
	NoteDeathForWitheredGround(Notice);
	NoteDeathForFungalOvergrowth(Notice);
	NoteDeathForLeechSpores(Notice);
	NoteDeathForBloodAltar(Notice);
	NoteDeathForMortalDecay(Notice);
	NoteDeathForWastingSickness(Notice);
	NoteDeathForSporeClouds(Notice);
	NoteDeathForHellfire(Notice);
	NoteDeathForDemonPrince(Notice);
	NoteDeathForEpidemic(Notice);
	NoteDeathForBloodForgedChampions(Notice);
	NoteDeathForVengefulWraiths(Notice);
	NoteDeathForVoidParasite(Notice);
	NoteDeathForObsidianSarcophagi(Notice);
	NoteDeathForInfestedHoard(Notice);
	NoteDeathForAbyssalRifts(Notice);
	NoteDeathForRawSewage(Notice);
	NoteDeathForMarchOfProgress(Notice);
	// BEFORE DIVINE RESURGENCE, so a creature this same death got back up is already
	// standing and marked when that rule counts the floor. Issues #1820 and #41.
	NoteDeathForDeadRising(Notice);
	NoteDeathForBloodGates(Notice);
	NoteDeathForNothingIsForgotten(Notice);
	NoteDeathForStarvationCurse(Notice);
	NoteDeathForSoulHarvest(Notice);
	NoteDeathForChaosTouched(Notice);
	NoteDeathForBloodBond(Notice);
	NoteDeathForPlagueConvergence(Notice);
	NoteDeathForEchoesOfThePast(Notice);
	NoteDeathForPlagueHarbingers(Notice);
	// LAST, so a wraith this same death raised is already standing and already marked
	// when the floor's creatures are counted. Issues #1820 and #41.
	NoteDeathForDivineResurgence(Notice);
}

void ACataclysmDungeonGameMode::NoteDeathForDemonPrince(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::DemonPrinceKey)))
	{
		return;
	}

	// ONE A FLOOR. The ceiling is asked first, so a floor that has had its own does no
	// further work on any death.
	if (!Effects::DemonPrinceMayRise(DemonPrincesRisen))
	{
		return;
	}

	ACataclysmEnemyCharacter* Slain = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Slain)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// "WHEN YOU SLAY AN ENEMY", WHICH IS NOW ONE QUESTION. The killer on the notice
	// is the player only when the player really killed it: a minion's kill is credited
	// to the minion unless its summoner holds the Conduit keystone, which
	// `UCataclysmCombatEvents::NoteBlow` decides in one place. Issue #1515.
	//
	// THIS USED TO ASK TWICE, and the second question was written pending exactly that
	// correction. A minion's blow was credited to its summoner, so the killer alone
	// counted a minion's kill as the player's, and a second check on the actor that
	// dealt the blow refused it. That check is gone with this change, because a minion's
	// kill now fails the first question by itself -- and a summoner who HAS taken the
	// keystone should bring a prince from its minion's kill, which the second check
	// would have gone on refusing.
	APlayerController* Controller = World->GetFirstPlayerController();
	const ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player || Notice.Killer != Player)
	{
		return;
	}

	if (!Effects::DemonPrinceRises(DungeonGameModeDemonPrinceRoll()))
	{
		return;
	}

	// ITS OWN KIND, WORKED OUT FROM ITS CLASS, and nothing rises for a creature that is
	// none of the seven. That is Royal Guard's refusal and the same reason: a kind
	// guessed here would put a creature on the floor the floor's own populator would
	// never place.
	const ECataclysmDungeonCreature Kind = DungeonGameModeKindOf(Slain);
	if (Kind == ECataclysmDungeonCreature::Count)
	{
		UE_LOG(LogCataclysm, Log,
			   TEXT("Demon Prince: %s is none of the kinds this dungeon places, so "
					"nothing rose from it"),
			   *Slain->GetName());
		return;
	}

	FCataclysmEnemyPlacement Placement;
	Placement.Cell = CurrentFloor->CellOfWorld(Notice.Location);
	Placement.Creature = Kind;

	ACataclysmEnemyCharacter* Risen =
		SpawnPlacedCreature(Placement, FloorBrief.SightRadiusMultiplier);
	if (!Risen)
	{
		return;
	}

	// THE RUNG AFTER THE SPAWN, because the spawn gives the creature its kind's own.
	// The refill both calls end in is right here: what rises has not been fought yet.
	Risen->SetRarityStep(Effects::DemonPrinceRung);
	Risen->DrawModifiersForRarity();
	FloorEnemies.Add(Risen);
	++DemonPrincesRisen;

	UE_LOG(LogCataclysm, Log,
		   TEXT("Demon Prince: the player's kill of %s (%s) brought one of its own kind "
				"at rarity step %d"),
		   *Slain->GetName(), CataclysmDungeonCreatureName(Kind),
		   Effects::DemonPrinceRung);

	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteDeathForEpidemic(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::EpidemicKey)))
	{
		return;
	}

	// NOT WHILE THIS RULE IS KILLING. The deaths the mass kill causes are real deaths and
	// are announced, so one of them can come straight back here, roll again, and start a
	// second chain inside the first.
	//
	// THE CASE THIS GUARDS IS NARROWER THAN IT LOOKS, measured on 2026-09-18 by reading
	// `UCataclysmCombatEvents::NoteDeath` and `NoteBlow`. A death caused by writing health
	// to zero carries no killer of its own: the notice's killer is read out of the dying
	// creature's OWN last blow, and that record is written only for a blow that reached
	// health. A creature the player never damaged dies anonymously and is refused by the
	// killer check below with or without this flag. THE CASE THIS FLAG EXISTS FOR is a
	// creature the player DAMAGED BUT DID NOT KILL, which the mass kill then finishes: its
	// record names the player, so the death arrives here as the player's own kill.
	// `Cataclysm.DungeonModifierEffects.TheMassKillDoesNotFeedItself` builds that case on
	// purpose, and is the test that fails if this is removed.
	if (bEpidemicKilling)
	{
		return;
	}

	ACataclysmEnemyCharacter* Slain = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Slain)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// "WHEN YOU KILL", WHICH IS ONE QUESTION. The killer on the notice is the player only
	// when the player really killed it: a minion's kill is credited to the minion unless
	// its summoner holds the Conduit keystone, which `UCataclysmCombatEvents::NoteBlow`
	// decides in one place. Issue #1515, and `NoteDeathForDemonPrince` above says the same.
	//
	// SO THERE IS NO SECOND CHECK ON THE ACTOR THAT DEALT THE BLOW, and there must not be:
	// a summoner who HAS taken that keystone should spread a disease from its minion's
	// kill, and a check on the dealer would go on refusing it.
	APlayerController* Controller = World->GetFirstPlayerController();
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player || Notice.Killer != Player)
	{
		return;
	}

	// "A DISEASED ENEMY", WHICH IS ASKED OF THE CORPSE ITSELF. A creature carrying
	// nothing that could pass on is not diseased: no roll happens and the chain is left
	// where it is. That is not the same as a roll that misses, which breaks it.
	const TArray<FGameplayTag> Passing = UCataclysmContagion::EverySpreadable(
		UCataclysmTargeting::AbilitySystemOf(Slain));
	if (Passing.IsEmpty())
	{
		return;
	}

	if (!Effects::EpidemicSpreads(DungeonGameModeEpidemicRoll()))
	{
		// A ROLL THAT MISSES BREAKS THE CHAIN, which is what "in a single chain" means.
		EpidemicChain = 0;
		RefreshFloorModifierPanel();
		return;
	}

	// THE NEAREST CREATURE WITHIN THE REACH, and the search is made FROM THE PLAYER with
	// the body's location as its centre, for the reason `UCataclysmContagion::
	// SpreadOnDeath` gives: the search decides sides from the actor passed to it, so
	// passing the corpse would find the player.
	ACataclysmEnemyCharacter* Nearest = nullptr;
	float NearestAway = TNumericLimits<float>::Max();
	for (AActor* Found : UCataclysmTargeting::FindEnemiesInSphere(
			 World, Player, Notice.Location, Effects::EpidemicRadiusCm()))
	{
		ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Found);
		if (!IsValid(Creature) || Creature == Slain)
		{
			continue;
		}
		const float Away = FVector::Dist(Creature->GetActorLocation(), Notice.Location);
		if (Away < NearestAway)
		{
			NearestAway = Away;
			Nearest = Creature;
		}
	}

	if (!Nearest)
	{
		// NOBODY TO CATCH IT. The roll was spent and nothing landed, so the chain breaks
		// the way a missed roll breaks it: a chain is spreads in a row.
		EpidemicChain = 0;
		RefreshFloorModifierPanel();
		return;
	}

	// ALL OF THEM, WHICH IS WHAT THE ROW SAYS. `EverySpreadable` is the list
	// `PickSpreadable` chooses one from; this rule wants the whole of it.
	int32 Landed = 0;
	for (const FGameplayTag& Tag : Passing)
	{
		Landed += UCataclysmContagion::SpreadOne(Player, Nearest, Tag) ? 1 : 0;
	}

	++EpidemicChain;

	UE_LOG(LogCataclysm, Log,
		   TEXT("Epidemic: %s died carrying %d debuff(s), %d landed on %s, chain %d of %d"),
		   *Slain->GetName(), Passing.Num(), Landed, *Nearest->GetName(), EpidemicChain,
		   Effects::EpidemicSpreadsToKill);

	if (Effects::EpidemicChainIsComplete(EpidemicChain))
	{
		EpidemicEndTheChain(Notice.Location, DungeonGameModeKindOf(Slain));
	}

	RefreshFloorModifierPanel();
}

float ACataclysmDungeonGameMode::JudgmentZonesMagicFindBonus() const
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::JudgmentZonesKey))
		|| !Effects::JudgmentZonesBonusIsEarned(JudgmentZonesTriggers))
	{
		return 0.0f;
	}

	return Effects::JudgmentZonesMagicFind;
}

float ACataclysmDungeonGameMode::JudgmentZonesMagicFindIn(const UObject* WorldContext)
{
	// THE HOP NO AUTOMATION TEST CAN TAKE. See the declaration: a world built for a test
	// has no authority game mode, so this answers nothing there and the arithmetic it feeds
	// is tested separately.
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContext,
											 EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return 0.0f;
	}

	const ACataclysmDungeonGameMode* Mode =
		World->GetAuthGameMode<ACataclysmDungeonGameMode>();
	return Mode ? Mode->JudgmentZonesMagicFindBonus() : 0.0f;
}

void ACataclysmDungeonGameMode::StepJudgmentZones(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	// WHAT IS STILL STANDING, ASKED RATHER THAN REMEMBERED. A zone destroys itself when its
	// life ends, so a weak pointer going invalid IS the expiry. `StepInfernalRain` keeps its
	// patches the same way.
	JudgmentZones.RemoveAll([](const TWeakObjectPtr<ACataclysmGroundZone>& Zone)
	{
		return !Zone.IsValid();
	});

	// THE TYPE COMES OUT OF THE ROW AND IS NOT WRITTEN HERE, which is what answers the
	// row's "holy damage" -- a word naming no damage type this game has. The eight types
	// are the eight Cataclysms and this row's own `CataclysmType` is Celestial.
	// `StepInfernalRain` gives the reason at length: a row retyped in the workbook retypes
	// its hazard with no code change.
	const FCataclysmDungeonModifierRow* Row = UCataclysmDungeonModifierTable::FindRow(
		UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
		FName(Effects::JudgmentZonesKey));
	if (!Row)
	{
		return;
	}

	const float MaxHealth = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute());

	// LAYING NEW GROUND FIRST, THEN WHAT THE PLAYER OWES FOR STANDING ON IT. The order
	// matters only in that a zone laid this beat cannot be one the player is already inside:
	// it is laid past its own radius from them, as Infernal Rain's is and for the reason
	// Infernal Rain gives -- ground that damages the instant it appears is not ground to get
	// off.
	JudgmentZonesSecondsSinceLastZone += SecondsBetweenWaveChecks;
	if (Effects::JudgmentZoneIsDue(JudgmentZonesSecondsSinceLastZone, JudgmentZones.Num()))
	{
		const FVector Centre = Player->GetActorLocation();
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const float Away = FMath::FRandRange(Effects::JudgmentZonesRadiusCm + 1.0f,
											 Effects::JudgmentZonesFallsWithinCm);
		const FVector Where(Centre.X + Away * FMath::Cos(Angle),
							Centre.Y + Away * FMath::Sin(Angle), Centre.Z);

		// THE SOURCE IS MADE LAST, because `ForFloor` spawns one when the floor has none
		// and everything that could refuse has now been asked. `StepInfernalRain` gives
		// the reason: asking first and then finding a reason not to lay ground would
		// leave an actor on the floor that nothing uses.
		//
		// DECLARED IN ONE LINE AND IN THIS EXACT FORM ON PURPOSE.
		// `test_every_ground_zone_the_game_mode_places_is_owned_by_the_hazard_source`
		// reads this file and requires every function that spawns a zone to take its
		// owner from `ForFloor` in the same function, because a same-arena floor change
		// destroys a rule's zones BY THEIR OWNER (issue #1925). A zone owned by anything
		// else would survive a Horde dungeon's next wave with no rule acting for it.
		ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(
			World);
		if (Source)
		{
			// SPAWNED WITH NO DAMAGE OF ITS OWN, AND THAT IS THE RULE'S SHAPE RATHER THAN AN
			// OVERSIGHT. Issue #1701 made a zone that does not damage possible so Singularity
			// Wells could have a well that slows without damaging. Here the zone is the ground
			// the player sees and stands in, and this rule below deals the damage and counts
			// the trigger in one place, so the reward cannot disagree with what earned it. Its
			// own sweep is skipped, which is what a zone that neither damages nor applies an
			// effect does.
			if (ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
					Source, Where, Effects::JudgmentZonesRadiusCm,
					Effects::JudgmentZonesSeconds, /*DamagePerTick=*/0.0f,
					FName(*Row->CataclysmType)))
			{
				JudgmentZones.Add(Zone);
				JudgmentZonesSecondsSinceLastZone = 0.0f;
			}
			// THE CLOCK IS NOT RESET ON A FAILED SPAWN, so the next beat tries again rather
			// than waiting a whole cadence for ground that never existed.
		}
	}

	// WHICH ZONE THE PLAYER IS IN, IF ANY. The first one found is the answer: zones may
	// overlap, and a player inside two owes one ramp rather than two.
	ACataclysmGroundZone* Inside = nullptr;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Weak : JudgmentZones)
	{
		ACataclysmGroundZone* Zone = Weak.Get();
		if (IsValid(Zone)
			&& FVector::Dist(Player->GetActorLocation(), Zone->GetActorLocation())
				   <= Zone->RadiusCm)
		{
			Inside = Zone;
			break;
		}
	}

	// STEPPING OUT, OR INTO A DIFFERENT ONE, SETS THE RAMP BACK. The row says standing
	// inside ramps the damage, so the ramp belongs to the zone stood in and not to the
	// floor. The TRIGGER COUNT is not touched here: the row says "5+ times" and not "in a
	// row", so ticks anywhere on the floor count towards the same total.
	if (Inside != JudgmentZonesStandingIn.Get())
	{
		JudgmentZonesStandingIn = Inside;
		JudgmentZonesSecondsInside = 0.0f;
		JudgmentZonesTicksInThisZone = 0;
	}

	if (!Inside)
	{
		return;
	}

	// FOUR BEATS MAKE A SECOND, and the remainder is carried rather than dropped, so a
	// player who steps in and out repeatedly is not charged less than one who stands.
	JudgmentZonesSecondsInside += SecondsBetweenWaveChecks;

	// THE SAME OWNER THE GROUND WAS LAID IN THE NAME OF, so the damage a zone's ground
	// does is dealt by the actor that owns it.
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(
		World);
	bool bCounted = false;
	while (JudgmentZonesSecondsInside >= 1.0f)
	{
		JudgmentZonesSecondsInside -= 1.0f;

		const float Damage =
			Effects::JudgmentZonesDamageFor(MaxHealth, JudgmentZonesTicksInThisZone);
		if (Damage <= 0.0f || !Source)
		{
			// A CHARACTER WITH NO MAXIMUM HEALTH OWES NOTHING AND IS TRIGGERED BY NOTHING.
			// Counting a trigger for a tick that dealt nothing would let a floor pay its
			// reward for damage the player never took.
			break;
		}

		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		Delivery.DamageType = FName(*Row->CataclysmType);
		UCataclysmSkillEffects::ApplyDirectDamage(Source, Player, Damage, Delivery);

		++JudgmentZonesTicksInThisZone;
		++JudgmentZonesTriggers;
		bCounted = true;
	}

	if (bCounted)
	{
		UE_LOG(LogCataclysm, Log,
			   TEXT("Judgment Zones: the player has taken %d tick(s) of %s damage, %d of %d "
					"towards the floor's bonus"),
			   JudgmentZonesTicksInThisZone, *Row->CataclysmType, JudgmentZonesTriggers,
			   Effects::JudgmentZonesTriggersForTheBonus);
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::ApplyVengefulWraithFigures(
	ACataclysmEnemyCharacter* Wraith)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Combat = UCataclysmCombatAttributeSet;

	if (!IsValid(Wraith) || !VengefulWraiths.Contains(Wraith))
	{
		return;
	}

	UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Wraith);
	if (!Abilities)
	{
		return;
	}

	// WRITTEN STRAIGHT ONTO THE ATTRIBUTES, AND THAT IS NOT A SHORTCUT. There is no
	// creature equivalent of `UCataclysmDungeonModifierEffects::PlayerEffectsFor`, which is
	// how a floor rule gives the PLAYER a stat change; a creature has no such path, so this
	// is the only way a rule reaches one. The cost is that anything writing the creature's
	// stat block again wipes these, which is why the two rules that raise a living
	// creature's rung both call this afterwards.
	//
	// THE ROW'S 90 GOES INTO THE MULTIPLICATIVE BUCKET, WHICH IS THE PROJECT OWNER'S
	// DECISION. `UCataclysmDamageCalculation::DamageReductionCap` bounds the ADDITIVE pool
	// at 75, so 90 written there would read as 75 and the row's number would not be what
	// happens. `MoreDamageReductionCap` bounds one multiplicative source at 99, and the
	// calculation reads this attribute as a percentage, clamps it and divides by 100 -- so
	// a wraith takes a tenth of whatever the other layers leave, which is what the row says.
	Abilities->SetNumericAttributeBase(Combat::GetDamageReductionMoreAttribute(),
									   Effects::VengefulWraithsDamageReductionMore);

	// AND THE ONE INCREASE THE ROW STATES, ON THE ONE STAT OF THE THREE THAT IS AN
	// ATTRIBUTE. It is read and raised, so a wraith is the row's figure above ITS OWN
	// rung's damage rather than above a fixed one: a wraith later raised a rung keeps the
	// rung's gain and its own on top.
	Abilities->SetNumericAttributeBase(
		Combat::GetAttackDamageAttribute(),
		Effects::VengefulWraithsIncreased(
			Abilities->GetNumericAttribute(Combat::GetAttackDamageAttribute())));

	// AND THE OTHER TWO STATS THE ROW NAMES ARE NOT ATTRIBUTES AT ALL, WHICH WAS MEASURED
	// AND NOT ASSUMED. A creature's attack rate is its designed interval over
	// `ACataclysmEnemyCharacter::SpeedMultiplier`, and its walk speed is its designed
	// speed times the same; NEITHER reads `AttackSpeed` or `MovementSpeed`. Writing those
	// two attributes here did nothing at all, and the automation test for the row's three
	// stats is what found it: the attack speed attribute read 0.00 on a creature.
	//
	// SO THE TWO SPEEDS GO THROUGH THE CREATURE'S OWN MULTIPLIER, where `SpeedMultiplier`
	// says an effect naming BOTH belongs. The flag is all this rule sets; the factor is
	// read from this rule's own constant on the other side.
	Wraith->bIsVengefulWraith = true;

	// AND IT IS MARKED AS BROUGHT BACK, SO ITS DEATH PAYS NOTHING. Issues #1820 and #41.
	// The kill it stood up from already paid, and the owner's decision of 2026-09-17 is
	// that one kill pays once. Ruled under the owner's delegation on 2026-09-23 that a
	// wraith is a revival -- the row says the kill "stands back up" -- and the owner may
	// veto it. See `ACataclysmEnemyCharacter::bRisenFromTheDead`.
	Wraith->bRisenFromTheDead = true;

	// AND THE WALK SPEED IS PUT RIGHT NOW RATHER THAN NEXT FRAME. `RefreshWalkSpeed` runs
	// every Tick anyway, so this only spares the creature one frame at its old speed --
	// but it is also what lets a test read the speed without ticking the world.
	Wraith->RefreshWalkSpeed();

	// AND IT GOES ON SEEING THE WHOLE FLOOR. `SpawnPlacedCreature` set this when the wraith
	// rose; it is written again here because a rung change is a good place to lose it and
	// because a wraith that stopped hunting would be the row's last sentence undone.
	Wraith->SightRadiusMultiplier = Effects::VengefulWraithsSightMultiplier;
}

void ACataclysmDungeonGameMode::NoteDeathForVengefulWraiths(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE FIGURE COVERS THE LONGEST LINE ON THE LARGEST FLOOR THIS GAME BUILDS. The
	// multiplier scales each creature's OWN radius and the smallest of the seven is the
	// Imp's 1000 cm, so it is sized against that one. 1.5 stands in for the square root of
	// two, which is not available at compile time, and is on the safe side of it.
	static_assert(
		Effects::VengefulWraithsSightMultiplier * ACataclysmImpCharacter::ImpNoticeRadiusCm
			>= FCataclysmFloorGenerator::MostFloorSide
				   * FCataclysmFloorGenerator::CellSizeCm * 1.5f,
		"A wraith can no longer see across the largest floor this game builds. Either the "
		"floor grew or the multiplier shrank; the row says it hunts across the entire "
		"dungeon.");

	if (!FloorBrief.Modifiers.Contains(FName(Effects::VengefulWraithsKey)))
	{
		return;
	}

	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Fallen)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// THE DESTROYED ARE FORGOTTEN, so the record does not grow from floor to floor. A
	// creature that has died keeps its entry until the actor itself is destroyed, which
	// costs nothing. Royal Guard's record is kept the same way.
	for (auto Entry = VengefulWraiths.CreateIterator(); Entry; ++Entry)
	{
		if (Entry->IsStale())
		{
			Entry.RemoveCurrent();
		}
	}

	// "THE ONE WHO KILLED THEM", WHICH IS ONE QUESTION. The killer on the notice is the
	// player only when the player really killed it: a minion's kill is credited to the
	// minion unless its summoner holds the Conduit keystone, which
	// `UCataclysmCombatEvents::NoteBlow` decides in one place. Issue #1515.
	//
	// SO A MINION'S KILL RAISES NOTHING UNLESS THAT KEYSTONE IS HELD, and there is no
	// second check on the actor that dealt the blow. A summoner who HAS taken it should
	// raise a wraith from its minion's kill; one who has not should not, because the whole
	// point of that change was that such a kill is the minion's own.
	APlayerController* Controller = World->GetFirstPlayerController();
	const ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player || Notice.Killer != Player)
	{
		return;
	}

	if (!Effects::VengefulWraithRises(DungeonGameModeVengefulWraithRoll()))
	{
		return;
	}

	// ITS OWN KIND, WORKED OUT FROM ITS CLASS, and nothing rises from a creature that is
	// none of the seven. Royal Guard, Demon Prince and Epidemic all make that refusal, for
	// the reason the first of them gives: a kind guessed here would put a creature on the
	// floor the floor's own populator would never place.
	const ECataclysmDungeonCreature Kind = DungeonGameModeKindOf(Fallen);
	if (Kind == ECataclysmDungeonCreature::Count)
	{
		UE_LOG(LogCataclysm, Log,
			   TEXT("Vengeful Wraiths: %s is none of the kinds this dungeon places, so "
					"nothing rose from it"),
			   *Fallen->GetName());
		return;
	}

	FCataclysmEnemyPlacement Placement;
	Placement.Cell = CurrentFloor->CellOfWorld(Notice.Location);
	Placement.Creature = Kind;

	// THE SIGHT MULTIPLIER IS GIVEN AT THE SPAWN, not written afterwards, because that is
	// the parameter `SpawnPlacedCreature` already takes and the floor's own creatures get
	// theirs the same way.
	ACataclysmEnemyCharacter* Wraith =
		SpawnPlacedCreature(Placement, Effects::VengefulWraithsSightMultiplier);
	if (!Wraith)
	{
		return;
	}

	FloorEnemies.Add(Wraith);
	VengefulWraiths.Add(Wraith);

	// THE FIGURES AFTER THE SPAWN, because the spawn gives the creature its kind's own and
	// the increases raise what they read.
	ApplyVengefulWraithFigures(Wraith);
	++VengefulWraithsRisen;

	UE_LOG(LogCataclysm, Log,
		   TEXT("Vengeful Wraiths: the player's kill of %s (%s) left a wraith that takes "
				"%.0f%% less from each hit and sees %.0f times as far"),
		   *Fallen->GetName(), CataclysmDungeonCreatureName(Kind),
		   Effects::VengefulWraithsDamageReductionMore,
		   Effects::VengefulWraithsSightMultiplier);

	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteDeathForVoidParasite(const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::VoidParasiteKey)))
	{
		return;
	}

	// A DEATH THAT PAYS NOTHING LEAVES NOTHING: a floor source, a creature a rule brought back, and a
	// voidling itself, so one voidling cannot leave another.
	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Fallen || !Fallen->PaysForItsDeath())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// THE PLAYER'S KILL, READ AS DEMON PRINCE READS IT: a minion's kill is the minion's own unless its
	// summoner holds the Conduit keystone, which `UCataclysmCombatEvents::NoteBlow` decides. Issue #1515.
	APlayerController* Controller = World->GetFirstPlayerController();
	const ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player || Notice.Killer != Player)
	{
		return;
	}

	if (!Effects::VoidlingRises(DungeonGameModeVoidParasiteRoll()))
	{
		return;
	}

	// AN IMP AT COMMON WHERE THE CREATURE DIED, with the Imp's brain, attack and health, seeing as far as a
	// wraith does so that it comes for the player from anywhere on the floor.
	FCataclysmEnemyPlacement Placement;
	Placement.Cell = CurrentFloor->CellOfWorld(Notice.Location);
	Placement.Creature = ECataclysmDungeonCreature::Imp;
	ACataclysmEnemyCharacter* Voidling =
		SpawnPlacedCreature(Placement, Effects::VengefulWraithsSightMultiplier, /*FixedRung=*/0);
	if (!Voidling)
	{
		return;
	}

	// IT PAYS NOTHING, IS RAISED BY THE RULE AND IS NOT ONE OF THE FLOOR'S CREATURES, as ruled.
	Voidling->bIsAVoidling = true;
	Voidling->bDiesUnpaid = true;
	Voidling->bRaisedByARule = true;
	CreaturesRaisedByARule.Add(Voidling);
	Voidlings.Add(Voidling);

	UE_LOG(LogCataclysm, Log, TEXT("Void Parasite: the player's kill of %s left a voidling on floor %d"),
		   *Fallen->GetName(), FloorNumber);
}

void ACataclysmDungeonGameMode::NoteDeathForDeadRising(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::DeadRisingKey)))
	{
		return;
	}

	// A MARKED CREATURE NEVER ROLLS, so one extra life is the most this row gives. Ruled
	// under the owner's delegation, 2026-09-23. A Vengeful Wraith and a creature Divine
	// Resurgence raised are marked too, and are refused here for the same reason.
	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!IsValid(Fallen) || !Fallen->PaysForItsDeath())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// EVERY DEATH ROLLS, WHOEVER DEALT IT: the row says "after being killed" and names no
	// killer, so nothing here asks who did it.
	if (!Effects::DeadRisingRevives(DungeonGameModeDeadRisingRoll()))
	{
		return;
	}

	// ITS OWN KIND, and nothing gets up from a creature that is none of the seven, the
	// refusal Vengeful Wraiths and Divine Resurgence make.
	const ECataclysmDungeonCreature Kind = DungeonGameModeKindOf(Fallen);
	if (Kind == ECataclysmDungeonCreature::Count)
	{
		return;
	}

	FCataclysmEnemyPlacement Placement;
	Placement.Cell = CurrentFloor->CellOfWorld(Notice.Location);
	Placement.Creature = Kind;

	// AT THE RUNG IT DIED AT, set before its modifiers are drawn, as Divine Resurgence
	// does. AT FULL HEALTH because the spawn gives it that and nothing here lowers it:
	// "revive" with no reduction stated is the creature as it was placed.
	ACataclysmEnemyCharacter* Risen = SpawnPlacedCreature(
		Placement, FloorBrief.SightRadiusMultiplier, Fallen->RarityStep);
	if (!Risen)
	{
		return;
	}
	Risen->bRisenFromTheDead = true;
	FloorEnemies.Add(Risen);
	++DeadRisingRisen;

	UE_LOG(LogCataclysm, Log,
		   TEXT("Dead Rising: %s (%s) got back up at rung %d"),
		   *Fallen->GetName(), CataclysmDungeonCreatureName(Kind), Fallen->RarityStep);

	RefreshFloorModifierPanel();
}

int32 ACataclysmDungeonGameMode::DivineResurgencePlacedCount() const
{
	// THE FALLEN PLUS THE UNMARKED STILL STANDING, and not `FloorEnemies.Num()`. The list
	// keeps an entry per creature added, but a destroyed creature's entry can be emptied
	// by the engine at any time afterwards, so its length drifts; this count does not.
	// A MARKED creature is neither: it is one of the floor's dead brought back.
	int32 Standing = 0;
	for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : FloorEnemies)
	{
		if (IsValid(Enemy) && !UCataclysmSkillEffects::IsDead(Enemy)
			&& Enemy->PaysForItsDeath())
		{
			++Standing;
		}
	}
	return DivineResurgenceFallen + Standing;
}

void ACataclysmDungeonGameMode::NoteDeathForDivineResurgence(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::DivineResurgenceKey))
		|| bDivineResurgenceDone)
	{
		return;
	}

	// A MARKED CREATURE IS NOT THE FLOOR'S TO COUNT OR TO RAISE. See the declaration.
	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Fallen || !Fallen->PaysForItsDeath())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// COUNTED WHATEVER KILLED IT: the row says "all defeated enemies", not the player's
	// kills. RECORDED only if it is one of the kinds this dungeon places, because
	// nothing else can be put back; the sandbox's plain creature is counted and not
	// recorded, the same answer Vengeful Wraiths gives it.
	++DivineResurgenceFallen;
	const ECataclysmDungeonCreature Kind = DungeonGameModeKindOf(Fallen);
	if (Kind != ECataclysmDungeonCreature::Count)
	{
		FDivineResurgenceGrave Grave;
		Grave.Location = Notice.Location;
		Grave.Kind = Kind;
		Grave.RarityStep = Fallen->RarityStep;
		DivineResurgenceGraves.Add(Grave);
	}

	const int32 Placed = DivineResurgencePlacedCount();
	if (!Effects::DivineResurgenceIsDue(DivineResurgenceFallen, Placed))
	{
		RefreshFloorModifierPanel();
		return;
	}

	// ONCE: decided before anything is raised, so a creature raised below that died at
	// once could not bring on a second revival from inside this one.
	bDivineResurgenceDone = true;

	for (const FDivineResurgenceGrave& Grave : DivineResurgenceGraves)
	{
		FCataclysmEnemyPlacement Placement;
		Placement.Cell = CurrentFloor->CellOfWorld(Grave.Location);
		Placement.Creature = Grave.Kind;

		// AT THE RUNG IT DIED AT, set before its modifiers are drawn; see
		// `SpawnPlacedCreature`. Its modifiers are drawn afresh for that rung: which
		// ones it carried before is not recorded, a judgement under the owner's
		// delegation written in the decisions entry.
		ACataclysmEnemyCharacter* Risen = SpawnPlacedCreature(
			Placement, FloorBrief.SightRadiusMultiplier, Grave.RarityStep);
		if (!Risen)
		{
			continue;
		}
		Risen->bRisenFromTheDead = true;
		FloorEnemies.Add(Risen);

		// "AT HALF HEALTH": half of its maximum, written after the rung, because setting
		// a rung refills health. Its energy shield is left as its rung gives it; the
		// row names health only.
		if (UAbilitySystemComponent* Abilities = Risen->GetAbilitySystemComponent())
		{
			Abilities->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(),
				Effects::DivineResurgenceHealthFor(Abilities->GetNumericAttribute(
					UCataclysmVitalAttributeSet::GetMaxHealthAttribute())));
		}
		++DivineResurgenceRisen;
	}

	UE_LOG(LogCataclysm, Log,
		   TEXT("Divine Resurgence: %d of %d fell, and %d rose at %.0f%% health"),
		   DivineResurgenceFallen, Placed, DivineResurgenceRisen,
		   Effects::DivineResurgenceHealthPercent);
	DivineResurgenceGraves.Reset();
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepMarchOfProgress(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player))
	{
		return;
	}

	// ONE FIGURE FOR THE WHOLE FLOOR, WORKED OUT ONCE. Every creature on the floor is
	// given the same multiplier, which is what "each floor, enemies damage increases"
	// says: it is a fact about the floor and not about any creature.
	//
	// THE BRIEF'S FLOOR NUMBER AND NOT `FloorNumber`, which is the number every other
	// per-floor rule reads through `PlayerEffectsFor`. For a Horde dungeon the brief's
	// number is the wave, so all of them agree about what a floor is rather than this
	// one disagreeing.
	const float Multiplier =
		Effects::MarchOfProgressDamageMultiplierOnFloor(FloorBrief.FloorNumber);

	// EVERY CREATURE ON THE PLAYER'S OTHER SIDE, WHEREVER IT CAME FROM, which is the
	// sweep `StepRavenousHoard` makes and for its reasons: `IsHostileTo` also turns away
	// the dead, and the player's minions are `ACataclysmMinion`, which this never
	// iterates.
	//
	// THE SETTER RETURNS AT ONCE WHEN THE MULTIPLIER HAS NOT CHANGED, so after the first
	// beat of a floor this writes nothing and the sweep is the whole cost.
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		ACataclysmEnemyCharacter* Creature = *It;
		if (!IsValid(Creature) || !UCataclysmTargeting::IsHostileTo(Creature, Player))
		{
			continue;
		}

		Creature->SetFloorDepthDamageMultiplier(Multiplier);
	}

	// AND THE ARMOUR THE PLAYER HAS EARNED, PUT ON ONLY WHEN IT HAS MOVED. The apply
	// rewrites the character's whole standing stat line, so doing it four times a second
	// for a figure that changes on a kill would be waste. Death's Embrace makes the same
	// guard for the same reason.
	const float Owed =
		Effects::MarchOfProgressArmourMorePercentFor(MarchOfProgressCommandersKilled);
	if (!FMath::IsNearlyEqual(Owed, MarchOfProgressArmourApplied))
	{
		MarchOfProgressArmourApplied = Owed;
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}
}

void ACataclysmDungeonGameMode::StepCommandersAura(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player))
	{
		return;
	}

	// THE TAG IS LOOKED UP ONCE AND NOT ONCE PER CREATURE, and a missing tag says so once
	// rather than failing silently. This is the route all four things that grant this buff
	// take: `StatusTagFor` resolves the vocabulary in
	// `game/Config/Tags/CataclysmTags.ini`.
	const FGameplayTag Empowered =
		UCataclysmSkillShapes::StatusTagFor(TEXT("Commander"));
	if (!Empowered.IsValid())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("Commander's Aura cannot empower: there is no Status.Buff.Commander tag. "
				 "See game/Config/Tags/CataclysmTags.ini and "
				 "tools/generate_gameplay_tags.py."));
		return;
	}

	// EVERY CREATURE ON THE PLAYER'S OTHER SIDE AT ELITE OR ABOVE, WHEREVER IT CAME FROM.
	// The sweep `StepRavenousHoard` and `StepMarchOfProgress` make, for their reasons:
	// `IsHostileTo` also turns away the dead, and the player's minions are
	// `ACataclysmMinion`, which this never iterates.
	int32 Commanders = 0;
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		ACataclysmEnemyCharacter* Commander = *It;
		if (!IsValid(Commander)
			|| !UCataclysmTargeting::IsHostileTo(Commander, Player)
			|| !Effects::CommandersAuraCommandsAtRung(Commander->RarityStep))
		{
			continue;
		}

		++Commanders;

		// ITS ALLIES AND NOT ITS ENEMIES, ASKED ON THE CREATURE'S BEHALF.
		// `FindAlliesInSphere` decides sides from the INSTIGATOR it is given rather than
		// from whoever calls it, so passing the commanding creature gives that creature's
		// allies. It excludes the instigator, which is the whole of "a commander does not
		// buff itself", and it refuses corpses, so a dead ally is not buffed.
		const TArray<AActor*> Allies = UCataclysmTargeting::FindAlliesInSphere(
			World, Commander, Commander->GetActorLocation(),
			Effects::CommandersAuraRadiusCm);

		for (AActor* Ally : Allies)
		{
			// REFRESHED RATHER THAN STACKED. `ApplyTagForDuration` keeps one effect per
			// tag, so a creature standing between two commanders -- or in a Hallowed
			// Groundfall crater as well -- is 20% faster and not 44%.
			//
			// THE COMMANDER IS THE FIRST ARGUMENT AND THE ALLY THE SECOND. The order is
			// (Instigator, Target) and NOT (Target, Instigator); the two neighbouring
			// grants of this same buff pass the same actor twice, so neither tells the
			// order apart. `ACataclysmSuccubusCharacter::PulseDominion` is the one that
			// does: it passes `this, Ally`. Written the other way round this buffs the
			// commander and nothing else, which compiles and looks right.
			UCataclysmSkillEffects::ApplyTagForDuration(
				Commander, Ally, Empowered, Effects::CommandersAuraGrantSeconds);
		}
	}

	if (Commanders != CommandersAuraCommanders)
	{
		CommandersAuraCommanders = Commanders;
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::StepAntiMagicZones(
	ACataclysmPlayerCharacter* Player, UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	// WHAT IS STILL STANDING, ASKED RATHER THAN REMEMBERED. A zone destroys itself when
	// its life ends, so a weak pointer going invalid IS the expiry. The count before and
	// after is what tells the floor panel to say so.
	const int32 StandingBefore = AntiMagicZones.Num();
	AntiMagicZones.RemoveAll([](const TWeakObjectPtr<ACataclysmGroundZone>& Zone)
	{
		return !Zone.IsValid();
	});

	// THE LOCK FIRST, FROM WHAT EXISTS, ON EVERY BEAT. See the declaration: a lock decided
	// only when ground is laid would follow the player off it.
	//
	// EACH ZONE IS ASKED WHETHER IT COVERS THE PLAYER, the same question
	// `StepSingularityWells` asks, so the ground the player sees and the ground that
	// locks cannot disagree about where it is. ONE ZONE OR THREE MAKE NO DIFFERENCE: a
	// spell is refused or it is not.
	const FVector Feet = Player->GetActorLocation();
	bool bInsideAZone = false;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Zone : AntiMagicZones)
	{
		if (Zone.IsValid() && Zone->Covers(Feet))
		{
			bInsideAZone = true;
			break;
		}
	}

	// ONLY WHEN SOMETHING CHANGED, the guard every beat-driven rule here keeps: the apply
	// rewrites the character's whole standing stat line.
	const float Wanted = Effects::SpellsLockedWhile(bInsideAZone);
	if (!FMath::IsNearlyEqual(Wanted, AntiMagicZonesLockApplied))
	{
		AntiMagicZonesLockApplied = Wanted;
		UE_LOG(LogCataclysm, Log,
			   TEXT("Anti-Magic Zones: the player's spells are %s."),
			   bInsideAZone ? TEXT("locked, standing in a zone")
							: TEXT("usable again, standing in no zone"));
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}

	// AND NOW WHETHER TO LAY ANOTHER. The cap is asked inside the predicate, before its
	// clock, so a floor at its limit does not swallow the count.
	AntiMagicZonesSecondsSinceLastZone += SecondsBetweenWaveChecks;
	if (Effects::AntiMagicZoneIsDue(AntiMagicZonesSecondsSinceLastZone,
									AntiMagicZones.Num()))
	{
		// THE ZONE IS DRAWN IN THE ROW'S OWN TYPE, read off the row rather than written
		// here, for the reason `StepInfernalRain` gives: a row retyped in the workbook
		// retypes its ground with no code change. An unreadable table lays nothing.
		const FCataclysmDungeonModifierRow* Row = UCataclysmDungeonModifierTable::FindRow(
			UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
			FName(Effects::AntiMagicZonesKey));
		if (Row)
		{
			// NEAR THE PLAYER AND PAST ITS OWN RADIUS, as Judgment Zones lays its ground:
			// near, or nobody meets it; past the radius, because ground that refuses a
			// spell the instant it appears under the player is not ground to walk out of.
			const FVector Centre = Player->GetActorLocation();
			const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
			const float Away = FMath::FRandRange(Effects::AntiMagicZonesRadiusCm + 1.0f,
												 Effects::AntiMagicZonesFallsWithinCm);
			const FVector Where(Centre.X + Away * FMath::Cos(Angle),
								Centre.Y + Away * FMath::Sin(Angle), Centre.Z);

			// THE SOURCE IS MADE LAST, because `ForFloor` spawns one when the floor has
			// none. Declared in this exact form for
			// `test_every_ground_zone_the_game_mode_places_is_owned_by_the_hazard_source`:
			// a floor change destroys a rule's zones BY THEIR OWNER (issue #1925).
			ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(
				World);
			if (Source)
			{
				// NO DAMAGE, which issue #1701 made possible so Singularity Wells could
				// have a well that slows without damaging.
				if (ACataclysmGroundZone* Zone = ACataclysmGroundZone::Spawn(
						Source, Where, Effects::AntiMagicZonesRadiusCm,
						Effects::AntiMagicZonesSeconds, /*DamagePerTick=*/0.0f,
						FName(*Row->CataclysmType)))
				{
					AntiMagicZones.Add(Zone);
					AntiMagicZonesSecondsSinceLastZone = 0.0f;
				}
				// THE CLOCK IS NOT RESET ON A FAILED SPAWN, so the next beat tries again.
			}
		}
	}

	if (AntiMagicZones.Num() != StandingBefore)
	{
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::NoteDeathForMarchOfProgress(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::MarchOfProgressKey)))
	{
		return;
	}

	// THE FLOOR'S COMMANDER AND NOBODY ELSE. A weak pointer that has gone invalid cannot
	// match a victim, so a floor whose Commander was destroyed rather than killed pays
	// nothing, which is right: the row pays for killing it.
	if (bMarchOfProgressCommanderSlain || !MarchOfProgressCommander.IsValid()
		|| Notice.Victim != MarchOfProgressCommander.Get())
	{
		return;
	}

	// THIS FLOOR'S COMMANDER IS GONE, WHOEVER KILLED IT, AND THE FLOOR DOES NOT GET
	// ANOTHER. The row names "the Commander in each level" -- one creature a floor -- so
	// a Commander that burns to death on another rule's ground or is killed by another
	// creature takes the floor's Commander with it. Recorded BEFORE the question of who
	// struck the blow, because that question decides the payment and not whether the
	// Commander is still there to kill.
	//
	// WITHOUT THIS THE FLOOR WOULD QUIETLY OFFER A SECOND ONE. The chooser also returns
	// early while the pointer is still valid, and a creature killed in a test world stays
	// valid because nothing runs the timer that removes its body -- so the fault would
	// not show until real play, where the body does go.
	bMarchOfProgressCommanderSlain = true;
	RefreshFloorModifierPanel();

	// AND THE PLAYER IS PAID ONLY IF THEY STRUCK THE LAST BLOW. "Killing the Commander"
	// is the row. `NoteDeathForDemonPrince` and three other listeners ask the same
	// question the same way, for rows that say "when you kill".
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player || Notice.Killer != Player)
	{
		return;
	}

	++MarchOfProgressCommandersKilled;

	UE_LOG(LogCataclysm, Verbose,
		TEXT("The floor's Commander was slain; %d this run, armour %.0f%% more."),
		MarchOfProgressCommandersKilled,
		Effects::MarchOfProgressArmourMorePercentFor(MarchOfProgressCommandersKilled));

	// THE PANEL IS REFRESHED A SECOND TIME, AND IT IS NOT A DUPLICATE. Its line carries
	// two things this function can change: whether the floor's Commander is still standing,
	// which the refresh above reports, and how many commanders the player has killed in
	// this run, which only becomes true on the line above this one. Refreshing once, at
	// either point, would leave one of the two a beat stale.
	//
	// AND THE ARMOUR ITSELF GOES ON AT THE NEXT BEAT rather than here. It is applied
	// through `ApplyChangingFloorEffects`, which needs the player's ability system; the
	// beat has it in hand and this does not, and a quarter of a second is what every other
	// recorded-here-applied-there rule already waits.
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteDeathForBloodForgedChampions(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vitals = UCataclysmVitalAttributeSet;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::BloodForgedChampionsKey)))
	{
		return;
	}

	ACataclysmEnemyCharacter* Fallen = Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Fallen)
	{
		return;
	}

	// NO KILLER IS ASKED FOR. The row says "nearby dying allies" and names nobody, so a
	// creature killed by another creature, by burning ground or by another floor rule
	// feeds a champion exactly as the player's own kill does. The four listeners above
	// each ask `Notice.Killer != Player` because their rows say "when you kill"; a check
	// here would be their habit carried into a row that does not have it.

	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!World || !Player)
	{
		return;
	}

	// THE NEAREST CHAMPION WITHIN THE REACH, AND THE SEARCH IS MADE FROM THE PLAYER with
	// the body's location as its centre. `UCataclysmTargeting::FindEnemiesInSphere` decides
	// sides from the actor handed to it, so passing the corpse would find the player.
	// `NoteDeathForEpidemic` above records the same trap.
	ACataclysmEnemyCharacter* Champion = nullptr;
	float NearestAway = TNumericLimits<float>::Max();
	for (AActor* Found : UCataclysmTargeting::FindEnemiesInSphere(
			 World, Player, Notice.Location, Effects::BloodForgedChampionsRadiusCm()))
	{
		ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Found);

		// THE CREATURE THAT DIED IS NOT A CANDIDATE. It is still standing in the sphere at
		// the moment its own death is announced.
		if (!IsValid(Creature) || Creature == Fallen)
		{
			continue;
		}

		// ELITE OR ABOVE, AND NOT ALREADY AT THE CEILING. Both ends are asked in one place,
		// `BloodForgedChampionsAbsorbs`, so a creature at Herald is refused here rather than
		// fed and then found to have nowhere to rise.
		if (!Effects::BloodForgedChampionsAbsorbs(Creature->RarityStep))
		{
			continue;
		}

		const float Away = FVector::Dist(Creature->GetActorLocation(), Notice.Location);
		if (Away < NearestAway)
		{
			NearestAway = Away;
			Champion = Creature;
		}
	}

	// THE DESTROYED ARE FORGOTTEN, so the record does not grow from floor to floor. A
	// creature that has died keeps its entry until the actor itself is destroyed, which
	// costs nothing: a corpse feeds nobody and rises no further. Royal Guard's record is
	// kept the same way.
	for (auto Entry = BloodForgedChampionsFed.CreateIterator(); Entry; ++Entry)
	{
		if (Entry->Key.IsStale())
		{
			Entry.RemoveCurrent();
		}
	}

	if (!Champion)
	{
		return;
	}

	const int32 Fed = BloodForgedChampionsFed.FindOrAdd(Champion) + 1;
	BloodForgedChampionsFed[Champion] = Fed;
	++BloodForgedChampionsAbsorbed;

	if (!Effects::BloodForgedChampionsRungIsEarned(Fed))
	{
		UE_LOG(LogCataclysm, Log,
			   TEXT("Blood-Forged Champions: %s died beside %s, which has taken %d of %d"),
			   *Fallen->GetName(), *Champion->GetName(), Fed,
			   Effects::BloodForgedChampionsDeathsPerRung);
		RefreshFloorModifierPanel();
		return;
	}

	UAbilitySystemComponent* Abilities = UCataclysmTargeting::AbilitySystemOf(Champion);
	if (!Abilities)
	{
		return;
	}

	const int32 Rung = Effects::BloodForgedChampionsRungAfter(Champion->RarityStep);

	// WHAT IT HAD IN BOTH POOLS, READ BEFORE ANYTHING IS WRITTEN. `SetRarityStep` and
	// `DrawModifiersForRarity` both end in `ApplyStartingAttributes`, which refills health
	// and energy shield to the new maximums. THE SAME DECISION `StepVolatileEvolution`
	// MAKES, and for the same reason its header gives: a champion that healed itself every
	// third death would undo the work the player had already done on it.
	const float Health = Abilities->GetNumericAttribute(Vitals::GetHealthAttribute());
	const float Shield = Abilities->GetNumericAttribute(Vitals::GetEnergyShieldAttribute());

	Champion->SetRarityStep(Rung);

	// AND THE MODIFIERS THE NEW RUNG CARRIES. `DrawModifiersForRarity` draws only the
	// shortfall and never draws one the creature already holds.
	Champion->DrawModifiersForRarity();

	// NOW PUT BOTH POOLS BACK, HELD TO THE NEW MAXIMUMS, which are read again because the
	// rung is what moved them. A champion is therefore proportionally MORE wounded at its
	// new rung than it was at its old one, which is what keeping the amount means.
	Abilities->SetNumericAttributeBase(
		Vitals::GetHealthAttribute(),
		FMath::Min(Health,
				   Abilities->GetNumericAttribute(Vitals::GetMaxHealthAttribute())));
	Abilities->SetNumericAttributeBase(
		Vitals::GetEnergyShieldAttribute(),
		FMath::Min(Shield,
				   Abilities->GetNumericAttribute(
					   Vitals::GetMaxEnergyShieldAttribute())));

	// AND IF IT IS A WRAITH, THE FIGURES THAT MAKE IT ONE GO BACK ON. The two calls above
	// end in `ApplyStartingAttributes`, which has just written this creature's whole stat
	// block over with its new rung's own. Without this a floor carrying both rows would
	// strip a wraith of everything but its name the first time it was fed.
	ApplyVengefulWraithFigures(Champion);
	ApplyNothingIsForgottenFigures(Champion);
	ApplySoulHarvestFigures(Champion, /*bFreshBlock=*/true);

	// AND ITS TALLY STARTS AGAIN, so the next rung costs the same as this one did.
	BloodForgedChampionsFed[Champion] = 0;
	++BloodForgedChampionsRungsGained;

	UE_LOG(LogCataclysm, Log,
		   TEXT("Blood-Forged Champions: %s took %d death(s) and rose to rarity step %d, "
				"keeping %.1f health and %.1f energy shield"),
		   *Champion->GetName(), Effects::BloodForgedChampionsDeathsPerRung, Rung, Health,
		   Shield);

	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::EpidemicEndTheChain(
	const FVector& Where, ECataclysmDungeonCreature LastVictimsKind)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!World || !Player)
	{
		return;
	}

	// THE FLAG IS SET FOR THE WHOLE OF THIS, deaths and spawn together, because every
	// death below is announced and would otherwise come back to the listener.
	bEpidemicKilling = true;

	int32 Killed = 0;
	for (AActor* Found : UCataclysmTargeting::FindEnemiesInSphere(
			 World, Player, Where, Effects::EpidemicRadiusCm()))
	{
		ACataclysmEnemyCharacter* Creature = Cast<ACataclysmEnemyCharacter>(Found);
		if (!IsValid(Creature) || UCataclysmSkillEffects::IsDead(Creature))
		{
			continue;
		}

		// HEALTH TO ZERO AND THEN `HandleDeath`, WHICH IS A REAL DEATH. That pair is what
		// `UCataclysmHealthDebt` uses, and it is what makes the loot roll and the
		// experience grant in the creature's own handler run. Marking it dead instead
		// would announce the death and pay nothing.
		if (UAbilitySystemComponent* Abilities =
				UCataclysmTargeting::AbilitySystemOf(Creature))
		{
			Abilities->SetNumericAttributeBase(
				UCataclysmVitalAttributeSet::GetHealthAttribute(), 0.0f);
		}
		Creature->HandleDeath();
		++Killed;
	}

	// AND ONE PLAGUE LORD, OF THE LAST VICTIM'S KIND, ONCE A FLOOR. A creature of no kind
	// brings none, which is the refusal Royal Guard and Demon Prince both make.
	int32 Risen = 0;
	if (EpidemicPlagueLordsRisen < Effects::EpidemicPlagueLordsPerFloor
		&& LastVictimsKind != ECataclysmDungeonCreature::Count
		&& CurrentFloor && CurrentFloor->IsBuilt())
	{
		FCataclysmEnemyPlacement Placement;
		Placement.Cell = CurrentFloor->CellOfWorld(Where);
		Placement.Creature = LastVictimsKind;

		if (ACataclysmEnemyCharacter* Lord =
				SpawnPlacedCreature(Placement, FloorBrief.SightRadiusMultiplier))
		{
			Lord->SetRarityStep(Effects::EpidemicPlagueLordRung);
			Lord->DrawModifiersForRarity();
			FloorEnemies.Add(Lord);
			++EpidemicPlagueLordsRisen;
			Risen = 1;
		}
	}

	// AND THE CHAIN IS OVER. It starts again from nothing, so a floor may have another.
	EpidemicChain = 0;
	bEpidemicKilling = false;

	UE_LOG(LogCataclysm, Log,
		   TEXT("Epidemic: a chain of %d killed %d creature(s) within %.0f cm and brought "
				"%d Plague Lord(s)"),
		   Effects::EpidemicSpreadsToKill, Killed, Effects::EpidemicRadiusCm(), Risen);
}

void ACataclysmDungeonGameMode::OnSomethingWasHit(
	const FCataclysmHitNotice& Notice)
{
	// ONE ANNOUNCEMENT, EVERY RULE THAT WANTS IT, EACH TESTING FOR ITS OWN ROW.
	// Issues #1786 and #41. Shaped like `OnSomethingDied` above rather than
	// holding one rule's logic behind an early return on its key.
	//
	// THIS SAID "two further rows ... describe a blow" AND NAMED A COUNT RATHER
	// THAN THE ROWS. Reading the table on 2026-09-14 while building the second
	// listener, the rows that want this announcement are
	// `Celestial_Holy_Repercussions`, `Demonic_Brand_of_the_Aggressor` and
	// `Pestilence_Contagious_Touch` -- three, not two. Naming them instead of
	// counting them is what stops the line going stale again, and it is what
	// `CataclysmDungeonModifierEffects.cpp` already does where it says "Count the
	// arms rather than reading a number here."
	NoteHitForWastingSickness(Notice);
	NoteHitForBrandOfTheAggressor(Notice);
	NoteHitForHolyRepercussions(Notice);
	NoteHitForTheReaper(Notice);
	NoteHitForPlagueConvergence(Notice);
}

void ACataclysmDungeonGameMode::NoteHitForWastingSickness(
	const FCataclysmHitNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::WastingSicknessKey)))
	{
		return;
	}

	// A BLOW THAT ACTUALLY LANDED. `Landed` is what reached the target after
	// every mitigation step, so an evaded or wholly stopped blow is not a chance
	// to inflict anything -- which is what keeps the row's chance meaning what it
	// says rather than being a chance per swing.
	if (Notice.Landed <= 0.0f)
	{
		return;
	}

	// AND IT MUST HAVE LANDED ON THE PLAYER. This is announced for every blow on
	// the floor, the player's own included, and the row says "Enemies have a
	// chance to inflict".
	//
	// THE SAME ROUTE TO THE PLAYER THE BEAT TAKES, so the two cannot disagree
	// about whose floor this is.
	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
	if (!Player || Notice.Target != Player)
	{
		return;
	}

	// ALREADY AT THE CAP COSTS A ROLL AND NOTHING ELSE, and the roll is still
	// drawn so that pinning it in a test cannot change how many rolls happen.
	const bool bInflicts =
		DungeonGameModeWastingSicknessRoll() < Effects::WastingSicknessChancePercentPerHit;

	// THE COUNT IS THE WHOLE STATE AND THE BEAT APPLIES IT, within a quarter of a
	// second, which is what both death listeners already do.
	WastingSicknessStacks =
		Effects::WastingSicknessStacksAfterHit(WastingSicknessStacks, bInflicts);

	// AND THE PANEL LEARNS THE NEW COUNT. Unconditionally rather than only when
	// the roll inflicted something: `WastingSicknessStacksAfterHit` answers the
	// old count for a blow that did not stack, so a refresh that did nothing is
	// cheaper to reason about than a branch that decides whether to make one.
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::NoteDeathForWastingSickness(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::WastingSicknessKey)))
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player = Controller
		? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn())
		: nullptr;
	if (!Player)
	{
		return;
	}

	// THE PLAYER'S OWN DEATH CLEARS IT, AND CLEARS IT NOW. The project owner's
	// ruling of 2026-09-10 ends anything that lasts only for a dungeon at a
	// death, and this row is one of the five it names.
	//
	// APPLIED HERE RATHER THAN LEFT TO THE BEAT, which is the one place in this
	// file where waiting a quarter of a second would be observable.
	// `ACataclysmPlayerCharacter::Revive` refills the vitals and the refill READS
	// the maximums, so a beat that had not yet run would refill the player to the
	// lowered maximum and then lift it, leaving them standing up short of full.
	if (Notice.Victim == Player)
	{
		if (WastingSicknessStacks != 0 || WastingSicknessStacksApplied != 0)
		{
			WastingSicknessStacks = 0;
			WastingSicknessStacksApplied = 0;
			ApplyChangingFloorEffects(
				Player,
				Cast<UCataclysmAbilitySystemComponent>(
					Player->GetAbilitySystemComponent()));
		}
		return;
	}

	// AND A BOSS'S DEATH CLEARS IT, WHICH IS THE ROW'S OWN CURE: "can only be
	// removed by defeating a floor boss".
	//
	// THE VICTIM'S OWN RARITY, NOT THE NOTICE'S BOSS FACT, for the reason The
	// Nihil's Embrace's cleanse gives above: that fact says whether a boss DEALT
	// the last blow, and this asks about who died.
	//
	// A FLOOR'S BOSS AS `DiedAsAFloorsBoss` ANSWERS IT: a Gatekeeper, or any creature at the
	// Boss rung. Until 2026-09-23 this asked the rung alone, a judgement that rested on
	// nothing marking the creature at a floor's exit; the Gatekeeper draws its rung like
	// every creature, so that made the cure a 1% draw. Ruled by the coordinating session
	// under the owner's delegation; `docs/DECISIONS.md` has both entries.
	if (!DiedAsAFloorsBoss(Notice.Victim))
	{
		return;
	}

	// LEFT TO THE BEAT, UNLIKE THE DEATH ABOVE. Nothing reads the player's
	// maximums in the moment a creature dies, so a quarter of a second is not
	// observable and the shared applier is reached the ordinary way.
	WastingSicknessStacks = 0;
}

void ACataclysmDungeonGameMode::NoteDeathForNihilsEmbrace(
	const FCataclysmDeathNotice& Notice)
{
	if (!FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::NihilsEmbraceKey)))
	{
		return;
	}

	// THE VICTIM'S OWN RARITY, NOT THE NOTICE'S BOSS FACT. That fact says whether
	// a boss DEALT the last blow, and this row asks about who died.
	//
	// A BOSS OR A CATACLYSM BOSS, AND THAT IS A JUDGEMENT recorded in
	// `docs/DECISIONS.md`. It is the line the game already draws for a boss, and a
	// Herald sits below it deliberately -- a mini-boss the player meets often --
	// so a cleanse a Herald satisfied would be routine rather than the objective
	// the row asks for.
	const ACataclysmEnemyCharacter* Died =
		Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Died || !Died->IsBoss())
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	const ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	const UCataclysmAbilitySystemComponent* AbilitySystem = Player
		? Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent())
		: nullptr;
	if (!World || !AbilitySystem)
	{
		return;
	}

	// EVERY POINT BACK AT ONCE, by moving the baseline up to where the character
	// has walked to, and a bounded reward on top. The beat applies both within a
	// quarter of a second.
	MetresWalkedAtLastCleanse = AbilitySystem->MetresWalkedTotal();
	NihilsEmbraceRewardUntilSeconds =
		World->GetTimeSeconds()
		+ UCataclysmDungeonModifierEffects::NihilsEmbraceRewardSeconds;
}

void ACataclysmDungeonGameMode::RefreshFloorModifierPanel()
{
	using Effects = UCataclysmDungeonModifierEffects;

	// THE SAME ROUTE TO THE CONTROLLER `ApplyFloorRulesToPlayer` TAKES, so the
	// floor-change draw and the on-a-blow draw cannot reach different panels.
	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerController* Cataclysm =
		Cast<ACataclysmPlayerController>(Controller);
	if (!Cataclysm)
	{
		return;
	}

	Cataclysm->ShowFloorModifiers(FloorBrief.Modifiers, FloorBrief.FloorNumber,
								  LiveCountsForTheFloor());
}

TMap<FName, FString> ACataclysmDungeonGameMode::LiveCountsForTheFloor() const
{
	using Effects = UCataclysmDungeonModifierEffects;

	// "N of M" FOR EVERY ROW, WHICH DIFFERS FROM THE PLAIN COUNT FIRST ASKED FOR ON
	// WASTING SICKNESS, and the reason is that it has an M.
	// `WastingSicknessMostStacks` is 5 and that row says its debuff stacks, so a
	// player reading "2" cannot tell whether that is nearly all of it or a fifth
	// of it. Every row reads alike and no number is bare.
	TMap<FName, FString> Counting;

	const FName Brand(Effects::BrandOfTheAggressorKey);
	if (FloorBrief.Modifiers.Contains(Brand))
	{
		Counting.Add(Brand, FString::Printf(TEXT("%d of %d"), BrandStacks,
											Effects::BrandStacksToErupt));
	}

	// AND JUDGMENT, WHICH A PLAYER CANNOT SEE ANY OTHER WAY. Issues #1820 and
	// #41. A lowered resistance shows up only as damage arriving harder, and the
	// floor panel prints the row's description word for word -- so without this
	// a player reads that a debuff stacks and never learns how much they carry.
	const FName Holy(Effects::HolyRepercussionsKey);
	if (FloorBrief.Modifiers.Contains(Holy))
	{
		Counting.Add(Holy, FString::Printf(
			TEXT("%d of %d"), JudgmentStacks,
			Effects::HolyRepercussionsJudgmentMostStacks));
	}

	// AND WINGS OF THE HOST: when the next flyover comes, or that its feathers are falling.
	// Issues #1820 and #41.
	const FName Wings(Effects::WingsOfTheHostKey);
	if (FloorBrief.Modifiers.Contains(Wings))
	{
		const int32 Falling = WingsOfTheHostMarksNow().Num();
		Counting.Add(Wings, Falling > 0
			? FString::Printf(TEXT("wings of the host: %d feathers falling"), Falling)
			: FString::Printf(TEXT("wings of the host: next flyover in %.0f seconds"),
							  FMath::Max(0.0f, Effects::WingsOfTheHostSecondsBetween
												- WingsOfTheHostSecondsSinceLast)));
	}

	// AND ETERNAL CHORUS: how many sources are still singing. Issues #1820 and #41.
	const FName Chorus(Effects::EternalChorusKey);
	if (FloorBrief.Modifiers.Contains(Chorus))
	{
		Counting.Add(Chorus, FString::Printf(TEXT("eternal chorus: %d sources singing"),
											  EternalChorusSourcesNow().Num()));
	}

	// AND TRIAL OF ENDURANCE: its seconds and the creatures left while it runs, then how it ended; a Horde
	// floor has no timer. Issues #1820 and #41.
	const FName Trial(Effects::TrialOfEnduranceKey);
	if (FloorBrief.Modifiers.Contains(Trial))
	{
		FString Line;
		if (FloorBrief.bWaveWalksIn)
		{
			Line = TEXT("trial of endurance: no timer on a Horde floor");
		}
		else if (bTrialRanOut)
		{
			Line = TEXT("trial of endurance: failed; enemies deal double damage and have double resistances");
		}
		else if (bTrialClearedInTime)
		{
			Line = TEXT("trial of endurance: cleared in time");
		}
		else
		{
			Line = FString::Printf(TEXT("trial of endurance: %.0f seconds left; %d creatures left"),
								   FMath::Max(0.0f, Effects::TrialOfEnduranceSeconds - TrialSeconds),
								   LivingFloorEnemies());
		}
		Counting.Add(Trial, Line);
	}

	// AND RAW SEWAGE: the stacks held, what they burn, and what cleanses them. Issues #1820 and #41.
	const FName Sewage(Effects::RawSewageKey);
	if (FloorBrief.Modifiers.Contains(Sewage))
	{
		Counting.Add(Sewage, RawSewageStacks > 0
			? FString::Printf(TEXT("raw sewage: %d disease stack%s, %.1f%% of maximum health a second; a floor's boss "
								   "cleanses them"),
							  RawSewageStacks, RawSewageStacks == 1 ? TEXT("") : TEXT("s"),
							  Effects::RawSewagePercentPerSecond(RawSewageStacks))
			: FString(TEXT("raw sewage: no disease stacks")));
	}

	// AND SWARM OF LOCUSTS: the next swarm's seconds, or that one is crossing and a shelter stops it. Issues #1820
	// and #41.
	const FName Locusts(Effects::SwarmOfLocustsKey);
	if (FloorBrief.Modifiers.Contains(Locusts))
	{
		Counting.Add(Locusts, SwarmOfLocusts.IsValid()
			? FString(TEXT("swarm of locusts: a swarm is crossing; shelter stops it"))
			: FString::Printf(TEXT("swarm of locusts: next in %d s"),
							  FMath::Max(0, FMath::CeilToInt(Effects::SwarmOfLocustsSecondsBetween
															  - SwarmOfLocustsSecondsSinceLast))));
	}

	// AND ABYSSAL RIFTS: the rift's state, and what the successes have earned. Issues #1820 and #41.
	const FName Rifts(Effects::AbyssalRiftsKey);
	if (FloorBrief.Modifiers.Contains(Rifts))
	{
		const int32 Earned = FMath::RoundToInt(Effects::AbyssalRiftsMagicFindFor(AbyssalRiftSuccesses));
		if (AbyssalRiftState == ERiftState::Open)
		{
			Counting.Add(Rifts, FString::Printf(TEXT("abyssal rifts: open, %d s left, %d creatures left"),
				FMath::Max(0, FMath::CeilToInt(Effects::AbyssalRiftsSecondsToClose - AbyssalRiftSecondsOpen)),
				AbyssalRiftCreaturesStanding().Num()));
		}
		else
		{
			const TCHAR* Where = FloorBrief.bWaveWalksIn ? TEXT("no rift on a Horde arena")
				: (AbyssalRiftState == ERiftState::Closed ? TEXT("the rift is closed") : TEXT("a rift waits"));
			Counting.Add(Rifts, FString::Printf(TEXT("abyssal rifts: %s; %d closed in time, +%d magic find"), Where,
												AbyssalRiftSuccesses, Earned));
		}
	}

	// AND THE INFESTED HOARD: the stacks, what they drain, and the chance of infested loot. Issues #1820 and #41.
	// NAMED InfestedRow AND NOT Hoard: Ravenous Hoard's block in this function already declares `Hoard`.
	const FName InfestedRow(Effects::InfestedHoardKey);
	if (FloorBrief.Modifiers.Contains(InfestedRow))
	{
		const int32 Chance = FMath::RoundToInt(Effects::InfestedHoardChancePercentFor(InfestedHoardStacks));
		Counting.Add(InfestedRow, InfestedHoardStacks > 0
			? FString::Printf(TEXT("infested hoard: %d stack%s, %.1f%% of maximum health a second; %d%% chance of "
								   "infested loot"),
							  InfestedHoardStacks, InfestedHoardStacks == 1 ? TEXT("") : TEXT("s"),
							  Effects::InfestedHoardPercentPerSecond(InfestedHoardStacks), Chance)
			: FString::Printf(TEXT("infested hoard: no stacks; %d%% chance of infested loot"), Chance));
	}

	// AND PORTAL UNLEASHING: how many portals, and how many of the creatures they sent stand against the cap
	// summed over them. Issues #1820 and #41.
	const FName Portals(Effects::PortalUnleashingKey);
	if (FloorBrief.Modifiers.Contains(Portals))
	{
		int32 Standing = 0;
		for (const FVoidPortal& One : VoidPortals)
		{
			Standing += AbominationsOf(One.Portal.Get()).Num();
		}
		Counting.Add(Portals, FString::Printf(TEXT("portal unleashing: %d portal%s; %d of %d abominations standing"),
											  VoidPortals.Num(), VoidPortals.Num() == 1 ? TEXT("") : TEXT("s"), Standing,
											  VoidPortals.Num() * Effects::PortalUnleashingMostAlivePerPortal));
	}

	// AND PESTILENT EMPOWERMENT: this floor's beacons standing, what earlier floors left, and what later
	// floors would get if the player left now. Issues #1820 and #41.
	const FName Beacons(Effects::PestilentEmpowermentKey);
	if (FloorBrief.Modifiers.Contains(Beacons))
	{
		int32 Uncounted = 0;
		for (const FPlagueBeacon& One : PlagueBeacons)
		{
			const ACataclysmEnemyCharacter* Beacon = One.Beacon.Get();
			Uncounted += (IsValid(Beacon) && !UCataclysmSkillEffects::IsDead(Beacon) && !One.bCounted) ? 1 : 0;
		}
		const float ThisFloor =
			(Effects::PestilentEmpowermentDamageMultiplier(PestilentBeaconsLeftStanding) - 1.0f) * 100.0f;
		const float Later =
			(Effects::PestilentEmpowermentDamageMultiplier(PestilentBeaconsLeftStanding + Uncounted) - 1.0f) * 100.0f;
		Counting.Add(Beacons, FString::Printf(
			TEXT("pestilent empowerment: %d beacons here; this floor +%.0f%%; later floors +%.0f%%"),
			PlagueBeaconsStanding().Num(), ThisFloor, Later));
	}

	// AND INFESTED VEINS: how many stand, and how many have been destroyed against the guardians'
	// threshold, or that the guardians have come. Issues #1820 and #41.
	// AND OBSIDIAN SARCOPHAGI: the paid deaths beside each coffin against its threshold, or that its Vampire
	// Lord has come. Issues #1820 and #41.
	const FName Coffins(Effects::ObsidianSarcophagiKey);
	if (FloorBrief.Modifiers.Contains(Coffins))
	{
		// "ONE" AND "THE OTHER" FOR TWO COFFINS, AS PROPOSED; "IT" FOR A HORDE ARENA'S ONE. "Slain" once, in the
		// line's first count, as proposed.
		TArray<FString> Parts;
		for (int32 Which = 0; Which < Sarcophagi.Num(); ++Which)
		{
			const FSarcophagus& One = Sarcophagi[Which];
			const TCHAR* Name = Sarcophagi.Num() == 1 ? TEXT("it") : (Which == 0 ? TEXT("one") : TEXT("the other"));
			if (One.bLordCame)
			{
				Parts.Add(FString::Printf(TEXT("the Vampire Lord of %s has come"), Name));
			}
			else
			{
				Parts.Add(FString::Printf(TEXT("%d of %d %sbeside %s"), One.Deaths,
										  Effects::ObsidianSarcophagiDeathsForTheLord,
										  Which == 0 ? TEXT("slain ") : TEXT(""), Name));
			}
		}
		Counting.Add(Coffins, Parts.IsEmpty() ? FString(TEXT("obsidian sarcophagi: none on this floor"))
											  : TEXT("obsidian sarcophagi: ") + FString::Join(Parts, TEXT(", ")));
	}

	const FName Veins(Effects::InfestedVeinsKey);
	if (FloorBrief.Modifiers.Contains(Veins))
	{
		const int32 Standing = InfestedVeinsStanding().Num();
		Counting.Add(Veins, bInfestedVeinsGuardiansCame
			? FString::Printf(TEXT("infested veins: %d standing; %d destroyed; the guardians have come"),
							  Standing, InfestedVeinsDestroyed)
			: FString::Printf(TEXT("infested veins: %d standing; %d destroyed of %d before the guardians come"),
							  Standing, InfestedVeinsDestroyed, Effects::InfestedVeinsDestroyedBeforeGuardians));
	}

	// AND VOID PARASITE: how many voidlings the player carries, and what clears them. Issues #1820 and #41.
	const FName Parasite(Effects::VoidParasiteKey);
	if (FloorBrief.Modifiers.Contains(Parasite))
	{
		Counting.Add(Parasite, VoidParasiteStacks > 0
			? FString::Printf(TEXT("void parasite: %d attached (each %.0f%% less damage, resistances and movement "
								   "speed); a light zone clears them"),
							  VoidParasiteStacks, Effects::VoidParasitePercentPerStack)
			: FString(TEXT("void parasite: none attached")));
	}

	// AND GOLDEN SPIRES: how many stand. Issues #1820 and #41.
	const FName Spires(Effects::GoldenSpiresKey);
	if (FloorBrief.Modifiers.Contains(Spires))
	{
		Counting.Add(Spires, FString::Printf(TEXT("golden spires: %d standing"), GoldenSpiresStanding().Num()));
	}

	// AND NECROTIC BLOOM: how many flowers stand, and when the soonest of them sends its next wave,
	// while any has a wave left to send. Issues #1820 and #41.
	const FName Bloom(Effects::NecroticBloomKey);
	if (FloorBrief.Modifiers.Contains(Bloom))
	{
		const int32 Standing = NecroticBloomFlowersNow().Num();
		float Soonest = -1.0f;
		for (const FNecroticBloom& One : NecroticBlooms)
		{
			const ACataclysmEnemyCharacter* Flower = One.Flower.Get();
			if (IsValid(Flower) && !UCataclysmSkillEffects::IsDead(Flower)
				&& One.Waves < Effects::NecroticBloomMostWaves)
			{
				const float Left = FMath::Max(0.0f, Effects::NecroticBloomSecondsBetween - One.SecondsSinceLastWave);
				Soonest = Soonest < 0.0f ? Left : FMath::Min(Soonest, Left);
			}
		}
		Counting.Add(Bloom, Soonest >= 0.0f
			? FString::Printf(TEXT("necrotic bloom: %d flowers, next wave in %.0f seconds"), Standing, Soonest)
			: FString::Printf(TEXT("necrotic bloom: %d flowers"), Standing));
	}

	// AND PLAGUE HARBINGERS: how many are alive and how many trail patches stand. Issues #1820
	// and #41.
	const FName Harbingers(Effects::PlagueHarbingersKey);
	if (FloorBrief.Modifiers.Contains(Harbingers))
	{
		Counting.Add(Harbingers, FString::Printf(
			TEXT("plague harbingers: %d alive, %d trail patches"),
			PlagueHarbingersAlive().Num(), PlagueHarbingerTrailPatches()));
	}

	// AND ECHOES OF THE PAST: how many of the last floor's dead come back. Issues #1820 and #41.
	const FName Echoes(Effects::EchoesOfThePastKey);
	if (FloorBrief.Modifiers.Contains(Echoes))
	{
		Counting.Add(Echoes, EchoesStage == 0
			? FString::Printf(TEXT("echoes of the past: %d of the last floor's dead come %.0f seconds in"),
							  EchoesFromLastFloor.Num(), Effects::EchoesAppearAfterSeconds)
			: FString::Printf(TEXT("echoes of the past: %d echo(es) standing"),
							  EchoesStandingNow().Num()));
	}

	// AND DIVINE WRATH: whether a beam is chasing, and how many creatures beams have destroyed.
	// Issues #1820 and #41.
	const FName Wrath(Effects::DivineWrathKey);
	if (FloorBrief.Modifiers.Contains(Wrath))
	{
		Counting.Add(Wrath, DivineWrathBeam.IsValid()
			? FString::Printf(TEXT("divine wrath: a beam is chasing you; %d creature(s) destroyed"),
							  DivineWrathDestroyed)
			: FString::Printf(TEXT("divine wrath: a beam every %.0f seconds; %d creature(s) destroyed"),
							  Effects::DivineWrathSecondsBetween, DivineWrathDestroyed));
	}

	// AND PLAGUE CONVERGENCE: when it begins, or how many have come and how sick the player is.
	// Issues #1820 and #41.
	const FName Plague(Effects::PlagueConvergenceKey);
	if (FloorBrief.Modifiers.Contains(Plague))
	{
		Counting.Add(Plague, FloorBrief.bOneWave
			? FString(TEXT("plague convergence: never on a horde wave"))
			: !Effects::PlagueConvergenceHasBegun(PlagueConvergenceSecondsOnFloor)
			? FString::Printf(TEXT("plague convergence: begins %.0f seconds into the floor"),
							  Effects::PlagueConvergenceBeginsAfterSeconds)
			: FString::Printf(TEXT("plague convergence: %d of %d alive; disease %d of %d, %.1f%% of "
								   "maximum health a second; only descending stops it"),
							  PlagueConvergenceCreaturesAlive(), Effects::PlagueConvergenceMostAlive,
							  PlagueConvergenceStacks, Effects::PlagueConvergenceMostStacks,
							  Effects::PlagueConvergenceDiseasePercentPerSecond(PlagueConvergenceStacks)));
	}

	// AND BLOOD BOND: whether this floor has bonded, and whether the bond still holds.
	// Issues #1820 and #41.
	const FName Bond(Effects::BloodBondKey);
	if (FloorBrief.Modifiers.Contains(Bond))
	{
		Counting.Add(Bond, BloodBonded.IsValid()
			? FString(TEXT("blood bond: an elite is bound to you; it cannot die unless you do"))
			: bBloodBondFormed
			? FString(TEXT("blood bond: ended for this floor"))
			: FString(TEXT("blood bond: the first elite that notices you will be bound")));
	}

	// AND THE REAPER: whether it has come, and what it does. Issues #1820 and #41.
	const FName Reaper(Effects::TheReaperKey);
	if (FloorBrief.Modifiers.Contains(Reaper))
	{
		Counting.Add(Reaper, FloorBrief.bOneWave
			? FString(TEXT("the reaper: never on a horde wave"))
			: TheReaper.IsValid()
			? FString(TEXT("the reaper: here, and it cannot die; one landed blow kills"))
			: FString::Printf(TEXT("the reaper: comes %.0f seconds into the floor"),
							  Effects::TheReaperDelaySeconds));
	}

	// AND CHAOS TOUCHED: the stacks of all eight kinds. Issues #1820 and #41.
	const FName Touched(Effects::ChaosTouchedKey);
	if (FloorBrief.Modifiers.Contains(Touched))
	{
		const auto S = [this](int32 Kind) { return ChaosTouchedStacksOf(Kind); };
		Counting.Add(Touched, FString::Printf(
			TEXT("chaos touched: more health %d, speed %d, attack speed %d, resistances %d; "
				 "less health %d, speed %d, attack speed %d, resistances %d (each %.0f%%, at most %d); "
				 "a floor boss's death cleanses the less"),
			S(Effects::ChaosTouchedHealthMore), S(Effects::ChaosTouchedSpeedMore),
			S(Effects::ChaosTouchedAttackSpeedMore), S(Effects::ChaosTouchedResistanceMore),
			S(Effects::ChaosTouchedHealthLess), S(Effects::ChaosTouchedSpeedLess),
			S(Effects::ChaosTouchedAttackSpeedLess), S(Effects::ChaosTouchedResistanceLess),
			Effects::ChaosTouchedPercentPerStack, Effects::ChaosTouchedMostStacks));
	}

	const FName Souls(Effects::SoulHarvestKey);
	if (FloorBrief.Modifiers.Contains(Souls))
	{
		int32 Most = 0;
		for (const TPair<TWeakObjectPtr<ACataclysmEnemyCharacter>, FSoulHarvestHeld>& Entry :
			 SoulHarvestHeld)
		{
			if (Entry.Key.IsValid() && !UCataclysmSkillEffects::IsDead(Entry.Key.Get()))
			{
				Most = FMath::Max(Most, Entry.Value.Souls);
			}
		}
		Counting.Add(Souls, FString::Printf(
			TEXT("soul harvest: %d soul(s) taken, the most on one living creature %d of %d"),
			SoulHarvestGiven, Most, Effects::SoulHarvestMostSouls));
	}

	// AND TRICK OR TREAT: the clicks, the creatures raised, and whether a treat is running.
	// Issues #1820 and #41.
	const FName Treat(Effects::TrickOrTreatKey);
	if (FloorBrief.Modifiers.Contains(Treat))
	{
		Counting.Add(Treat, FString::Printf(
			TEXT("trick or treat: %d picked up, %d creatures raised%s"),
			TrickOrTreatPickups, TrickOrTreatRaised,
			TrickOrTreatIsHasting() ? TEXT(", hasted by a treat") : TEXT("")));
	}

	// AND THE STARVATION CURSE: each kind as "N of M" and the share it takes. Issues #1820
	// and #41.
	const FName Curse(Effects::StarvationCurseKey);
	if (FloorBrief.Modifiers.Contains(Curse))
	{
		Counting.Add(Curse, FString::Printf(
			TEXT("starvation curse: movement %.0f%% slower (%d of %d), maximum health %.0f%% "
				 "less (%d of %d); a floor boss's death cleanses both"),
			Effects::StarvationCurseLessPercent(StarvationCurseMovementStacks),
			StarvationCurseMovementStacks, Effects::StarvationCurseMostStacks,
			Effects::StarvationCurseLessPercent(StarvationCurseHealthStacks),
			StarvationCurseHealthStacks, Effects::StarvationCurseMostStacks));
	}

	const FName Wasting(Effects::WastingSicknessKey);
	if (FloorBrief.Modifiers.Contains(Wasting))
	{
		Counting.Add(Wasting,
					 FString::Printf(TEXT("%d of %d"), WastingSicknessStacks,
									 Effects::WastingSicknessMostStacks));
	}

	// AND THE BLOOD ALTAR'S DEATHS, WITH M THE DEATH AT WHICH A PULSE STOPS GROWING.
	// Issues #1820 and #41. The count alone: the panel shows no damage figure for
	// any row, so this one does not start.
	const FName Altar(Effects::BloodAltarKey);
	if (FloorBrief.Modifiers.Contains(Altar))
	{
		Counting.Add(Altar, FString::Printf(TEXT("%d of %d"), BloodAltarDeaths,
											Effects::BloodAltarDeathsToCeiling));
	}
	const FName Fog(Effects::NecroticGroundKey);
	if (FloorBrief.Modifiers.Contains(Fog))
	{
		int32 Patches = 0;
		for (const TWeakObjectPtr<ACataclysmGroundZone>& Patch : NecroticGroundPatches)
		{
			Patches += Patch.IsValid() ? 1 : 0;
		}
		Counting.Add(Fog, FString::Printf(TEXT("%d of %d"), Patches,
										  Effects::NecroticGroundMostPatches));
	}

	// AND RAVENOUS HOARD'S STRONGEST CREATURE, AS OF THE LAST BEAT. Issues #1820 and #41.
	// Every creature holds its own count, and the panel shows one line a row, so it
	// shows the count nearest the cap.
	const FName Hoard(Effects::RavenousHoardKey);
	if (FloorBrief.Modifiers.Contains(Hoard))
	{
		Counting.Add(Hoard, FString::Printf(TEXT("strongest %d of %d"),
											RavenousHoardStrongest,
											Effects::RavenousHoardMostStacks));
	}

	// AND MARCH OF PROGRESS, WHICH SAYS TWO THINGS AND NEITHER IS A COUNT OF N OF M.
	// Issues #1820 and #41. What the floor is doing to the creatures is a multiplier, and
	// what the player has to do about it is kill one particular creature -- so the line
	// says the damage and then says whether that creature is still alive.
	//
	// IT DOES NOT SAY WHICH CREATURE. Nothing in the game does; that is issue #1997.
	// This tells the player whether there is one left to kill, which is what the row's
	// last sentence -- skipping commanders -- is about.
	const FName March(Effects::MarchOfProgressKey);
	if (FloorBrief.Modifiers.Contains(March))
	{
		const float Multiplier = Effects::MarchOfProgressDamageMultiplierOnFloor(
			FloorBrief.FloorNumber);

		// THE CREATURE IS NAMED, AND THAT IS WHY THIS LINE CHANGED AFTER THE RULE MERGED.
		// Issues #1820, #41 and #1997. A floor can carry this row and `War_Commander_s_Aura`
		// at once, and both use the word "Commander" for different things: one creature the
		// player must hunt, and every Elite buffing its neighbours. Saying WHICH creature is
		// this rule's Commander is what keeps the two lines apart on one panel. It does not
		// close issue #1997 -- a name in a panel is still not a way to pick a creature out
		// of a crowd -- and that issue says what would be.
		//
		// FROM THE ARCHETYPE TABLE AND NOT FROM THE CLASS NAME, which is what
		// `ArchetypeNameForRow` exists for: a creature renamed in the design workbook is
		// renamed here with no C++ edited. It answers an empty string for a creature that
		// names no row, which is every creature the sandbox spawns as a practice target, and
		// `UnnamedCreature` is what the creature panel already says for those.
		//
		// THIS FILE'S FIRST INCLUDE FROM `Interface/`, AND IT IS A FUNCTION LIBRARY RATHER
		// THAN A WIDGET: `UCataclysmCreaturePanel` is a `UBlueprintFunctionLibrary` whose
		// header pulls in CoreMinimal, the library base and Box2D. Resolving the name here
		// rather than passing the row key outward is what stops a second copy of the lookup
		// existing.
		// THE NAME IS TAKEN FIRST AND THE STATE IS DECIDED SECOND, and the order of those
		// two questions is the whole of this block. A creature that has DIED keeps a valid
		// weak pointer until its body is removed, so "is the pointer valid" does not answer
		// "is it alive" -- only `bMarchOfProgressCommanderSlain` does. Asking the pointer
		// first and calling that alive would print "alive" for the Commander the player had
		// just killed, for as long as the body stood there.
		FString Named;
		if (const ACataclysmEnemyCharacter* Chosen = MarchOfProgressCommander.Get())
		{
			Named = UCataclysmCreaturePanel::ArchetypeNameForRow(
				UCataclysmCreaturePanel::LoadEnemyArchetypeTable(), Chosen->ArchetypeRow);
			if (Named.IsEmpty())
			{
				Named = UCataclysmCreaturePanel::UnnamedCreature;
			}
		}

		// AND A SLAIN COMMANDER IS STILL NAMED WHEN ITS BODY IS STILL THERE. Once the body
		// goes the pointer goes stale and there is no name left to print, which is why the
		// slain wording has to read without one.
		FString Commander(TEXT("no Commander"));
		if (bMarchOfProgressCommanderSlain)
		{
			Commander = Named.IsEmpty()
				? FString(TEXT("this floor's Commander, slain"))
				: FString::Printf(TEXT("%s, this floor's Commander, slain"), *Named);
		}
		else if (!Named.IsEmpty())
		{
			Commander = FString::Printf(TEXT("%s, this floor's Commander, alive"), *Named);
		}

		Counting.Add(March, FString::Printf(
			TEXT("enemies x%.1f, %s, %d slain this run"), Multiplier, *Commander,
			MarchOfProgressCommandersKilled));
	}

	// AND COMMANDER'S AURA, WHICH NAMES ITS OWN ROW IN THE LINE. Issues #1820 and #41.
	// A floor can carry this row and March of Progress at once, and both use the word
	// "Commander" for different things -- one creature the player must hunt, and every
	// Elite buffing its neighbours. Saying which rule the count belongs to is what keeps
	// the two lines apart on one panel.
	//
	// A COUNT WITH NO CEILING, like Volatile Evolution's. The row states no number of
	// commanders; how many a floor has is its population's business and not this rule's.
	const FName Aura(Effects::CommandersAuraKey);
	if (FloorBrief.Modifiers.Contains(Aura))
	{
		Counting.Add(Aura, FString::Printf(
			TEXT("%d commanders on this floor (Commander's Aura)"),
			CommandersAuraCommanders));
	}

	// AND HOW CLOSE THE HOLY REVIVAL IS, OR HOW MANY IT RAISED. Issues #1820 and #41. The
	// count it comes at is shown because it is a count the player moves: every creature
	// they kill brings it one nearer.
	const FName Resurgence(Effects::DivineResurgenceKey);
	if (FloorBrief.Modifiers.Contains(Resurgence))
	{
		const int32 Placed = DivineResurgencePlacedCount();
		const int32 ComesAt = (Placed * Effects::DivineResurgenceFallenPercent + 99) / 100;
		Counting.Add(Resurgence,
					 bDivineResurgenceDone
						 ? FString::Printf(TEXT("holy revival: %d risen"),
										   DivineResurgenceRisen)
						 : FString::Printf(TEXT("holy revival: %d of %d fallen, comes at %d"),
										   DivineResurgenceFallen, Placed, ComesAt));
	}

	// AND WHAT THE VOID HOLDS FOR THE FINAL BOSS, or what it gave the boss and whether the
	// damage reached its cap. Issues #1820 and #41.
	const FName Void(Effects::NothingIsForgottenKey);
	if (FloorBrief.Modifiers.Contains(Void))
	{
		Counting.Add(Void, NothingIsForgottenBoss.IsValid()
			? FString::Printf(TEXT("nothing is forgotten: the final boss took %.0f health and %.0f damage%s"),
							  NothingIsForgottenHealthGiven, NothingIsForgottenDamageGiven,
							  NothingIsForgottenDamage > NothingIsForgottenDamageGiven + 0.5f
								  ? TEXT(" (damage at its cap)") : TEXT(""))
			: FString::Printf(TEXT("nothing is forgotten: the void holds %.0f health and %.0f damage for the final boss"),
							  NothingIsForgottenHealth, NothingIsForgottenDamage));
	}

	// AND WHAT THE UNSTABLE PORTAL LAST DID, or its odds before its first step. Issues
	// #1820 and #41.
	const FName Portal(Effects::UnstablePortalKey);
	if (FloorBrief.Modifiers.Contains(Portal))
	{
		const TCHAR* Last = UnstablePortalLast == Effects::UnstablePortalReturns ? TEXT("sent you back to the start")
			: UnstablePortalLast == Effects::UnstablePortalRaisesAMiniBoss ? TEXT("raised a mini-boss")
												   : nullptr;
		Counting.Add(Portal, Last
			? FString::Printf(TEXT("unstable portal: %s"), Last)
			: FString(TEXT("unstable portal: 50% down, 25% back to the start, 25% a mini-boss")));
	}

	// AND THE RANGE CHAOTIC LOOT DRAWS AFFIX TIERS FROM, which is the difficulty's own cap.
	// Issues #1820 and #41. The item pop-up already prints each affix's tier.
	const FName Chaotic(Effects::ChaoticLootKey);
	if (FloorBrief.Modifiers.Contains(Chaotic))
	{
		Counting.Add(Chaotic, FString::Printf(
			TEXT("chaotic loot: every affix tier from T1 to T%d equally likely"),
			UCataclysmDropRoll::MaxAffixTierOnADrop(DifficultyTierFor(this))));
	}

	// AND WHICH SLOT SCARCITY HAS SWITCHED OFF, named. Issues #1820 and #41. The gear
	// screen marks the same slot.
	const FName Scarcity(Effects::ScarcityKey);
	if (FloorBrief.Modifiers.Contains(Scarcity))
	{
		const UWorld* World = GetWorld();
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const ACataclysmPlayerCharacter* Player =
			Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
		const UCataclysmEquipmentComponent* Equipment = Player ? Player->GetEquipment() : nullptr;
		const ECataclysmGearSlot Off =
			Equipment ? Equipment->GetDisabledSlot() : ECataclysmGearSlot::Count;
		Counting.Add(Scarcity,
					 Off == ECataclysmGearSlot::Count
						 ? FString(TEXT("scarcity: nothing worn to switch off"))
						 : FString::Printf(TEXT("scarcity: %s gives nothing on this floor"),
										   *UCataclysmGearSlots::DisplayName(Off)));
	}

	// AND WHEN THE DIRGE NEXT CRESCENDOS, OR HOW LONG ITS HASTE HAS LEFT. Issues #1820 and
	// #41. The panel is the rule's only sign: there is no audio for the music.
	const FName Dirge(Effects::DirgeResonanceKey);
	if (FloorBrief.Modifiers.Contains(Dirge))
	{
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		Counting.Add(Dirge,
					 DirgeResonanceHastedUntilSeconds > Now
						 ? FString::Printf(TEXT("dirge: enemies hasted, %d s left"),
										   FMath::CeilToInt(DirgeResonanceHastedUntilSeconds - Now))
						 : FString::Printf(TEXT("dirge: crescendo in %d s"),
										   FMath::CeilToInt(Effects::DirgeResonanceEverySeconds
															- DirgeResonanceSecondsSinceLast)));
	}

	// AND HOW FAR THE PLAYER IS FROM OPENING THE STAIRS. Issues #1820 and #41. The target
	// is the current one: it rises when creatures arrive and falls when one dies to
	// anything but the player.
	const FName Gates(Effects::BloodGatesKey);
	if (FloorBrief.Modifiers.Contains(Gates))
	{
		const int32 Placed = BloodGatesPlacedCount();
		Counting.Add(Gates,
					 BloodGatesSealTheStairs()
						 ? FString::Printf(TEXT("blood gates: %d of %d slain, open at %d"),
										   BloodGatesSlain, Placed,
										   Effects::BloodGatesOpenAt(Placed))
						 : FString(TEXT("blood gates: open")));
	}

	// AND HOW MANY OF THE FLOOR'S DEAD GOT BACK UP. Issues #1820 and #41.
	const FName Rising(Effects::DeadRisingKey);
	if (FloorBrief.Modifiers.Contains(Rising))
	{
		Counting.Add(Rising, FString::Printf(TEXT("dead rising: %d got back up"),
											 DeadRisingRisen));
	}

	// AND HOW MANY ANTI-MAGIC ZONES ARE STANDING. Issues #1820 and #41. The number is all
	// the panel says. Which spells are refused is the skill bar's to show, and it does:
	// it marks each slot whose own skill `skill_locked` reaches (issue #1810, built in
	// #1819).
	//
	// COUNTED AS WHAT IS STILL THERE, not as the list's length. The list is pruned on the
	// beat, so between a zone expiring and the next beat it holds a pointer to nothing.
	const FName AntiMagic(Effects::AntiMagicZonesKey);
	if (FloorBrief.Modifiers.Contains(AntiMagic))
	{
		int32 Standing = 0;
		for (const TWeakObjectPtr<ACataclysmGroundZone>& Zone : AntiMagicZones)
		{
			Standing += Zone.IsValid() ? 1 : 0;
		}
		Counting.Add(AntiMagic,
					 FString::Printf(TEXT("anti-magic zone: %d standing"), Standing));
	}

	// AND GRAVE TIDE'S WAVES SO FAR. Issues #1820 and #41.
	const FName Tide(Effects::GraveTideKey);
	if (FloorBrief.Modifiers.Contains(Tide))
	{
		Counting.Add(Tide, FString::Printf(TEXT("wave %d of %d"), GraveTideWaves,
										   Effects::GraveTideMostWaves));
	}

	// A COUNT WITH NO CEILING IN IT, unlike every line above. There is no limit on how
	// many creatures a floor may mutate: the limit is one each, and the floor's
	// population is not a figure this rule owns.
	const FName Mutating(Effects::VolatileEvolutionKey);
	if (FloorBrief.Modifiers.Contains(Mutating))
	{
		Counting.Add(Mutating, FString::Printf(TEXT("mutated %d"),
											   VolatileEvolutionMutations));
	}

	// AND HOW MANY GUARDS HAVE ARRIVED, a count with no ceiling for the reason the line
	// above has none: the limit is one roll each, not a number of guards a floor may hold.
	const FName Guarding(Effects::RoyalGuardKey);
	if (FloorBrief.Modifiers.Contains(Guarding))
	{
		Counting.Add(Guarding, FString::Printf(TEXT("guards %d"), RoyalGuardGuardsArrived));
	}

	// AND WHETHER THIS FLOOR'S ONE HAS RISEN. A count of one against its ceiling, the
	// shape Grave Tide's waves use, because this row has a ceiling and Royal Guard's
	// guards do not.
	const FName Prince(Effects::DemonPrinceKey);
	if (FloorBrief.Modifiers.Contains(Prince))
	{
		Counting.Add(Prince, FString::Printf(TEXT("prince %d of %d"), DemonPrincesRisen,
											 Effects::DemonPrincesPerFloor));
	}

	// AND THE CHAIN, WITH THE LORD BESIDE IT. Two numbers on one line because the row has
	// two things a player would want to know and the panel gives a row one line.
	const FName Spreading(Effects::EpidemicKey);
	if (FloorBrief.Modifiers.Contains(Spreading))
	{
		Counting.Add(Spreading,
					 FString::Printf(TEXT("chain %d of %d, lord %d of %d"), EpidemicChain,
									 Effects::EpidemicSpreadsToKill,
									 EpidemicPlagueLordsRisen,
									 Effects::EpidemicPlagueLordsPerFloor));
	}

	// AND WHAT THE CHAMPIONS HAVE TAKEN, WITH WHAT IT BOUGHT THEM. Both numbers are the
	// floor's and not one creature's: a floor may have several champions feeding at once
	// and the panel gives a row one line.
	const FName Feeding(Effects::BloodForgedChampionsKey);
	if (FloorBrief.Modifiers.Contains(Feeding))
	{
		Counting.Add(Feeding,
					 FString::Printf(TEXT("%d death(s) absorbed, %d rung(s) gained"),
									 BloodForgedChampionsAbsorbed,
									 BloodForgedChampionsRungsGained));
	}

	// AND HOW MANY OF THE PLAYER'S KILLS GOT BACK UP.
	const FName Haunting(Effects::VengefulWraithsKey);
	if (FloorBrief.Modifiers.Contains(Haunting))
	{
		Counting.Add(Haunting, FString::Printf(TEXT("%d wraith(s) risen"),
											   VengefulWraithsRisen));
	}

	// AND WHAT THE RADIANT GROUND HAS TAKEN, WITH WHAT IS STANDING. Two numbers because a
	// player wants to know both how close the reward is and how much ground there is to
	// avoid.
	const FName Judging(Effects::JudgmentZonesKey);
	if (FloorBrief.Modifiers.Contains(Judging))
	{
		Counting.Add(Judging,
					 FString::Printf(TEXT("%d trigger(s) of %d, %d zone(s) standing"),
									 JudgmentZonesTriggers,
									 Effects::JudgmentZonesTriggersForTheBonus,
									 JudgmentZones.Num()));
	}

	return Counting;
}

void ACataclysmDungeonGameMode::NoteHitForBrandOfTheAggressor(
	const FCataclysmHitNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::BrandOfTheAggressorKey)))
	{
		return;
	}

	// A BLOW THAT ACTUALLY LANDED, the same guard `NoteHitForWastingSickness`
	// makes and for the same reason: `Landed` is what reached the target after
	// every mitigation step, so an evaded or wholly stopped blow is not a hit and
	// does not brand.
	if (Notice.Landed <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player)
	{
		return;
	}

	// THE PLAYER MUST HAVE STRUCK, AND STRUCK A CREATURE. The row is "Hitting an
	// enemy applies a stack ... to you", so this reads `Attacker` where Wasting
	// Sickness reads `Target`. Without the creature check a blow the player
	// landed on anything else -- a hazard, a destructible -- would brand them.
	//
	// AND IT STOPS THIS RULE FEEDING ITSELF, WHICH IS THE BIGGER HALF AND WAS NOT
	// IN THIS COMMENT UNTIL A GUARD PROOF SHOWED IT. The nova below is dealt BY
	// the player TO the player, so it is announced as a blow like any other. With
	// this guard gone it brands: every eruption adds one to the count it just
	// cleared, and the next eruption arrives a blow early, for ever.
	//
	// MEASURED 2026-09-14 rather than reasoned about. Breaking this guard was
	// predicted to fail one test and failed two -- the second being
	// `TwentyLandedBlowsBrandThePlayerAndTheTwentiethErupts`, which lands nineteen
	// blows after an eruption and asserts they take nothing. The prediction was
	// traced from what the TESTS land and missed what the RULE lands in reply.
	if (Notice.Attacker != Player
		|| !Cast<ACataclysmEnemyCharacter>(Notice.Target))
	{
		return;
	}

	// ASKED BEFORE THE COUNT IS RAISED, because raising it clears the count on
	// the blow that erupts and a cleared count cannot answer this. The two reads
	// are separated by taking the old value first.
	const int32 Before = BrandStacks;
	BrandStacks = Effects::BrandStacksAfterHit(Before, /*bBrands=*/true);

	// THE PANEL LEARNS THE NEW COUNT BEFORE THE RETURN BELOW, AND THAT ORDER IS
	// THE POINT. Nineteen blows out of twenty leave through that return, and they
	// are exactly the blows a player has nothing else to read: the rule does
	// nothing to them and the count is the only sign anything is building.
	RefreshFloorModifierPanel();

	if (!Effects::BrandErupts(Before))
	{
		return;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent());
	if (!AbilitySystem)
	{
		return;
	}

	const float Damage = Effects::BrandNovaDamage(AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute()));
	if (Damage <= 0.0f)
	{
		return;
	}

	// AN AREA BLOW, NOT A LASTING FIRE. `bIsArea` says it cannot be evaded, which
	// is what "you erupt" describes; leaving `bIsDamageOverTime` false lets an
	// energy shield absorb it as it absorbs any other blow. The same two choices
	// `StepArtilleryStrike` and `NoteDeathForHellfire` make.
	FCataclysmHitDelivery Delivery;
	Delivery.bIsArea = true;

	// THE PLAYER FIRST AND SEPARATELY, because the ally search below excludes the
	// actor it is asked on behalf of -- its shared gather step drops
	// `Actor == Instigator`. The row says the nova reaches "you and nearby
	// allies"; a rule that only used the search would erupt and never touch the
	// player at all.
	UCataclysmSkillEffects::ApplyDirectDamage(Player, Player, Damage, Delivery);

	// AND THEN WHOEVER IS ON THE PLAYER'S SIDE INSIDE IT. This is the one rule
	// here that asks for ALLIES: `Demonic_Hellfire` catches everyone because its
	// row names nobody, and this row names its two sides.
	//
	// IT ANSWERS AN EMPTY LIST TODAY AND THAT IS NOT A FAULT. The player's only
	// possible allies are minions, which are a placeholder under issue #340, so
	// in a dungeon as it stands the nova reaches the player alone. The call is
	// here because the row asks for it and because the day a minion exists this
	// rule should already be right.
	const TArray<AActor*> Allies = UCataclysmTargeting::FindAlliesInSphere(
		World, Player, Player->GetActorLocation(), Effects::BrandNovaRadiusCm);
	for (AActor* Ally : Allies)
	{
		if (!UCataclysmTargeting::AbilitySystemOf(Ally))
		{
			continue;
		}

		UCataclysmSkillEffects::ApplyDirectDamage(Player, Ally, Damage, Delivery);
	}
}

void ACataclysmDungeonGameMode::NoteDeathForWitheredGround(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::WitheredGroundKey)))
	{
		return;
	}

	// THE VICTIM MUST BE A CREATURE. The row says "Enemies leave patches", and
	// this notice is sent for every death on the floor including the player's.
	// A patch left where the player died would punish them for dying in a place
	// they are about to stand up in.
	if (!Cast<ACataclysmEnemyCharacter>(Notice.Victim))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// WHOSE NAME IT IS DEALT IN. Nothing here damages, so the owner is not
	// needed for a damage route -- but a patch with no owner has no ability
	// system component behind it, and `ACataclysmFloorHazardSource`'s own header
	// records that every apply route refuses in that case. Using it here keeps
	// this row's patch the same kind of object as the other two hazards', so a
	// later change that gives Barren Earth something to apply does not have to
	// rebuild the ownership first.
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}

	// WHERE THE CREATURE DIED, WHICH THE NOTICE CARRIES. The other two hazard
	// rules pick a random point near the player because nothing in the world
	// tells them where to put one. This row does not choose: "Enemies leave
	// patches of Barren Earth ON DEATH" names the place.
	//
	// START AND END THE SAME POINT MAKES IT ROUND, the way a well is made round.
	//
	// NO DAMAGE PER TICK, WHICH IS THE FIRST HAZARD HERE THAT DOES NOT BURN. The
	// row takes recovery away and nothing else. Since issue #1701 a zone with no
	// damage still sweeps, and this one does not even need that -- the reduction
	// is a stat modifier the beat applies, not something the patch does to
	// whoever it finds. The patch is a shape to stand in and a thing to see.
	ACataclysmGroundZone* Patch = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Notice.Location, Notice.Location,
		Effects::WitheredGroundPatchRadiusCm, 0.0f);
	if (!Patch)
	{
		return;
	}

	WitheredGroundPatches.Add(Patch);
}

void ACataclysmDungeonGameMode::NoteDeathForLeechSpores(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::LeechSporesKey)))
	{
		return;
	}

	// THE VICTIM MUST BE A CREATURE. The row says "When you kill an enemy", and
	// this notice is sent for every death on the floor including the player's.
	if (!Cast<ACataclysmEnemyCharacter>(Notice.Victim))
	{
		return;
	}

	// AND THE PLAYER MUST HAVE KILLED IT. "When you kill an enemy" names who does
	// the killing, and the design log's Mortal Decay entry records what that means
	// for a listener on this notice: `NoteDeathForMortalDecay` asks the same for
	// "reaping enemies". A creature whose death names anyone else as its killer,
	// or nobody, leaves no cloud.
	//
	// A MINION'S KILL LEAVES ONE ONLY WITH THE CONDUIT KEYSTONE, since issue
	// #1515. It used to leave one always, because the killer on the notice was
	// the summoner for a minion's blow; the project owner ruled on 2026-09-17
	// that a minion's kill is the minion's unless that keystone says otherwise,
	// and `UCataclysmCombatEvents::NoteBlow` is where that is decided.
	//
	// THE SAME ROUTE TO THE PLAYER THE BEAT TAKES, so the two cannot disagree
	// about whose floor this is.
	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
	if (!World || !Player || Notice.Killer != Player)
	{
		return;
	}

	// OWNED BY THE FLOOR, AND THE DESIGN LOG SETTLED WHY. A hazard left "from
	// their corpse" cannot be owned by a creature that is already dead, because
	// `ACataclysmGroundZone::Sweep` returns early when its owner is gone.
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}

	ACataclysmGroundZone* Cloud = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Notice.Location, Notice.Location,
		Effects::LeechSporesCloudRadiusCm, 0.0f);
	if (!Cloud)
	{
		return;
	}

	LeechSporesClouds.Add(Cloud);
}

void ACataclysmDungeonGameMode::NoteDeathForBloodAltar(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::BloodAltarKey)))
	{
		return;
	}

	// A CREATURE'S DEATH, WHOEVER CAUSED IT. This notice is sent for every death on
	// the floor, the player's included, so the victim is asked about. Who killed it
	// is not asked, because "Slaying enemies" names nobody; the declaration says
	// why that is the opposite of Leech Spores.
	if (!Cast<ACataclysmEnemyCharacter>(Notice.Victim))
	{
		return;
	}

	const int32 Before = BloodAltarDeaths;
	BloodAltarDeaths = Effects::BloodAltarDeathsAfterOne(BloodAltarDeaths);
	if (BloodAltarDeaths != Before)
	{
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::NoteHitForHolyRepercussions(
	const FCataclysmHitNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::HolyRepercussionsKey)))
	{
		return;
	}

	// A LANDED BLOW AND NOT AN ATTEMPT, the test every blow listener here makes.
	// `Landed` is what reached the target after every mitigation step and is zero
	// for a blow that was evaded or wholly stopped.
	if (Notice.Landed <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player)
	{
		return;
	}

	// WHOSE BLOW, AND ON WHAT -- TWO TESTS, EACH GUARDING A BLOW THE OTHER DOES
	// NOT. This function's declaration carries the full reasoning; in short:
	//
	//   `!Creature`                  alone refuses the player hitting the player,
	//                                which is Brand of the Aggressor's eruption.
	//   `Notice.Attacker != Player`  alone refuses a creature, hazard or explosion
	//                                hitting a creature. A reading of "upon being
	//                                hit", which names no attacker.
	//
	// Both refuse the burst below, which is a creature hitting the player, so a
	// burst cannot provoke another whichever half is removed.
	// `OnlyThePlayersOwnBlowProvokesJudgmentAndTheStairsClearIt` lands one blow of
	// each kind, so removing either half fails a test.
	ACataclysmEnemyCharacter* Creature =
		Cast<ACataclysmEnemyCharacter>(Notice.Target);
	if (Notice.Attacker != Player || !Creature)
	{
		return;
	}

	if (!Effects::HolyRepercussionsRetaliates(
			DungeonGameModeHolyRepercussionsRoll()))
	{
		return;
	}

	// THE BURST IS WORTH THE CREATURE'S OWN ATTACK DAMAGE, read off it rather
	// than written here, which is `UCataclysmEnemyModifiers::InfernalBrand`'s
	// stated reason: a figure in this file would make every creature retaliate
	// alike. It also means an illusion retaliates for nothing, with no code here
	// knowing that rule exists.
	const UAbilitySystemComponent* Theirs =
		UCataclysmTargeting::AbilitySystemOf(Creature);
	const float Damage = Theirs
		? Theirs->GetNumericAttribute(
			  UCataclysmCombatAttributeSet::GetAttackDamageAttribute())
		: 0.0f;

	// THE JUDGMENT STACK LANDS WHETHER OR NOT THE BURST IS WORTH ANYTHING. The
	// row gives the burst and the debuff as two things one retaliation does, and
	// a creature with no damage has still retaliated.
	JudgmentStacks = Effects::HolyRepercussionsStacksAfterBurst(JudgmentStacks);
	RefreshFloorModifierPanel();

	if (Damage <= 0.0f)
	{
		return;
	}

	// EVERYONE ON THE PLAYER'S SIDE INSIDE IT, asked as the CREATURE's enemies.
	// The row says "dealing damage in an area" and names no side; whose burst it
	// is decides the side, so no ruling was needed.
	const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
		World, Creature, Creature->GetActorLocation(),
		Effects::HolyRepercussionsBurstRadiusCm);

	FCataclysmHitDelivery Delivery;
	Delivery.bIsArea = true;
	for (AActor* Target : Caught)
	{
		if (!IsValid(Target) || !UCataclysmTargeting::AbilitySystemOf(Target))
		{
			continue;
		}
		UCataclysmSkillEffects::ApplyDirectDamage(Creature, Target, Damage,
												  Delivery);
	}
}

void ACataclysmDungeonGameMode::NoteDeathForFungalOvergrowth(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::FungalOvergrowthKey)))
	{
		return;
	}

	// THE VICTIM MUST BE A CREATURE. The row says "Killing enemies creates
	// mushrooms", and this notice is sent for every death on the floor including
	// the player's. The same guard `NoteDeathForWitheredGround` above makes, for
	// the same reason.
	if (!Cast<ACataclysmEnemyCharacter>(Notice.Victim))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}

	// WHICH KIND, ROLLED ONCE AND USED TWICE -- for the colour it is drawn in and
	// for the list it is remembered in. Asking twice would let a mushroom be
	// drawn as one kind and act as the other, which no test of either half alone
	// would catch.
	const bool bHelps =
		Effects::FungalOvergrowthBoosts(DungeonGameModeFungalOvergrowthRoll());

	// WHERE THE CREATURE DIED, WHICH THE NOTICE CARRIES, and start and end at the
	// same point to make it round. Withered Ground's patch is placed exactly so.
	//
	// NO DAMAGE PER TICK. A mushroom is a thing to stand on, and the row gives it
	// nothing to do to anybody who is not the player.
	//
	// AND ITS OWN COLOUR, WHICH IS THE ONLY ARGUMENT HERE NO OTHER FLOOR HAZARD
	// PASSES. Every zone in the game until now was drawn in its owner's colour,
	// so the two kinds of mushroom -- which share an owner, because one floor has
	// one hazard source -- would have been indistinguishable.
	ACataclysmGroundZone* Mushroom = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Notice.Location, Notice.Location,
		Effects::FungalOvergrowthMushroomRadiusCm, 0.0f,
		/*bAffectsEveryone=*/false,
		FName(bHelps ? Effects::FungalOvergrowthBoostDrawnAs
					 : Effects::FungalOvergrowthSlowDrawnAs));
	if (!Mushroom)
	{
		return;
	}

	if (bHelps)
	{
		FungalOvergrowthBoostMushrooms.Add(Mushroom);
	}
	else
	{
		FungalOvergrowthSlowMushrooms.Add(Mushroom);
	}
}

void ACataclysmDungeonGameMode::NoteDeathForSporeClouds(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::SporeCloudsKey)))
	{
		return;
	}

	// THE VICTIM MUST BE A CREATURE. The row says "Enemies have a chance", and
	// this notice is sent for every death on the floor including the player's.
	// The same guard `NoteDeathForWitheredGround` above makes, for the same
	// reason: a rule written for enemies must not fire on the player's death.
	if (!Cast<ACataclysmEnemyCharacter>(Notice.Victim))
	{
		return;
	}

	// THE ROLL COMES BEFORE EVERYTHING THAT FOLLOWS, and the header says why:
	// the chance decides whether spores are released, not whether they land on
	// anybody. A death far from the player still spends its roll.
	if (!Effects::SporeCloudsRelease(DungeonGameModeSporeCloudsRoll()))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// THE SAME ROUTE `ApplyFloorRulesToPlayer` TAKES to the player's character,
	// so the two cannot disagree about whose floor rules these are.
	APlayerController* Controller = World->GetFirstPlayerController();
	ACataclysmPlayerCharacter* Player =
		Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player)
	{
		return;
	}

	// WHERE THE CREATURE DIED, WHICH THE NOTICE CARRIES, MEASURED FLAT. The
	// reach is a distance across the floor, and a player standing a step above
	// or below a corpse is no further from it. That is the reading
	// `UCataclysmTargeting::IsInLine` already makes, by zeroing Z on both
	// vectors before it measures anything.
	FVector Apart = Player->GetActorLocation() - Notice.Location;
	Apart.Z = 0.0f;
	if (!Effects::SporeCloudsReach(Apart.Size()))
	{
		return;
	}

	// WHOSE NAME THE POISON IS DEALT IN. `ApplyDamageOverTime` refuses outright
	// unless the instigator resolves to an ability system component, and the
	// creature that released the spores is dead -- which is the whole reason
	// `ACataclysmFloorHazardSource` exists. Naming the corpse here is the fault
	// that made three of Artillery Strike's tests fail before it was found.
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}

	// THE AILMENT'S OWN ROW SAYS WHAT IT DOES, AND NOTHING HERE STATES A FIGURE.
	// `UCataclysmAilments::Apply` reads `DoT_Poison` out of
	// `game/Data/StatusEffects.csv` -- 20 damage a second for 8 seconds today --
	// and magnitude one asks for that designed figure and no more. A number
	// written here would be a second place to change it and a chance for the two
	// to disagree.
	//
	// AND THE ROW'S FIGURES ARRIVE UNCHANGED, WHICH IS NOT OBVIOUS AND IS LOAD
	// BEARING. `Apply` hands `ApplyDamageOverTime` a true `bScalesWithInstigator`,
	// which multiplies the amount, the duration and the tick rate by the
	// instigator's three damage-over-time stats. `ACataclysmFloorHazardSource`'s
	// constructor makes a bare ability system component and adds NO attribute
	// set, so `AsMultiplierForSkill` takes its `HasAttributeSetForAttribute`
	// branch and answers 1 for all three.
	//
	// THAT IS THE ANSWER THIS RULE WANTS RATHER THAN AN ACCIDENT IT SURVIVES: a
	// floor is not a character and has no business making an ailment stronger.
	// If the hazard source is ever given a combat attribute set, those stats
	// start at zero and a zero multiplier makes the poison deal nothing --
	// `ApplyDamageOverTime` refuses an amount at or below zero -- so this rule
	// would stop working silently. `SporesFromADeathNearThePlayerPoisonThem`
	// reads health and is what notices.
	const FCataclysmAilmentKind* Poison =
		UCataclysmAilments::KindNamed(TEXT("Poison"));
	if (!Poison)
	{
		return;
	}

	// TYPED BY THE ROW, since the source carries no type and a poison takes its
	// element when it lands. Issue #1924.
	UCataclysmAilments::Apply(Source, Player, *Poison, /*Magnitude=*/1.0f,
							  /*Skill=*/nullptr,
							  DungeonGameModeTypeOfRow(Effects::SporeCloudsKey));
}

void ACataclysmDungeonGameMode::NoteDeathForHellfire(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::HellfireKey)))
	{
		return;
	}

	// THE VICTIM MUST BE A CREATURE, and this rule needs the creature itself
	// rather than only the fact, because its attack damage is what the explosion
	// is worth. The row says "Enemies have a chance", and this notice is sent for
	// every death on the floor including the player's.
	const ACataclysmEnemyCharacter* Exploding =
		Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Exploding)
	{
		return;
	}

	// THE ROLL COMES BEFORE ANYTHING IS READ OR PLACED, so a death spends the
	// same one roll wherever it happens and whatever it was worth. The reason
	// `NoteDeathForSporeClouds` above gives.
	if (!Effects::HellfireExplodes(DungeonGameModeHellfireRoll()))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// WHAT THE CREATURE HIT FOR, READ OFF THE CREATURE AND NOT WRITTEN HERE. This
	// is the whole of the rule's magnitude: `UCataclysmEnemyModifiers` explodes a
	// branded creature the same way and its comment says why -- "so a Herald's
	// brand is a Herald's brand". A creature the game mode never gave a damage to
	// answers zero, which `HellfireDamage` turns into zero and the check below
	// stops. `ACataclysmEnemyCharacter::StartingAttackDamage` says as much: "Zero
	// means it deals nothing."
	//
	// READ NOW, WHILE THE NOTICE IS BEING DISPATCHED. The creature is dead and
	// the actor is still valid at this point, which is what lets this ask; a
	// pointer kept past this function would not be safe to read.
	const UAbilitySystemComponent* Theirs =
		UCataclysmTargeting::AbilitySystemOf(Exploding);
	if (!Theirs)
	{
		return;
	}

	const float Damage = Effects::HellfireDamage(Theirs->GetNumericAttribute(
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute()));
	if (Damage <= 0.0f)
	{
		return;
	}

	// WHOSE NAME THE BLOW IS DEALT IN. `ApplyDirectDamage` refuses an instigator
	// with no ability system and the creature that exploded is dead, so the floor
	// carries it -- the same conclusion `StepArtilleryStrike` reached after
	// naming the circle made every strike land for nothing.
	AActor* Firing = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Firing)
	{
		return;
	}

	// EVERYONE STANDING IN IT, WHICH IS A RULING RECORDED IN `docs/DECISIONS.md`
	// AND NOT A DEFAULT: a dungeon hazard belongs to no side. The row does not
	// say who an exploding enemy catches, and a creature beside the one that
	// exploded is caught too. `StepArtilleryStrike` asks the same question the
	// same way, with the start and the end in one place, which is a circle.
	const TArray<AActor*> Caught = UCataclysmTargeting::FindEveryoneInLine(
		World, Firing, Notice.Location, Notice.Location, Effects::HellfireRadiusCm);

	// ONE BLOW AND NOT A LASTING FIRE, which is what separates this row from
	// `Pestilence_Spore_Clouds` beside it. `bIsArea` says it cannot be evaded;
	// leaving `bIsDamageOverTime` false says an energy shield absorbs it as it
	// absorbs any other blow. `StepArtilleryStrike` makes the same two choices
	// for the same reason.
	FCataclysmHitDelivery Delivery;
	Delivery.bIsArea = true;
	// AND TYPED BY THE ROW, since the source carries no type. Issue #1924.
	Delivery.DamageType = DungeonGameModeTypeOfRow(Effects::HellfireKey);

	for (AActor* Target : Caught)
	{
		if (!UCataclysmTargeting::AbilitySystemOf(Target))
		{
			continue;
		}

		UCataclysmSkillEffects::ApplyDirectDamage(Firing, Target, Damage, Delivery);
	}
}

void ACataclysmDungeonGameMode::NoteDeathForMortalDecay(
	const FCataclysmDeathNotice& Notice)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!FloorBrief.Modifiers.Contains(FName(Effects::MortalDecayKey)))
	{
		return;
	}

	// THE VICTIM MUST BE A CREATURE. The row says "reaping enemies", and this
	// notice is sent for every death on the floor, the player's included.
	if (!Cast<ACataclysmEnemyCharacter>(Notice.Victim))
	{
		return;
	}

	// AND THE PLAYER MUST HAVE BEEN THE ONE TO REAP IT, WHICH IS WHERE THIS
	// DIFFERS FROM THE TWO LISTENERS ABOVE. "the player must give death his due
	// souls by reaping enemies" names who does the killing, so a creature that
	// dies to a patch of burning ground, to another creature, or to anything
	// else on the floor buys nothing.
	//
	// THE SAME ROUTE TO THE PLAYER THE BEAT TAKES, so the two cannot disagree
	// about whose floor this is.
	UWorld* World = GetWorld();
	APlayerController* Controller =
		World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
	if (!World || !Player || Notice.Killer != Player)
	{
		return;
	}

	// PUSHED FORWARD RATHER THAN ADDED TO. A kill sets the window to its own
	// length from now, so killing steadily holds the slow and one kill never
	// buys more than the row's few seconds however many creatures fall at once.
	MortalDecaySlowedUntilSeconds =
		World->GetTimeSeconds() + Effects::MortalDecaySlowSeconds;
}

void ACataclysmDungeonGameMode::StepWitheredGround(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!Player || !AbilitySystem)
	{
		return;
	}

	// WHAT IS STILL THERE, ASKED RATHER THAN REMEMBERED. A patch is destroyed
	// with the rest of the floor's contents, so a weak pointer going invalid IS
	// that.
	WitheredGroundPatches.RemoveAll(
		[](const TWeakObjectPtr<ACataclysmGroundZone>& Patch)
		{
			return !Patch.IsValid();
		});

	// EACH PATCH IS ASKED WHETHER IT COVERS THE PLAYER. `Covers` is the same test
	// a zone's own sweep makes, so what reduces a character's recovery and what
	// the patch is drawn as cannot disagree about where the patch is.
	const FVector Feet = Player->GetActorLocation();
	bool bOnAPatch = false;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Patch : WitheredGroundPatches)
	{
		if (Patch.IsValid() && Patch->Covers(Feet))
		{
			bOnAPatch = true;
			break;
		}
	}

	// ONE PATCH OR SIX MAKE NO DIFFERENCE, and it matters more here than it does
	// for the two hazards placed at random: these are placed wherever creatures
	// die, which on a floor with a choke point is repeatedly the same few metres.
	// The row states one figure and says nothing about overlapping patches, and
	// stacking 80% twice would reach the pipeline's floor at once.
	const float Wanted = bOnAPatch ? Effects::WitheredGroundRecoveryLessPercent : 0.0f;
	if (!FMath::IsNearlyEqual(Wanted, WitheredGroundRecoveryLessApplied))
	{
		WitheredGroundRecoveryLessApplied = Wanted;
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}
}

void ACataclysmDungeonGameMode::StepLeechSpores(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	if (!Player || !AbilitySystem || UCataclysmSkillEffects::IsDead(Player))
	{
		return;
	}

	LeechSporesClouds.RemoveAll(
		[](const TWeakObjectPtr<ACataclysmGroundZone>& Cloud)
		{
			return !Cloud.IsValid();
		});

	// WHICH CLOUDS THE PLAYER IS TOUCHING, COLLECTED BEFORE ANY IS SPENT, so the
	// list is not changed underneath the loop that reads it.
	const FVector Feet = Player->GetActorLocation();
	TArray<ACataclysmGroundZone*> Touched;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Cloud : LeechSporesClouds)
	{
		if (Cloud.IsValid() && Cloud->Covers(Feet))
		{
			Touched.Add(Cloud.Get());
		}
	}
	if (Touched.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	for (ACataclysmGroundZone* Cloud : Touched)
	{
		// WHAT ACTUALLY LEAVES THE PLAYER, MEASURED RATHER THAN ASSUMED.
		// `ReduceHealthDirectly` stops at zero, so a player with less health than
		// the share loses less -- and the heal must be paid from what left, or
		// health would be created.
		const float Before = AbilitySystem->GetNumericAttribute(
			Vital::GetHealthAttribute());
		UCataclysmSkillEffects::ReduceHealthDirectly(
			Player, Player,
			Effects::LeechSporesDrain(AbilitySystem->GetNumericAttribute(
				Vital::GetMaxHealthAttribute())));
		const float Drained = FMath::Max(
			0.0f, Before - AbilitySystem->GetNumericAttribute(
							   Vital::GetHealthAttribute()));

		// EVERY CREATURE NEAR THE PLAYER, and only creatures. The search is asked
		// as the player, whose enemies are the creatures; anything else it finds
		// is not an enemy the row means and would take a share of the heal.
		TArray<UAbilitySystemComponent*> Healed;
		if (World)
		{
			for (AActor* Near : UCataclysmTargeting::FindEnemiesInSphere(
					 World, Player, Feet, Effects::LeechSporesHealRadiusCm))
			{
				if (!Cast<ACataclysmEnemyCharacter>(Near))
				{
					continue;
				}
				if (UAbilitySystemComponent* Theirs =
						UCataclysmTargeting::AbilitySystemOf(Near))
				{
					Healed.Add(Theirs);
				}
			}
		}

		// AN EQUAL SHARE EACH. A creature already at its maximum takes its share
		// and wastes it, because `TopUp` caps there -- so the total healed is never
		// more than the total drained.
		const float Each = Effects::LeechSporesHealEach(Drained, Healed.Num());
		for (UAbilitySystemComponent* Theirs : Healed)
		{
			UCataclysmRegeneration::TopUp(*Theirs, Vital::GetHealthAttribute(),
										  Vital::GetMaxHealthAttribute(), Each);
		}

		// SPENT. `ACataclysmGroundZone::EndPlay` ends the cloud's drawing for any
		// ending but a natural expiry, and a Destroy is one of those.
		LeechSporesClouds.RemoveAll(
			[Cloud](const TWeakObjectPtr<ACataclysmGroundZone>& Held)
			{
				return Held.Get() == Cloud;
			});
		Cloud->Destroy();
	}
}

void ACataclysmDungeonGameMode::StepBloodAltar(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem || !CurrentFloor
		|| !CurrentFloor->IsBuilt())
	{
		return;
	}

	const FCataclysmDungeonModifierRow* Row = UCataclysmDungeonModifierTable::FindRow(
		UCataclysmDungeonModifierTable::LoadDungeonModifierTable(),
		FName(Effects::BloodAltarKey));
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Row || !Source)
	{
		return;
	}

	// THE ALTAR, PLACED ONCE A FLOOR ON THE EXIT CELL. Drawn as its row's type, so its
	// colour is its own rather than whatever the shared source was last typed as.
	ACataclysmGroundZone* Ring = BloodAltarRing.Get();
	if (!Ring)
	{
		const FVector Altar = CurrentFloor->ExitWorld();
		Ring = ACataclysmGroundZone::SpawnForTheFloor(
			Source, Altar, Altar, Effects::BloodAltarReachCm, 0.0f,
			/*bAffectsEveryone=*/false, FName(*Row->CataclysmType));
		if (!Ring)
		{
			return;
		}
		BloodAltarRing = Ring;
	}

	BloodAltarSecondsSinceLastPulse += SecondsBetweenWaveChecks;
	if (!Effects::BloodAltarPulseIsDue(BloodAltarSecondsSinceLastPulse))
	{
		return;
	}
	BloodAltarSecondsSinceLastPulse = 0.0f;

	// THE PLAYER, FOUND THE WAY ARTILLERY STRIKE FINDS WHO IS UNDER ITS SHELL, so
	// "within reach" is measured to a body, as every area rule measures it. Anyone
	// else the search finds is left alone.
	const FVector Where = Ring->GetActorLocation();
	const TArray<AActor*> Inside = UCataclysmTargeting::FindEveryoneInLine(
		World, Source, Where, Where, Effects::BloodAltarReachCm);
	if (!Inside.Contains(Player))
	{
		return;
	}

	const float Damage = Effects::BloodAltarPulseDamage(
		AbilitySystem->GetNumericAttribute(
			UCataclysmVitalAttributeSet::GetMaxHealthAttribute()),
		BloodAltarDeaths);
	if (Damage <= 0.0f)
	{
		return;
	}

	// DEMONIC, CARRIED BY THE BLOW. The source is shared by every rule on the floor
	// and holds no type, so the pulse carries its own row's. Issue #1924.
	FCataclysmHitDelivery Delivery;
	Delivery.bIsArea = true;
	Delivery.DamageType = FName(*Row->CataclysmType);
	UCataclysmSkillEffects::ApplyDirectDamage(Source, Player, Damage, Delivery);
}

void ACataclysmDungeonGameMode::StepNecroticGround(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vital = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !AbilitySystem)
	{
		return;
	}

	NecroticGroundPatches.RemoveAll([](const TWeakObjectPtr<ACataclysmGroundZone>& Patch)
	{
		return !Patch.IsValid();
	});

	// IS THE PLAYER IN THE FOG, asked once for the cut and the burn.
	const FVector Feet = Player->GetActorLocation();
	bool bInTheFog = false;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Patch : NecroticGroundPatches)
	{
		if (Patch->Covers(Feet))
		{
			bInTheFog = true;
			break;
		}
	}

	// THE HEALING CUT, WRITTEN ONLY WHEN IT CHANGES, Withered Ground's shape: the
	// applier works the character's stats out again, which is not free.
	const float WantedLess = bInTheFog ? Effects::NecroticGroundHealingLessPercent : 0.0f;
	if (!FMath::IsNearlyEqual(WantedLess, NecroticGroundHealingLessApplied))
	{
		NecroticGroundHealingLessApplied = WantedLess;
		ApplyChangingFloorEffects(Player, AbilitySystem);
	}

	// THE BURN, ONCE A SECOND, FOR A PLAYER IN THE FOG AT THAT BEAT. Dealt here and not
	// by each patch's own sweep, because patches overlap and each sweep would take the
	// figure again from a player standing in two. Typed by the row, as every floor
	// rule's damage is since issue #1924.
	const FName Type = DungeonGameModeTypeOfRow(Effects::NecroticGroundKey);
	NecroticGroundSecondsSinceLastBurn += SecondsBetweenWaveChecks;
	if (NecroticGroundSecondsSinceLastBurn >= Effects::NecroticGroundSecondsBetweenBurns)
	{
		NecroticGroundSecondsSinceLastBurn = 0.0f;
		const float Burn = Effects::NecroticGroundBurn(
			AbilitySystem->GetNumericAttribute(Vital::GetMaxHealthAttribute()));
		ACataclysmFloorHazardSource* Burning = ACataclysmFloorHazardSource::Existing(World);
		if (bInTheFog && Burning && Burn > 0.0f)
		{
			FCataclysmHitDelivery Delivery;
			Delivery.bIsArea = true;
			Delivery.bIsDamageOverTime = true;
			Delivery.DamageType = Type;
			UCataclysmSkillEffects::ApplyDirectDamage(Burning, Player, Burn, Delivery);
		}
	}

	// THE CREATURES IN THE FOG REGENERATE, each counted once however many patches find
	// it, and a dead one not at all.
	TSet<UAbilitySystemComponent*> Regenerating;
	for (const TWeakObjectPtr<ACataclysmGroundZone>& Patch : NecroticGroundPatches)
	{
		const FVector Where = Patch->GetActorLocation();
		for (AActor* Creature : UCataclysmTargeting::FindEnemiesInLine(
				 World, Player, Where, Where, Effects::NecroticGroundPatchRadiusCm))
		{
			if (UCataclysmSkillEffects::IsDead(Creature))
			{
				continue;
			}
			if (UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Creature))
			{
				Regenerating.Add(Theirs);
			}
		}
	}
	for (UAbilitySystemComponent* Theirs : Regenerating)
	{
		UCataclysmRegeneration::TopUp(
			*Theirs, Vital::GetHealthAttribute(), Vital::GetMaxHealthAttribute(),
			Effects::NecroticGroundRegenPerBeat(
				Theirs->GetNumericAttribute(Vital::GetMaxHealthAttribute()),
				SecondsBetweenWaveChecks));
	}

	// AND THE FOG SPREADS ON ITS CADENCE, up to its cap.
	NecroticGroundSecondsSinceLastPatch += SecondsBetweenWaveChecks;
	if (!Effects::NecroticGroundPatchIsDue(NecroticGroundSecondsSinceLastPatch,
										   NecroticGroundPatches.Num()))
	{
		return;
	}
	ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor(World);
	if (!Source)
	{
		return;
	}

	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	FVector From;
	float Away = 0.0f;
	if (NecroticGroundPatches.IsEmpty())
	{
		// THE FIRST PATCH NEAR THE PLAYER BUT NOT ON THEM, Infernal Rain's placement:
		// strictly past its own radius, because a patch's cover includes its edge.
		From = Player->GetActorLocation();
		Away = FMath::FRandRange(Effects::NecroticGroundPatchRadiusCm + 1.0f,
								 Effects::NecroticGroundFirstPatchWithinCm);
	}
	else
	{
		// EVERY LATER PATCH TOUCHES A RANDOM ONE ALREADY THERE, its centre one
		// patch-width from that patch's centre.
		const int32 Pick = FMath::RandRange(0, NecroticGroundPatches.Num() - 1);
		From = NecroticGroundPatches[Pick]->GetActorLocation();
		Away = Effects::NecroticGroundSpreadCm;
	}
	const FVector Where(From.X + Away * FMath::Cos(Angle),
						From.Y + Away * FMath::Sin(Angle),
						From.Z);

	// NO DAMAGE ON THE PATCH ITSELF, and drawn in the row's colours. The burn above is
	// the damage; a patch that dealt its own would take it twice where patches overlap.
	ACataclysmGroundZone* Patch = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Where, Where, Effects::NecroticGroundPatchRadiusCm, 0.0f,
		/*bAffectsEveryone=*/false, /*InDrawnAsType=*/Type);
	if (!Patch)
	{
		// THE CLOCK IS NOT RESET ON A FAILED SPAWN, so the next beat tries again.
		return;
	}
	NecroticGroundPatches.Add(Patch);
	NecroticGroundSecondsSinceLastPatch = 0.0f;
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepRavenousHoard(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player))
	{
		return;
	}

	// EVERY CREATURE ON THE PLAYER'S OTHER SIDE, WHEREVER IT CAME FROM. `IsHostileTo`
	// also turns away the dead. The player's minions are `ACataclysmMinion`, which this
	// never iterates.
	int32 Strongest = 0;
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		ACataclysmEnemyCharacter* Creature = *It;
		if (!IsValid(Creature) || !UCataclysmTargeting::IsHostileTo(Creature, Player))
		{
			continue;
		}

		// ITS OWN CLOCK, STARTED BY THE FIRST BEAT THAT FINDS IT AND COUNTING THAT BEAT,
		// so a creature placed before a beat holds its first stack on its fortieth:
		// Death's Embrace's convention, and at most a beat after it was placed.
		float& SecondsAlive = RavenousHoardSecondsAlive.FindOrAdd(Creature);
		SecondsAlive += SecondsBetweenWaveChecks;

		// THE SETTER RETURNS AT ONCE WHEN THE MULTIPLIER HAS NOT CHANGED, so this writes
		// nothing between stacks.
		const int32 Stacks = Effects::RavenousHoardStacksAfter(SecondsAlive);
		Creature->SetTimeAliveDamageMultiplier(
			Effects::RavenousHoardDamageMultiplier(Stacks));
		Strongest = FMath::Max(Strongest, Stacks);
	}

	// THE DESTROYED AND THE DEAD ARE FORGOTTEN, so no clock is kept for them. A stale key
	// is a destroyed creature.
	for (auto Entry = RavenousHoardSecondsAlive.CreateIterator(); Entry; ++Entry)
	{
		const ACataclysmEnemyCharacter* Creature = Entry.Key().Get();
		if (Entry.Key().IsStale() || (Creature && UCataclysmSkillEffects::IsDead(Creature)))
		{
			Entry.RemoveCurrent();
		}
	}

	if (Strongest != RavenousHoardStrongest)
	{
		RavenousHoardStrongest = Strongest;
		RefreshFloorModifierPanel();
	}
}

// THE CEILING ON A MUTATION AND THE FIRST BOSS RUNG ARE ONE FACT WRITTEN TWICE, so they
// are compared here, where the ceiling is used. The rule library cannot do it: it holds
// figures for the tests and the Python checks to read and does not include the creature
// class.
static_assert(
	UCataclysmDungeonModifierEffects::VolatileEvolutionHighestRung
		== ACataclysmEnemyCharacter::FirstBossRarityStep - 1,
	"Volatile Evolution's ceiling is the rung under the first boss rung. If the rarity "
	"ladder gains or loses a rung, a floor rule must not start making bosses out of "
	"ordinary creatures in the middle of a fight.");

void ACataclysmDungeonGameMode::StepVolatileEvolution(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vitals = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player))
	{
		return;
	}

	const int32 Before = VolatileEvolutionMutations;
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		ACataclysmEnemyCharacter* Creature = *It;
		if (!IsValid(Creature) || !UCataclysmTargeting::IsHostileTo(Creature, Player))
		{
			continue;
		}

		// ONE CREATURE MUTATES ONCE, and the membership is what says so. Ruled under the
		// project owner's delegation; the row says only that enemies have a chance.
		if (VolatileEvolutionMutated.Contains(Creature))
		{
			continue;
		}

		// AND NEVER PAST HERALD. `VolatileEvolutionRungAfter` holds the ceiling, so a
		// creature already at it is answered with the rung it is on and is skipped here
		// rather than being counted as a mutation that changed nothing.
		const int32 Rung = Effects::VolatileEvolutionRungAfter(Creature->RarityStep);
		if (Rung <= Creature->RarityStep)
		{
			continue;
		}

		UAbilitySystemComponent* Abilities =
			UCataclysmTargeting::AbilitySystemOf(Creature);
		if (!Abilities)
		{
			continue;
		}

		const float Health = Abilities->GetNumericAttribute(Vitals::GetHealthAttribute());
		const float MaxHealth =
			Abilities->GetNumericAttribute(Vitals::GetMaxHealthAttribute());
		if (!Effects::VolatileEvolutionIsWounded(Health, MaxHealth))
		{
			continue;
		}

		if (DungeonGameModeVolatileEvolutionRoll()
			>= Effects::VolatileEvolutionChancePercent)
		{
			continue;
		}

		// WHAT IT HAD IN BOTH POOLS, READ BEFORE ANYTHING IS WRITTEN. The two calls
		// below each end in `ApplyStartingAttributes`, which refills health and energy
		// shield to the new maximums.
		const float Shield =
			Abilities->GetNumericAttribute(Vitals::GetEnergyShieldAttribute());

		Creature->SetRarityStep(Rung);

		// AND THE MODIFIERS THE NEW RUNG CARRIES. `DrawModifiersForRarity` draws only the
		// shortfall and never draws one the creature already holds, so a creature that
		// rises from Common to Elite gains exactly one.
		Creature->DrawModifiersForRarity();

		// NOW PUT BOTH POOLS BACK, HELD TO THE NEW MAXIMUMS. The maximums are read again
		// because the rung is what moved them.
		Abilities->SetNumericAttributeBase(
			Vitals::GetHealthAttribute(),
			FMath::Min(Health,
					   Abilities->GetNumericAttribute(Vitals::GetMaxHealthAttribute())));
		Abilities->SetNumericAttributeBase(
			Vitals::GetEnergyShieldAttribute(),
			FMath::Min(Shield,
					   Abilities->GetNumericAttribute(
						   Vitals::GetMaxEnergyShieldAttribute())));

		// AND IF IT IS A WRAITH, THE FIGURES THAT MAKE IT ONE GO BACK ON, for the reason
		// written where Blood-Forged Champions does the same: the two calls above have
		// just written this creature's whole stat block over.
		ApplyVengefulWraithFigures(Creature);
		ApplyNothingIsForgottenFigures(Creature);
		ApplySoulHarvestFigures(Creature, /*bFreshBlock=*/true);

		VolatileEvolutionMutated.Add(Creature);
		++VolatileEvolutionMutations;

		UE_LOG(LogCataclysm, Log,
			   TEXT("Volatile Evolution: %s mutated to rarity step %d, keeping %.1f "
					"health and %.1f energy shield"),
			   *Creature->GetName(), Rung, Health, Shield);
	}

	// THE DESTROYED ARE FORGOTTEN. A creature that died is left where it is: it cannot
	// mutate again in any case, and dropping it would let a resurrection of it -- which
	// no row has yet -- mutate a second time.
	for (auto Entry = VolatileEvolutionMutated.CreateIterator(); Entry; ++Entry)
	{
		if (Entry->IsStale())
		{
			Entry.RemoveCurrent();
		}
	}

	if (VolatileEvolutionMutations != Before)
	{
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::StepRoyalGuard(ACataclysmPlayerCharacter* Player)
{
	using Effects = UCataclysmDungeonModifierEffects;
	using Vitals = UCataclysmVitalAttributeSet;

	UWorld* World = GetWorld();
	if (!World || !IsValid(Player) || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	// WHO IS ON THE FLOOR WHEN THE BEAT STARTS, COLLECTED BEFORE ANY GUARD IS SPAWNED.
	// Spawning inside a `TActorIterator` walk adds actors to what it is walking, so a
	// guard could be offered its own roll on the beat it arrived.
	TArray<ACataclysmEnemyCharacter*> Standing;
	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		ACataclysmEnemyCharacter* Creature = *It;
		if (IsValid(Creature) && UCataclysmTargeting::IsHostileTo(Creature, Player))
		{
			Standing.Add(Creature);
		}
	}

	const int32 Before = RoyalGuardGuardsArrived;
	for (ACataclysmEnemyCharacter* Creature : Standing)
	{
		if (!IsValid(Creature) || RoyalGuardRolled.Contains(Creature))
		{
			continue;
		}

		// ELITE AND ABOVE ONLY, WHICH IS THE READING OF "ABOVE UNCOMMON RANKED". The rule
		// library says what the row's word names and why it needed a ruling.
		if (!Effects::RoyalGuardMaySummon(Creature->RarityStep))
		{
			continue;
		}

		UAbilitySystemComponent* Abilities =
			UCataclysmTargeting::AbilitySystemOf(Creature);
		if (!Abilities)
		{
			continue;
		}
		const float Health = Abilities->GetNumericAttribute(Vitals::GetHealthAttribute());
		const float MaxHealth =
			Abilities->GetNumericAttribute(Vitals::GetMaxHealthAttribute());
		if (!Effects::RoyalGuardIsWounded(Health, MaxHealth))
		{
			continue;
		}

		// THE ROLL IS REMEMBERED WHETHER OR NOT IT SUCCEEDS, so a creature that falls
		// below the share is offered one chance and not one a beat.
		RoyalGuardRolled.Add(Creature);

		if (DungeonGameModeRoyalGuardRoll() >= Effects::RoyalGuardChancePercent)
		{
			continue;
		}

		const ECataclysmDungeonCreature Kind = DungeonGameModeKindOf(Creature);
		if (Kind == ECataclysmDungeonCreature::Count)
		{
			UE_LOG(LogCataclysm, Log,
				   TEXT("Royal Guard: %s is none of the kinds this dungeon places, so no "
						"guards were called"),
				   *Creature->GetName());
			continue;
		}

		FCataclysmEnemyPlacement Placement;
		Placement.Cell = CurrentFloor->CellOfWorld(Creature->GetActorLocation());
		Placement.Creature = Kind;

		const int32 Rung = Effects::RoyalGuardRungForGuards(Creature->RarityStep);
		int32 Arrived = 0;
		for (int32 Guard = 0; Guard < Effects::RoyalGuardGuardsSummoned; ++Guard)
		{
			ACataclysmEnemyCharacter* Called =
				SpawnPlacedCreature(Placement, FloorBrief.SightRadiusMultiplier);
			if (!Called)
			{
				continue;
			}

			// THE RUNG IS WRITTEN AFTER THE SPAWN, BECAUSE THE SPAWN SETS ITS OWN.
			// `SpawnPlacedCreature` calls `ApplyDesignedStats`, which gives the creature
			// its kind's rung; a guard's rung is the summoner's plus one, held to the
			// ceiling. The draw tops up the modifiers the new rung carries, and the
			// refill both calls end in is right here: a guard arrives whole.
			Called->SetRarityStep(Rung);
			Called->DrawModifiersForRarity();
			FloorEnemies.Add(Called);
			++Arrived;
		}

		RoyalGuardGuardsArrived += Arrived;

		UE_LOG(LogCataclysm, Log,
			   TEXT("Royal Guard: %s (%s, rarity step %d) called %d guard%s at rarity "
					"step %d"),
			   *Creature->GetName(), CataclysmDungeonCreatureName(Kind),
			   Creature->RarityStep, Arrived, Arrived == 1 ? TEXT("") : TEXT("s"), Rung);
	}

	// THE DESTROYED ARE FORGOTTEN, so the record does not grow from floor to floor. A
	// creature that died keeps its entry until it is destroyed, which costs nothing: it
	// cannot roll again in any case.
	for (auto Entry = RoyalGuardRolled.CreateIterator(); Entry; ++Entry)
	{
		if (Entry->IsStale())
		{
			Entry.RemoveCurrent();
		}
	}

	if (RoyalGuardGuardsArrived != Before)
	{
		RefreshFloorModifierPanel();
	}
}

void ACataclysmDungeonGameMode::StepGraveTide()
{
	using Effects = UCataclysmDungeonModifierEffects;

	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return;
	}

	GraveTideSecondsSinceLastWave += SecondsBetweenWaveChecks;
	if (!Effects::GraveTideWaveIsDue(GraveTideSecondsSinceLastWave, GraveTideWaves))
	{
		return;
	}

	// WHERE THEY STAND IS THE FLOOR POPULATOR'S ANSWER, asked for this floor's own plan
	// and density, so a wave cannot put a creature anywhere the floor would not.
	const FCataclysmFloorPopulation Population = FCataclysmFloorPopulator::Populate(
		CurrentFloor->GetPlan(), ChooseEnemyScale(), FloorBrief);
	if (Population.Enemies.IsEmpty())
	{
		// NOTHING TO PLACE IS NOT A WAVE, and the clock is left alone so the next beat
		// asks again rather than skipping a whole cadence.
		return;
	}

	const int32 Wanted = Effects::GraveTideCreaturesInWave(GraveTideWaves);
	const float Multiplier = Effects::GraveTideDamageMultiplier(GraveTideWaves);

	int32 Placed = 0;
	for (int32 Which = 0; Which < Wanted; ++Which)
	{
		// DRAWN FROM THE WHOLE POPULATION, so a wave is spread as the floor is rather
		// than gathered where the list happens to start.
		const FCataclysmEnemyPlacement& Placement =
			Population.Enemies[FMath::RandRange(0, Population.Enemies.Num() - 1)];
		ACataclysmEnemyCharacter* Risen =
			SpawnPlacedCreature(Placement, FloorBrief.SightRadiusMultiplier);
		if (!Risen)
		{
			continue;
		}

		// THE WAVE'S OWN STRENGTH, SET ONCE. Ravenous Hoard writes the other multiplier
		// every beat, and neither touches the other's.
		Risen->SetPlacedDamageMultiplier(Multiplier);

		// THE FLOOR'S LIST, so a floor change disposes of these creatures with the rest.
		// NOT `CurrentWave`, which counts a Horde wave's own creatures and decides when
		// the next one arrives.
		FloorEnemies.Add(Risen);
		++Placed;
	}

	if (Placed <= 0)
	{
		return;
	}

	++GraveTideWaves;
	GraveTideSecondsSinceLastWave = 0.0f;
	UE_LOG(LogCataclysm, Verbose,
		   TEXT("Grave Tide: wave %d of %d put %d creature%s on floor %d at %.2f "
				"times their own damage."),
		   GraveTideWaves, Effects::GraveTideMostWaves, Placed,
		   Placed == 1 ? TEXT("") : TEXT("s"), FloorNumber, Multiplier);
	RefreshFloorModifierPanel();
}

void ACataclysmDungeonGameMode::StepHolyRepercussions(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (JudgmentStacks == JudgmentStacksApplied)
	{
		return;
	}

	JudgmentStacksApplied = JudgmentStacks;
	ApplyChangingFloorEffects(Player, AbilitySystem);
}

void ACataclysmDungeonGameMode::StepFungalOvergrowth(
	ACataclysmPlayerCharacter* Player,
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	using Effects = UCataclysmDungeonModifierEffects;

	if (!Player || !AbilitySystem)
	{
		return;
	}

	// WHAT IS STILL THERE, ASKED RATHER THAN REMEMBERED, exactly as
	// `StepWitheredGround` asks it: a mushroom is destroyed with the rest of the
	// floor's contents, so a weak pointer going invalid IS that.
	const auto ForgetTheGone =
		[](TArray<TWeakObjectPtr<ACataclysmGroundZone>>& List)
		{
			List.RemoveAll([](const TWeakObjectPtr<ACataclysmGroundZone>& One)
			{
				return !One.IsValid();
			});
		};
	ForgetTheGone(FungalOvergrowthBoostMushrooms);
	ForgetTheGone(FungalOvergrowthSlowMushrooms);

	// EACH MUSHROOM IS ASKED WHETHER IT COVERS THE PLAYER. `Covers` is the same
	// test a zone's own sweep makes, so what changes a character's speed and what
	// the mushroom is drawn as cannot disagree about where it is.
	const FVector Feet = Player->GetActorLocation();
	const auto StandingOnOneOf =
		[&Feet](const TArray<TWeakObjectPtr<ACataclysmGroundZone>>& List)
		{
			for (const TWeakObjectPtr<ACataclysmGroundZone>& One : List)
			{
				if (One.IsValid() && One->Covers(Feet))
				{
					return true;
				}
			}
			return false;
		};

	const float WantedMore = StandingOnOneOf(FungalOvergrowthBoostMushrooms)
		? Effects::FungalOvergrowthSpeedMorePercent
		: 0.0f;
	const float WantedLess = StandingOnOneOf(FungalOvergrowthSlowMushrooms)
		? Effects::FungalOvergrowthSpeedLessPercent
		: 0.0f;

	// BOTH ARE COMPARED BEFORE EITHER IS WRITTEN, so a beat that changes only one
	// of them still asks for the refresh, and a beat that changes neither asks
	// for nothing. Writing one and testing the other would leave a player who
	// stepped from a helping mushroom straight onto a hurting one carrying both.
	if (FMath::IsNearlyEqual(WantedMore, FungalOvergrowthSpeedMoreApplied)
		&& FMath::IsNearlyEqual(WantedLess, FungalOvergrowthSpeedLessApplied))
	{
		return;
	}

	FungalOvergrowthSpeedMoreApplied = WantedMore;
	FungalOvergrowthSpeedLessApplied = WantedLess;
	ApplyChangingFloorEffects(Player, AbilitySystem);
}

void ACataclysmDungeonGameMode::ApplyFloorRulesToPlayer()
{
	UWorld* World = GetWorld();

	// THE ZONES THE LAST FLOOR'S RULES PLACED ARE DESTROYED FIRST, WITH OR WITHOUT A
	// PLAYER. Issue #1925. `GoToFloor` clears the world only when the next floor is
	// a new arena, and a Horde dungeon's waves share one arena, so until this each
	// rule's zones stayed on the next wave with no rule acting for them. Read from
	// `ACataclysmGroundZone::Sweep`, not measured: a zone that deals damage kept
	// sweeping, so a Singularity Well, which never expires, went on hurting any
	// player who stood in it for the rest of the dungeon.
	//
	// EVERY ONE GOES, including patches and craters still burning and an Artillery
	// Strike circle whose shell has not landed. That shell then never lands: a
	// circle drawn on the last floor is not a warning about this one. The rules'
	// lists below are still emptied, because each rule counts its own against its
	// cap. See `DungeonGameModeDestroyTheRulesZones` for why this goes by owner.
	const int32 RuleZonesDestroyed = DungeonGameModeDestroyTheRulesZones(World);
	UE_LOG(LogCataclysm, Verbose,
		   TEXT("Floor %d: %d ground zones the last floor's rules placed were destroyed."),
		   FloorNumber, RuleZonesDestroyed);

	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;

	// NO PLAYER DURING THE FIRST `GoToFloor` OF `StartPlay`, whose pawn is made
	// later by the parent's login. `StartPlay` calls this again once it exists.
	if (ACataclysmPlayerCharacter* Player =
			Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr)
	{
		// SCARCITY FIRST, so the attribute refresh inside `ApplyFloorRulesTo` already
		// leaves the switched-off slot out. Issues #1820 and #41.
		ChooseTheScarceSlot(Player->GetEquipment());

		ApplyFloorRulesTo(
			Cast<UCataclysmAbilitySystemComponent>(Player->GetAbilitySystemComponent()),
			Player->GetEquipment());

		// AND WHAT THE BEAT HAD PUT ON THE PLAYER IS GONE WITH IT. Issue #41,
		// slice 2. The call above replaces the floor's modifiers wholesale, so
		// the resistance The Nihil's Embrace had taken is no longer on the
		// character; forgetting it here is what makes the next beat put it back.
		ResistanceLessApplied = 0.0f;
		ResistanceMoreApplied = 0.0f;

		// AND DEATH'S EMBRACE'S STACKS GO WITH THE FLOOR, which its row states
		// outright: "Stacks reset when entering a new floor." Issue #41, slice 5.
		// It is the only one of these rules whose reset the data asks for rather
		// than the code needing it, and it is the same line, because the call
		// above has already taken the reduction off the character.
		DeathsEmbraceSecondsOnFloor = 0.0f;
		DeathsEmbraceStacksApplied = 0;

		// AND A PART-BUILT BRAND GOES WITH THE FLOOR. The row does not ask for
		// this, unlike Death's Embrace above; the code wants it, because a count
		// carried across a loading screen would erupt on a floor the player had
		// not yet hit anything on. No applied figure to clear beside it: a brand
		// puts nothing standing on the character.
		BrandStacks = 0;

		// AND INFERNAL RAIN FORGETS BOTH ITS CLOCK AND ITS PATCHES. The clock so
		// the first patch of a floor does not arrive on its first beat carrying
		// the last floor's wait; the list because those actors are already
		// destroyed -- see the top of this function -- and a stale list would count
		// them against the cap and stop the rain entirely.
		InfernalRainSecondsSinceLastPatch = 0.0f;
		InfernalRainPatches.Empty();

		// AND SINGULARITY WELLS FORGETS ITS CLOCK, ITS WELLS AND ITS SLOW. Issues
		// #1605 and #41. The clock so the first well of a floor does not arrive on
		// its first beat carrying the last floor's wait; the list because those
		// actors are already destroyed -- see the top of this function -- and a
		// stale list would count them against the cap and stop the wells entirely;
		// the slow because the call above has already taken it off the character,
		// so leaving the figure here would make the next beat believe it was still
		// applied and never put it back.
		SingularityWellsSecondsSinceLastWell = 0.0f;
		SingularityWells.Empty();
		SingularityWellsSlowApplied = 0.0f;

		// AND WITHERED GROUND FORGETS ITS PATCHES AND ITS REDUCTION. Issue #41.
		// TWO LINES AND NOT THREE, because this rule holds no clock: its patches
		// are placed by deaths rather than by a cadence, so there is no wait to
		// carry across a floor. The list because those actors are already
		// destroyed -- see the top of this function; the reduction because the
		// call above has already taken it off the character, so leaving the figure
		// here would make the next beat believe it was still applied and never put
		// it back.
		WitheredGroundPatches.Empty();
		WitheredGroundRecoveryLessApplied = 0.0f;

		// AND ETERNAL CHORUS'S EFFECTS ON THE PLAYER, for Withered Ground's reason: the call above has
		// taken them off, and the next beat puts them back if the player is still within earshot. Its
		// earshot zones went with the rules' other zones; the beat draws them again for every source
		// still singing. Issues #1820 and #41.
		EternalChorusCooldownApplied = 0.0f;
		EternalChorusRegenApplied = 0.0f;

		// AND JUDGMENT GOES ENTIRELY, BOTH NUMBERS. Issues #1820 and #41. This
		// is the opposite of Wasting Sickness further below, which keeps
		// its count because its row calls the debuff "permanent for the duration
		// of the dungeon". This row says nothing of the kind, and its stacks come
		// from creatures the player has left behind on the last floor.
		JudgmentStacks = 0;
		JudgmentStacksApplied = 0;

		// AND LEECH SPORES FORGETS ITS CLOUDS, which are already destroyed -- see the
		// top of this function. Nothing else to clear: a cloud's drain is done the
		// moment it is touched.
		LeechSporesClouds.Empty();

		// AND BLOOD ALTAR STARTS AGAIN: no deaths, a fresh clock, and its ring
		// forgotten. The ring is destroyed at the top of this function with every
		// other zone the floor's rules placed (issue #1925), and the next beat
		// places a new ring at the new floor's exit.
		BloodAltarRing = nullptr;
		BloodAltarDeaths = 0;
		BloodAltarSecondsSinceLastPulse = 0.0f;

		// AND NECROTIC GROUND FORGETS ITS PATCHES, BOTH CLOCKS AND ITS HEALING CUT.
		// Issues #1820 and #41. The patches are already destroyed -- see the top of this
		// function; the cut because the call above has already taken it off the
		// character, so leaving the figure would make the next beat believe it was
		// still applied.
		NecroticGroundPatches.Empty();
		NecroticGroundSecondsSinceLastPatch = 0.0f;
		NecroticGroundSecondsSinceLastBurn = 0.0f;
		NecroticGroundHealingLessApplied = 0.0f;

		// AND RAVENOUS HOARD FORGETS EVERY CLOCK AND ITS STRONGEST COUNT, AND EVERY
		// CREATURE STILL STANDING GETS ITS OWN DAMAGE BACK. Issues #1820 and #41. A
		// Horde dungeon's next wave shares the arena, so a creature can live through the
		// change; with its clock gone its stacks are gone, and the next floor may not
		// carry the row at all. Every creature in the world and not only the ones in the
		// clocks, so the damage cannot outlive a clock that was lost.
		for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
		{
			It->SetTimeAliveDamageMultiplier(1.0f);
			It->SetPlacedDamageMultiplier(1.0f);

			// AND MARCH OF PROGRESS' MULTIPLIER WITH THEM. Issues #1820 and #41. A
			// creature that lives through a Horde dungeon's change of wave would
			// otherwise keep the last floor's figure, and the next floor may not carry
			// the row at all. The next beat sets it again for a floor that does.
			It->SetFloorDepthDamageMultiplier(1.0f);
		}
		RavenousHoardSecondsAlive.Empty();
		RavenousHoardStrongest = 0;

		// AND GRAVE TIDE FORGETS ITS CLOCK AND ITS WAVES. Issues #1820 and #41. The
		// creatures its waves placed are in `FloorEnemies` and go the way every other
		// creature on the floor goes; the loop above puts back the damage of any that
		// lives through a Horde dungeon's change of wave.
		GraveTideSecondsSinceLastWave = 0.0f;
		GraveTideWaves = 0;

		// AND VOLATILE EVOLUTION FORGETS ITS COUNT AND NOT ITS MEMBERSHIP. Issues #1820
		// and #41. The count is what mutated on this floor and the panel says so. The
		// creatures themselves keep the rung they reached -- nothing puts a rarity back
		// -- so a creature that lives through a Horde dungeon's change of wave has had
		// its one mutation and must not be offered another.
		VolatileEvolutionMutations = 0;

		// AND ROYAL GUARD FORGETS ITS COUNT AND NOT ITS ROLLS. Issues #1820 and #41. The
		// count is how many guards arrived on this floor. The creatures that rolled keep
		// their entry, so one that lives through a Horde dungeon's change of wave has
		// had its one chance; the guards themselves are in `FloorEnemies` and go the way
		// every other creature on the floor goes.
		RoyalGuardGuardsArrived = 0;

		// AND DEMON PRINCE FORGETS THAT ONE ROSE. Issues #1820 and #41. The ceiling is
		// one a floor, so the next floor may have its own; the creature that rose is in
		// `FloorEnemies` and goes the way every other creature on the floor goes.
		DemonPrincesRisen = 0;

		// AND EPIDEMIC FORGETS ITS CHAIN AND ITS LORD. Issues #1820 and #41. A chain is
		// spreads in a row on one floor, and the next floor may have its own Plague Lord.
		EpidemicChain = 0;
		EpidemicPlagueLordsRisen = 0;

		// AND BLOOD-FORGED CHAMPIONS FORGETS THE FLOOR'S TWO COUNTS AND NOT THE TALLIES.
		// Issues #1820 and #41. The counts are what the panel shows about THIS floor. The
		// tallies stay, for the reason written beside them: a champion part way to its
		// next rung keeps that progress the way it keeps the rung it already reached.
		BloodForgedChampionsAbsorbed = 0;
		BloodForgedChampionsRungsGained = 0;

		// AND VENGEFUL WRAITHS FORGETS HOW MANY ROSE AND NOT WHICH CREATURES ARE WRAITHS.
		// Issues #1820 and #41. The count is this floor's; a wraith that lives through a
		// Horde dungeon's change of wave is still a wraith and still keeps its figures.
		VengefulWraithsRisen = 0;

		// AND DIVINE RESURGENCE FORGETS EVERYTHING, ONCE PER FLOOR BEING THE ROW'S OWN
		// WORDS. Issues #1820 and #41. A creature it raised that lives through a Horde
		// dungeon's change of wave keeps its mark, so the next floor neither counts it
		// as placed nor raises it again.
		DivineResurgenceGraves.Reset();
		DivineResurgenceFallen = 0;
		bDivineResurgenceDone = false;
		DivineResurgenceRisen = 0;

		// AND DEAD RISING FORGETS HOW MANY GOT UP. Issues #1820 and #41. A creature it
		// put back that lives through a Horde dungeon's change of wave keeps its mark, so
		// it never rolls again on the next floor either.
		DeadRisingRisen = 0;

		// AND BLOOD GATES FORGETS THE PLAYER'S KILLS: each floor's stairs are sealed
		// afresh. Issues #1820 and #41.
		BloodGatesSlain = 0;

		// AND UNSTABLE PORTAL FORGETS ITS ROLLS AND ITS WARDENS. Issues #1820 and #41.
		UnstablePortalRolls = 0;
		UnstablePortalLast = -1;
		CreaturesRaisedByARule.Reset();

		// AND THE REAPER'S CLOCK STARTS AGAIN: it went with the last floor's creatures, and
		// a floor carrying the row raises a new one ten seconds in. Issues #1820 and #41.
		TheReaperSecondsOnFloor = 0.0f;
		bTheReaperRaised = false;
		TheReaper.Reset();

		// AND BLOOD BOND LETS GO: a bond is this floor's. A Horde wave keeps its creatures, so
		// an elite still bonded is made mortal again rather than left unable to die with
		// nothing tying it to the player. Issues #1820 and #41.
		if (ACataclysmEnemyCharacter* Held = BloodBonded.Get())
		{
			Held->bCannotBeHurt = false;
		}
		BloodBonded.Reset();
		bBloodBondFormed = false;

		// AND PLAGUE CONVERGENCE STARTS AGAIN: descending is the one thing that stops it, so the
		// clock, the creatures it counts and the disease all go back. Issues #1820 and #41.
		PlagueConvergenceSecondsOnFloor = 0.0f;
		PlagueConvergenceSecondsSinceWave = 0.0f;
		PlagueConvergenceCreatures.Reset();
		PlagueConvergenceStacks = 0;
		PlagueConvergenceSecondsSinceBurn = 0.0f;

		// AND DIVINE WRATH STARTS ITS CLOCK AGAIN; the last floor's beam went with its zones.
		// Issues #1820 and #41.
		DivineWrathSecondsSinceLast = 0.0f;
		DivineWrathBeam.Reset();
		DivineWrathDestroyed = 0;

		// AND ECHOES OF THE PAST STARTS ITS CLOCK AGAIN, and any echo still standing goes: the
		// records themselves are handed over where a floor begins, in `GoToFloor`, which runs
		// once a floor where this runs twice for the first. Issues #1820 and #41.
		DismissTheEchoes();
		EchoesSecondsOnFloor = 0.0f;
		EchoesStage = 0;

		// AND DIRGE RESONANCE STARTS ITS NINETY SECONDS AGAIN: the first crescendo on a
		// floor comes ninety seconds into it. Issues #1820 and #41. A haste already
		// granted runs out on its own.
		DirgeResonanceSecondsSinceLast = 0.0f;
		DirgeResonanceHastedUntilSeconds = -1.0f;
		DirgeResonanceShownSeconds = -1;

		// AND JUDGMENT ZONES FORGETS EVERYTHING, which is the whole of its state.
		// Issues #1820 and #41. The zones themselves are actors on the floor being left
		// and go the way every other actor on it goes; the clock, the ramp and the
		// trigger count are all this floor's and none of them belongs to a creature.
		JudgmentZones.Reset();
		JudgmentZonesSecondsSinceLastZone = 0.0f;
		JudgmentZonesStandingIn = nullptr;
		JudgmentZonesSecondsInside = 0.0f;
		JudgmentZonesTicksInThisZone = 0;
		JudgmentZonesTriggers = 0;

		// AND ANTI-MAGIC ZONES FORGETS ITS ZONES, ITS CLOCK AND ITS LOCK. Issues #1820
		// and #41. Singularity Wells' three: the list because those zones are already
		// destroyed -- see the top of this function; the clock so the first zone of a floor
		// does not arrive carrying the last floor's wait; the lock because the call above
		// has already taken it off the character. The list and the lock would also be put
		// right by the next beat, which prunes dead zones before counting and finds the
		// player outside every zone -- the declaration says why no test can see those two
		// -- so they are cleared here so the fields never describe a floor that is gone.
		AntiMagicZones.Reset();
		AntiMagicZonesSecondsSinceLastZone = 0.0f;
		AntiMagicZonesLockApplied = 0.0f;

		// AND MARCH OF PROGRESS FORGETS WHAT ARMOUR IS STANDING ON THE PLAYER, BECAUSE
		// THE CALL ABOVE HAS ALREADY TAKEN IT OFF. Issues #1820 and #41. Leaving the
		// figure would make the next beat believe the armour was still on and never put
		// it back. Wasting Sickness above keeps its count and drops its applied figure
		// for exactly this reason, and the count of commanders killed is kept here for
		// exactly that reason too: the row pays it "in each level" and never takes it
		// back, so only `LeaveEmpireDungeon` clears it.
		//
		// THIS FLOOR'S COMMANDER IS NOT FORGOTTEN HERE, AND THAT IS NOT AN OVERSIGHT.
		// `GoToFloor` calls `PopulateFloor` and then this, so a Commander cleared here
		// would be the one the population pass had just chosen -- every floor would have
		// none. `PopulateFloor` forgets it before it places anything instead.
		MarchOfProgressArmourApplied = 0.0f;

		// AND COMMANDER'S AURA FORGETS ITS COUNT, WHICH IS THE WHOLE OF ITS STATE. Issues
		// #1820 and #41. The count is what the panel shows about THIS floor, and the next
		// beat writes it again from whatever is standing there.
		//
		// THE BUFF ITSELF IS NOT STRIPPED, AND THAT IS A JUDGEMENT RATHER THAN AN
		// OVERSIGHT. It lasts one second and is re-applied four times a second, so a
		// creature that lives through a Horde dungeon's change of wave loses it within a
		// second on a floor that does not carry this row. Stripping it here would also
		// take a Succubus's grant off its allies, because removing an effect by its tag
		// cannot tell which rule granted it.
		CommandersAuraCommanders = 0;

		// AND FUNGAL OVERGROWTH FORGETS ITS MUSHROOMS AND BOTH OF ITS FIGURES.
		// Issues #1820 and #41. Four lines and no clock, Withered Ground's shape
		// exactly: the lists because those actors are already destroyed -- see the
		// top of this function; the two figures because the call above has already
		// taken them off the character, so leaving either here would make the next
		// beat believe it was still applied and never put it back.
		FungalOvergrowthBoostMushrooms.Empty();
		FungalOvergrowthSlowMushrooms.Empty();
		FungalOvergrowthSpeedMoreApplied = 0.0f;
		FungalOvergrowthSpeedLessApplied = 0.0f;

		// AND WASTING SICKNESS FORGETS WHAT WAS APPLIED AND KEEPS ITS
		// STACKS. Issues #1786 and #41. This is the only rule here whose
		// count survives the stairs, because its row says the debuff is
		// "permanent for the duration of the dungeon" -- so only the applied
		// figure goes, because the call above has already taken the
		// reduction off the character. The next beat sees the two differ and
		// puts it back. Zeroing the count here would make the stairs a cure
		// the row does not offer.
		WastingSicknessStacksApplied = 0;

		// AND THE STARVATION CURSE THE SAME WAY, for the same reason: its stacks are the
		// dungeon's and the call above took them off the character. Issues #1820 and #41.
		StarvationCurseMovementApplied = 0;
		StarvationCurseHealthApplied = 0;

		// AND A TREAT'S HASTE THE SAME WAY: the call above took it off, and its clock is left
		// running, so the next beat puts it back if the ten seconds are not over.
		TrickOrTreatHasteApplied = 0.0f;

		// AND CHAOS TOUCHED THE SAME WAY: its stacks are the dungeon's and the call above took
		// them off the character. Issues #1820 and #41.
		ChaosTouchedApplied = {0, 0, 0, 0, 0, 0, 0, 0};

		// AND ABYSSAL RIFTS' MAGIC FIND THE SAME WAY: the successes are the dungeon's, and the next beat puts their
		// reward back. Issues #1820 and #41.
		AbyssalRiftSuccessesApplied = 0;

		// AND VOID PARASITE THE SAME WAY: the call above took the voidlings' figure off the character,
		// and a Horde arena's next wave keeps its stacks, so the next beat puts them back. Issues #1820
		// and #41.
		VoidParasiteStacksApplied = 0;

		// AND GRASPING TENTACLES FORGETS ALL FOUR OF ITS THINGS. Issues #1786
		// and #41. The list because those actors are already destroyed -- see the
		// top of this function -- and a stale list would count them against the cap
		// and stop the tentacles entirely; the clock so the first of a floor does
		// not arrive on its first beat carrying the last floor's wait; the grab
		// because a player who took the stairs is not still held by a tentacle they
		// left behind; and the applied figure because the call above has already
		// taken the reduction off the character, so leaving it would make the next
		// beat believe it was still applied.
		//
		// THESE FOUR LINES WERE IN `NoteDeathForWastingSickness` UNTIL NOW, AND
		// THAT WAS SHIPPED. The change that built Grasping Tentacles anchored
		// them on `WastingSicknessStacksApplied = 0;`, which appears twice --
		// once here and once in that death handler -- and attached them to the
		// wrong one. Every test of that rule still passed, because none of them
		// changed floor while a grab was running. What it cost in play: a grab
		// survived the stairs, the cadence carried across floors, and the applied
		// figure stayed set while the floor change had already taken the slow off
		// the character. Found by the Edict of Silence's stairs test, which fails
		// outright when a rule's applied figure is not cleared here.
		GraspingTentacles.Empty();
		GraspingTentaclesSecondsSinceLast = 0.0f;
		GraspedUntilSeconds = -1.0f;
		GraspMovementLessApplied = 0.0f;

		// AND THE EDICT OF SILENCE FORGETS ONLY WHAT WAS APPLIED. Issues #1786
		// and #41. ITS CLOCK AND ITS SILENCE DELIBERATELY SURVIVE THE STAIRS,
		// which makes it the only rule in this function that keeps a clock across
		// a floor. The row says the silence sweeps the DUNGEON, and a player
		// descending every eighty seconds would otherwise never meet one. The
		// applied figure still goes, because the call above has already taken the
		// lock off the character and the next beat has to put it back while the
		// silence is still running.
		EdictOfSilenceLockApplied = 0.0f;

		// AND THE ARTILLERY STRIKE FORGETS THE CIRCLE, THE WARNING AND THE
		// CLOCK. Issues #1820 and #41. The circle because it is already destroyed
		// -- see the top of this function -- and a stale pointer would stop the
		// rule placing another; the warning because a shell must not land on a
		// floor where nobody saw the circle that announced it, which is why a
		// circle still counting down when the floor changes never lands; the clock
		// so the first strike of a floor does not arrive on its first beat carrying
		// the last floor's wait.
		//
		// THIS RULE KEEPS NOTHING ACROSS THE STAIRS, unlike the Edict of Silence
		// directly above. Its row says nothing about the dungeon, only about a
		// repeating thirty seconds, so there is no sentence here asking a clock
		// to survive a floor.
		ArtilleryStrikeCircle = nullptr;
		ArtilleryStrikeWarningSoFar = 0.0f;
		ArtilleryStrikeSecondsSinceLast = 0.0f;

		// AND WINGS OF THE HOST, FOR THE SAME REASON: a flyover marked on the last floor never
		// lands on this one, and this floor's first comes thirty seconds in. The marks themselves
		// go with the floor's other zones. Issues #1820 and #41.
		WingsOfTheHostMarks.Reset();
		WingsOfTheHostWarningSoFar = 0.0f;
		WingsOfTheHostSecondsSinceLast = 0.0f;

		// AND THE INFESTED HOARD'S STACKS END WITH THE FLOOR, as ruled, and with leaving the dungeon, which also
		// comes through here. Issues #1820 and #41.
		InfestedHoardStacks = 0;
		InfestedHoardSecondsSinceDrain = 0.0f;
		InfestedHoardPanelStacks = -1;

		// AND HALLOWED GROUNDFALL FORGETS ITS CRATERS AND ITS CLOCK. Issues
		// #1820 and #41. The list because those actors are already destroyed -- see
		// the top of this function -- and a stale list would have the beat
		// empowering creatures standing where craters used to be; the clock so the
		// first bombardment of a floor does not arrive on its first beat carrying
		// the last floor's wait.
		//
		// NOTHING ELSE TO FORGET. The empowerment is a status effect on a
		// creature with its own one-second life, not a figure this rule holds, and
		// it ends on its own within that second wherever the creature is.
		HallowedGroundfallCratersBurning.Empty();
		HallowedGroundfallSecondsSinceLast = 0.0f;

		// AND LEAVING THE DUNGEON FORGETS THE WALK ITSELF. The brief carries no
		// modifiers once the player has left, and the row's reduction is
		// permanent within a dungeon rather than across a run.
		if (FloorBrief.Modifiers.IsEmpty())
		{
			if (const UCataclysmAbilitySystemComponent* Cataclysm =
					Cast<UCataclysmAbilitySystemComponent>(
						Player->GetAbilitySystemComponent()))
			{
				MetresWalkedAtLastCleanse = Cataclysm->MetresWalkedTotal();
			}
			NihilsEmbraceRewardUntilSeconds = -1.0f;

			// AND WASTING SICKNESS'S STACKS GO WITH IT, which is where "for the
			// duration of the dungeon" ends. Issues #1786 and #41.
			WastingSicknessStacks = 0;

			// AND THE VOID EMPTIES: what Nothing Is Forgotten held was this dungeon's.
			// Issues #1820 and #41.
			NothingIsForgottenHealth = 0.0f;
			NothingIsForgottenDamage = 0.0f;
			NothingIsForgottenBoss.Reset();
			NothingIsForgottenHealthGiven = 0.0f;
			NothingIsForgottenDamageGiven = 0.0f;

			// AND THE STARVATION CURSE'S STACKS: "persist unless cleansed" ends with the
			// dungeon. Issues #1820 and #41.
			StarvationCurseMovementStacks = 0;
			StarvationCurseHealthStacks = 0;

			// AND TRICK OR TREAT'S COUNTS AND ANY HASTE STILL RUNNING. Issues #1820 and #41.
			TrickOrTreatPickups = 0;
			TrickOrTreatRaised = 0;
			TrickOrTreatHasteUntilSeconds = -1.0f;

			// AND SOUL HARVEST'S RECORD: the souls went with the creatures that held them.
			// Issues #1820 and #41.
			SoulHarvestHeld.Reset();
			SoulHarvestGiven = 0;

			// AND CHAOS TOUCHED'S STACKS, BUFFS AND DEBUFFS. Issues #1820 and #41.
			ChaosTouchedStacks = {0, 0, 0, 0, 0, 0, 0, 0};

			// AND LEAVING THE DUNGEON IS WHERE THE EDICT OF SILENCE'S CLOCK
			// FINALLY STOPS. Issues #1786 and #41. This is the branch that runs
			// when the brief carries no modifiers at all, which is the player
			// out of the dungeon -- and "sweeps the dungeon" ends there.
			EdictOfSilenceSecondsSinceLast = 0.0f;
			EdictOfSilencedUntilSeconds = -1.0f;

			// AND MORTAL DECAY'S KILL WINDOW GOES WITH IT. Issues #1786 and
			// #41. Not on a new floor, the way the fields above this branch
			// are: those hold something applied to the character and this
			// holds only a time, so carrying the rest of a few seconds down a
			// staircase is what "temporarily" already means. Leaving the
			// dungeon is different -- the field's lifetime should be the
			// dungeon's, so a window bought in one is not open in the next.
			MortalDecaySlowedUntilSeconds = -1.0f;
		}
	}

	// THE PANEL, WITH WHATEVER THE STATEFUL ROWS ARE COUNTING. This was the only
	// place the panel was ever drawn, which is why a count shown on it would have
	// been frozen at whatever the player had on arriving. It is now one of three.
	RefreshFloorModifierPanel();

	// ONE LINE PER FLOOR THAT CARRIES ANY, so a playtest log says what the
	// player was walking through. The six logs read for issue #41's measurement
	// could not say, because nothing wrote it.
	if (FloorBrief.Modifiers.IsEmpty())
	{
		return;
	}

	TArray<FString> Names;
	for (const FName Key : FloorBrief.Modifiers)
	{
		Names.Add(DungeonGameModeModifierNameAndState(Key));
	}

	const FString OnThePlayer = UCataclysmDungeonModifierEffects::Describe(
		UCataclysmDungeonModifierEffects::PlayerEffectsFor(
			FloorBrief.Modifiers, FloorBrief.FloorNumber));
	const FString Tail = OnThePlayer.IsEmpty()
		? FString()
		: FString::Printf(TEXT(" On the player: %s."), *OnThePlayer);

	UE_LOG(LogCataclysm, Log,
		TEXT("Floor %d carries %d dungeon modifier(s) worth %.0f danger: %s.%s"),
		FloorBrief.FloorNumber, FloorBrief.Modifiers.Num(), FloorBrief.ModifierScore,
		*FString::Join(Names, TEXT(", ")), *Tail);
}

bool ACataclysmDungeonGameMode::ClearEmpireDungeon()
{
	UCataclysmEmpireRun* Run = EmpireRun();
	if (!Run || EmpireDungeonId == INDEX_NONE)
	{
		return false;
	}

	// CLEARED FIRST AND LEFT SECOND. `ClearDungeon` takes it off the clock as
	// well as off the map, and it reads `CurrentDungeonId` to notice it was the
	// one being stood in; leaving first would make it forget.
	const bool bCleared = Run->ClearDungeon(EmpireDungeonId);

	LeaveEmpireDungeon();

	return bCleared;
}

bool ACataclysmDungeonGameMode::GoToFloor(int32 NewFloorNumber, APawn* PawnToMove)
{
	FloorNumber = FMath::Max(1, NewFloorNumber);
	DungeonGameModeFollowFloorAtTheConsole(FloorNumber);

	// ASKED BEFORE THE FLOOR IS REPLACED AND ACTED ON AFTER. `BuildFloor` spawns
	// `CurrentFloor` the first time it runs, so afterwards there is no way to
	// tell a floor being replaced from the first floor of the run.
	const bool bReplacingAFloor = CurrentFloor != nullptr;

	if (!BuildFloor())
	{
		// NOTHING ELSE IS TOUCHED. The floor before is still standing and still
		// has its creatures on it, which is a better place to be left than on a
		// floor that does not exist.
		return false;
	}

	// PESTILENT EMPOWERMENT COUNTS THE BEACONS LEFT STANDING ON THE FLOOR OR WAVE BEING LEFT, here: after
	// the next floor is built, so a floor that fails to build counts nothing, and before the block below
	// clears the last floor's creatures, the beacons among them. Issues #1820 and #41.
	if (bReplacingAFloor)
	{
		CountThePlagueBeaconsLeftStanding();
	}

	// THE LAST FLOOR'S CONTENTS COME OUT OF THE WORLD. Every floor is built at
	// the same world coordinates -- `BuildFloor` reuses one `ACataclysmDungeonFloor`
	// -- so a dropped item, a called Imp or a burning patch left behind is not
	// somewhere the player has walked away from. It is standing inside the new
	// floor. Reported from play as items still visible on later floors and the
	// game slowing badly after four or five of them, which is issue #1176: two
	// things walk every dropped item in the world every frame, and both were
	// written when a floor's worth was the most there could ever be.
	//
	// AFTER `BuildFloor` AND NOT BEFORE, so a floor that fails to build leaves
	// the player standing on the last one with its creatures still on it, which
	// is what the branch above promises.
	//
	// BEFORE `PopulateFloor`, which is what puts the new floor's creatures out.
	// Clearing afterwards would destroy the creatures it had just spawned.
	//
	// NOT ON THE FIRST FLOOR OF A RUN. There is nothing to clear, and a level
	// may hold actors somebody placed by hand for the game mode to find.
	//
	// AND NOT WHEN THE FLOOR IS THE SAME SPACE IT WAS. A Horde dungeon's next
	// wave arrives in the arena the player is standing in, so what is lying
	// there is not left over from a floor that has gone -- it is the loot they
	// have just been dropped and have not picked up yet. Clearing it would
	// delete the reward for the wave they have just fought.
	if (bReplacingAFloor && !FloorBrief.bSameArenaAsLastFloor)
	{
		if (UWorld* World = GetWorld())
		{
			const int32 Removed = UCataclysmFloorContents::ClearTheFloor(*World);
			UE_LOG(LogCataclysm, Verbose,
				   TEXT("Floor %d: %d actors left on the last floor were removed."),
				   FloorNumber, Removed);
		}
	}

	PopulateFloor();

	// NOTHING IS FORGOTTEN: THE FINAL FLOOR'S BOSS TAKES WHAT THE VOID HOLDS as it is placed.
	// Issues #1820 and #41. The boss is the Gatekeeper the populator puts on the exit cell;
	// nothing else marks a floor's boss. Fed whether or not this floor carries the row,
	// because what the void holds was fed on floors that did.
	if (IsTheFinalFloorForItsBoss() && FloorBrief.bBossAtTheExit
		&& (NothingIsForgottenHealth > 0.0f || NothingIsForgottenDamage > 0.0f) && CurrentFloor)
	{
		const FVector Exit = CurrentFloor->ExitWorld();
		ACataclysmEnemyCharacter* Boss = nullptr;
		for (const TObjectPtr<ACataclysmEnemyCharacter>& Enemy : FloorEnemies)
		{
			if (IsValid(Enemy) && Enemy->IsA<ACataclysmGatekeeperCharacter>()
				&& (!Boss || FVector::DistSquared2D(Enemy->GetActorLocation(), Exit)
							   < FVector::DistSquared2D(Boss->GetActorLocation(), Exit)))
			{
				Boss = Enemy.Get();
			}
		}
		FeedTheFinalBoss(Boss);
	}

	// THE STAIRS, UNLESS THE FLOOR IS A WAVE. A Horde dungeon has none at all:
	// the way to the next floor is to beat the wave standing in front of you,
	// and `BringTheNextWaveIn` is what takes it. A flight of stairs in the
	// middle of an arena would be a second way down that skipped the fight.
	if (!FloorBrief.bWaveWalksIn)
	{
		PlaceStairs();
	}
	else
	{
		// **AND ANY STAIRS ALREADY IN THE WORLD ARE TAKEN OUT OF IT, WHICH IS
		// NOT THE SAME AS NOT PLACING ANY.** `EnterEmpireDungeon` reuses this
		// game mode, so a player who walks an ordinary dungeon and then enters a
		// Horde one arrives with the last dungeon's stairs still standing --
		// somewhere in the middle of the new arena, still watching for them, and
		// still bound to `HandleStairsTaken`. Walking over them would skip a
		// wave and spend a day for it. Not placing the stairs leaves that actor
		// exactly where it was; destroying it is what removes it.
		//
		// STOPPED BEFORE IT IS DESTROYED, so a look already in flight cannot
		// arrive during the destruction. `StopWatching`'s own comment says the
		// broadcast rebuilds the floor from inside itself.
		if (IsValid(Stairs))
		{
			Stairs->StopWatching();
			Stairs->Destroy();
		}
		Stairs = nullptr;
	}

	// AND THE PLAYER IS STOOD ON IT, AFTER the floor is built and not before, or
	// they would be placed at the previous floor's entrance.
	//
	// NOTHING TO MOVE DURING `StartPlay`, where the pawn does not exist yet and
	// the caller does this again afterwards.
	//
	// AND NOT AT ALL WHEN THE FLOOR IS THE SAME SPACE IT WAS. A Horde dungeon's
	// next wave arrives around the player; teleporting them back to the mouth of
	// the arena between waves would undo the fight they were in the middle of,
	// and there is no new entrance to put them at because there is no new floor.
	//
	// **`bReplacingAFloor` AS WELL, AND WITHOUT IT A LOADED SAVE LEAVES THE
	// PLAYER NOWHERE.** `bSameArenaAsLastFloor` is true on wave 5 of an arena
	// whether or not there is a wave 4 standing to be the same space as. Loading
	// a save taken on wave 5 builds the arena from nothing, so there is no
	// "where they already were" to leave them at and they have to be stood at
	// the entrance like any other first floor.
	if (!FloorBrief.bSameArenaAsLastFloor || !bReplacingAFloor)
	{
		APawn* Moving = PawnToMove;
		if (!Moving)
		{
			const APlayerController* Controller =
				GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
			Moving = Controller ? Controller->GetPawn() : nullptr;
		}

		PlaceAtEntrance(Moving);
	}

	// AND THE SAVE RECORD FOLLOWS. `UCataclysmSaveWriter::SetFloor` has existed
	// since the save system was built and nothing called it, because nothing
	// changed floors. It notes an `ECataclysmSaveTrigger::ChangedFloor`, so going
	// down is now one of the moments the game saves itself.
	//
	// ONLY ONCE THE RUN HAS BEGUN. During `StartPlay` this runs before the parent
	// starts the writer, and telling a writer with nowhere to write would count a
	// refused trigger for no reason. `BeginRun` records the floor a moment later
	// anyway.
	if (UCataclysmSaveWriter* Writer = UCataclysmSaveWriter::In(GetWorld()))
	{
		if (Writer->IsWriting())
		{
			Writer->SetFloor(DungeonName, FloorNumber);
		}
	}

	// AND THE NEW FLOOR'S MODIFIERS REACH THE PLAYER, and the panel says what
	// they are. LAST, after the brief is decided and the player is stood on the
	// floor, so a rule worded "each floor" follows the floor being stood on and
	// never the one before it. A Horde dungeon's next wave comes through here
	// too, so its rules apply per wave. Issue #41.
	// THE STARVATION CURSE ADDS ITS STACK AS THE FLOOR BEGINS, before the floor's rules
	// reach the player, so floor 1 counts and each floor adds exactly one. Here and not in
	// `ApplyFloorRulesToPlayer`, which `StartPlay` calls a second time for the first floor.
	// Issues #1820 and #41.
	if (FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::StarvationCurseKey)))
	{
		AddAStarvationCurse();
	}

	// AND CHAOS TOUCHED THE SAME WAY, for the same reason. Issues #1820 and #41.
	if (FloorBrief.Modifiers.Contains(
			FName(UCataclysmDungeonModifierEffects::ChaosTouchedKey)))
	{
		AddAChaosTouch();
	}

	// ECHOES OF THE PAST HANDS THE LAST FLOOR'S DEAD TO THIS ONE, once a floor, here rather than
	// in `ApplyFloorRulesToPlayer`, which `StartPlay` calls a second time for the first floor.
	// Issues #1820 and #41.
	EchoesFromLastFloor = MoveTemp(EchoesThisFloor);
	EchoesThisFloor.Reset();

	ApplyFloorRulesToPlayer();

	return true;
}
