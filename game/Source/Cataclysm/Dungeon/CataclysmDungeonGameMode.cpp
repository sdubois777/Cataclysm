// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonGameMode.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Dungeon/CataclysmDungeonModifierTable.h"
#include "Dungeon/CataclysmFloorHazardSource.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerController.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
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
	ACataclysmEnemyCharacter* Enemy, ECataclysmDungeonCreature Creature) const
{
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
		Enemy->SetRarityStep(RarityStepFor(ImpRarityStep, Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::Hellhound:
		Enemy->SetHealth(HellhoundHealth);
		Enemy->SetArmour(HellhoundArmour);
		Enemy->SetAttackDamage(HellhoundAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(HellhoundRarityStep, Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::Brute:
		Enemy->SetHealth(BruteHealth);
		Enemy->SetArmour(BruteArmour);
		Enemy->SetAttackDamage(BruteAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(BruteRarityStep, Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::AbyssalWarden:
		Enemy->SetHealth(AbyssalWardenHealth);
		Enemy->SetArmour(AbyssalWardenArmour);
		Enemy->SetAttackDamage(AbyssalWardenAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(AbyssalWardenRarityStep, Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::CorruptedSentinel:
		Enemy->SetHealth(CorruptedSentinelHealth);
		Enemy->SetArmour(CorruptedSentinelArmour);
		Enemy->SetAttackDamage(CorruptedSentinelAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(CorruptedSentinelRarityStep, Enemy));
		Enemy->DrawModifiersForRarity();
		break;

	case ECataclysmDungeonCreature::Succubus:
		Enemy->SetHealth(SuccubusHealth);
		Enemy->SetArmour(SuccubusArmour);
		Enemy->SetAttackDamage(SuccubusAttackDamage);
		Enemy->SetRarityStep(RarityStepFor(SuccubusRarityStep, Enemy));
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
		Enemy->SetRarityStep(RarityStepFor(GatekeeperRarityStep, Enemy));
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
	if (!FloorBrief.bSameArenaAsLastFloor)
	{
		ClearFloorEnemies();
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
	const FCataclysmEnemyPlacement& Placement, float SightRadiusMultiplier)
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

	ApplyDesignedStats(Enemy, Placement.Creature);

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
	GoDownOneFloor();
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
	Source->DamageType = FName(*Row->CataclysmType);

	ACataclysmGroundZone* Patch = ACataclysmGroundZone::Spawn(
		Source, Where, UCataclysmDungeonModifierEffects::InfernalRainRadiusCm,
		UCataclysmDungeonModifierEffects::InfernalRainPatchSeconds, PerSecond);
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
	Source->DamageType = FName(*Row->CataclysmType);

	// IT LASTS THE FLOOR, WHICH THE ROW NEITHER STATES NOR CONTRADICTS.
	// `SpawnForTheFloor` exists for the hazard rows of issue #1605 that state no
	// duration, and "pulsing void orbs" reads as a feature of the floor rather
	// than a passing strike. The cap of three is what keeps that from becoming a
	// floor that is slow everywhere.
	//
	// START AND END THE SAME POINT MAKES IT ROUND, which is how `Spawn` builds a
	// circle too: a segment of no length is a circle at that point.
	ACataclysmGroundZone* Well = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Where, Where, Effects::SingularityWellsRadiusCm, PerSecond);
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
	if (!bForcedMarch && !bNihilsEmbrace && !bDeathsEmbrace && !bInfernalRain
		&& !bSingularityWells && !bWitheredGround && !bMortalDecay
		&& !bWastingSickness && !bGraspingTentacles && !bEdictOfSilence
		&& !bArtilleryStrike && !bHallowedGroundfall && !bFungalOvergrowth
		&& !bHolyRepercussions)
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

	// AND MORTAL DECAY, WHICH ASKS FOR NO STAT REFRESH AT ALL. Issues #1786 and
	// #41. It takes health directly, so its position in this order is free the
	// way Withered Ground's is: it neither reads nor writes any field the rules
	// above share, and nothing it does can be undone by the applier they call.
	if (bMortalDecay)
	{
		StepMortalDecay(Player, AbilitySystem);
	}

	// AND WASTING SICKNESS LAST, WHICH IS FREE WHERE NOTHING HAS CHANGED. Issues
	// #1786 and #41. It compares two integers and returns on almost every beat.
	if (bWastingSickness)
	{
		StepWastingSickness(Player, AbilitySystem);
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
		ACataclysmGroundZone* Crater = ACataclysmGroundZone::Spawn(
			Source, Where, Effects::HallowedGroundfallCraterRadiusCm,
			Effects::HallowedGroundfallCraterSeconds, PerSecond);
		if (Crater)
		{
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
	Source->DamageType = FName(*Row->CataclysmType);

	// NO DAMAGE PER TICK. A tentacle grabs and does not burn: the row says
	// "restricting their movement" and says nothing about harm. Since issue #1701
	// a zone with no damage still sweeps, and this one does not even need that --
	// the grab is decided on the beat above rather than by the zone finding
	// anybody.
	//
	// IT LASTS THE FLOOR, which "appear all over the dungeon" reads as: a feature
	// of the place rather than something passing through it. The cap is what
	// keeps that from becoming a floor the player cannot cross.
	ACataclysmGroundZone* Tentacle = ACataclysmGroundZone::SpawnForTheFloor(
		Source, Where, Where, Effects::GraspingTentaclesReachCm, 0.0f);
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
	Effects.HealingReceivedLessPercent =
		UCataclysmDungeonModifierEffects::DeathsEmbraceHealingLessPercent(
			DeathsEmbraceStacksApplied);

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
	NoteDeathForMortalDecay(Notice);
	NoteDeathForWastingSickness(Notice);
	NoteDeathForSporeClouds(Notice);
	NoteDeathForHellfire(Notice);
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
	// ANY BOSS ON THE FLOOR RATHER THAN THE ONE AT THE EXIT, a judgement recorded
	// in `docs/DECISIONS.md`. Nothing marks the creature placed at a floor's exit
	// as that floor's boss, and the row's article is indefinite.
	const ACataclysmEnemyCharacter* Died =
		Cast<ACataclysmEnemyCharacter>(Notice.Victim);
	if (!Died || !Died->IsBoss())
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

	// "N of M" FOR BOTH ROWS, WHICH DIFFERS FROM THE PLAIN COUNT ASKED FOR ON THE
	// SECOND ONE, and the reason is that the second one has an M.
	// `WastingSicknessMostStacks` is 5 and that row says its debuff stacks, so a
	// player reading "2" cannot tell whether that is nearly all of it or a fifth
	// of it. Both rows read alike and neither number is bare.
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

	const FName Wasting(Effects::WastingSicknessKey);
	if (FloorBrief.Modifiers.Contains(Wasting))
	{
		Counting.Add(Wasting,
					 FString::Printf(TEXT("%d of %d"), WastingSicknessStacks,
									 Effects::WastingSicknessMostStacks));
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

	// WHOSE BLOW, AND ON WHAT. The row says "upon being hit", and the thing being
	// hit is the creature, so the player must be the attacker.
	//
	// THIS GUARD ALSO STOPS A BURST PROVOKING A BURST, and that is not a
	// secondary benefit. The burst below is dealt by a creature to the player and
	// is announced like any other blow; without this test it would arrive here,
	// the player would not be its attacker, and it would be refused -- but a
	// reader removing the guard for the first reason would silently create the
	// second. `Demonic_Brand_of_the_Aggressor`'s guard proof found exactly that
	// shape by tripping a test nobody predicted.
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

	UCataclysmAilments::Apply(Source, Player, *Poison, /*Magnitude=*/1.0f);
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
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;

	// NO PLAYER DURING THE FIRST `GoToFloor` OF `StartPlay`, whose pawn is made
	// later by the parent's login. `StartPlay` calls this again once it exists.
	if (ACataclysmPlayerCharacter* Player =
			Controller ? Cast<ACataclysmPlayerCharacter>(Controller->GetPawn()) : nullptr)
	{
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
		// the last floor's wait; the list because those actors are already gone --
		// `UCataclysmFloorContents::ClearTheFloor` destroys every patch with the
		// rest of the floor -- and a stale list would count expired patches
		// against the cap and stop the rain entirely.
		InfernalRainSecondsSinceLastPatch = 0.0f;
		InfernalRainPatches.Empty();

		// AND SINGULARITY WELLS FORGETS ITS CLOCK, ITS WELLS AND ITS SLOW. Issues
		// #1605 and #41. The clock so the first well of a floor does not arrive on
		// its first beat carrying the last floor's wait; the list because
		// `UCataclysmFloorContents::ClearTheFloor` has already destroyed those
		// actors and a stale list would count them against the cap and stop the
		// wells entirely; the slow because the call above has already taken it off
		// the character, so leaving the figure here would make the next beat
		// believe it was still applied and never put it back.
		SingularityWellsSecondsSinceLastWell = 0.0f;
		SingularityWells.Empty();
		SingularityWellsSlowApplied = 0.0f;

		// AND WITHERED GROUND FORGETS ITS PATCHES AND ITS REDUCTION. Issue #41.
		// TWO LINES AND NOT THREE, because this rule holds no clock: its patches
		// are placed by deaths rather than by a cadence, so there is no wait to
		// carry across a floor. The list because
		// `UCataclysmFloorContents::ClearTheFloor` has already destroyed those
		// actors; the reduction because the call above has already taken it off
		// the character, so leaving the figure here would make the next beat
		// believe it was still applied and never put it back.
		WitheredGroundPatches.Empty();
		WitheredGroundRecoveryLessApplied = 0.0f;

		// AND FUNGAL OVERGROWTH FORGETS ITS MUSHROOMS AND BOTH OF ITS FIGURES.
		// Issues #1820 and #41. Four lines and no clock, which is the rule above
		// this one exactly: the lists because
		// `UCataclysmFloorContents::ClearTheFloor` has already destroyed those
		// actors; the two figures because the call above has already taken them
		// off the character, so leaving either here would make the next beat
		// believe it was still applied and never put it back.
		// AND JUDGMENT GOES ENTIRELY, BOTH NUMBERS. Issues #1820 and #41. This
		// is the opposite of Wasting Sickness two paragraphs below, which keeps
		// its count because its row calls the debuff "permanent for the duration
		// of the dungeon". This row says nothing of the kind, and its stacks come
		// from creatures the player has left behind on the last floor.
		JudgmentStacks = 0;
		JudgmentStacksApplied = 0;

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

		// AND GRASPING TENTACLES FORGETS ALL FOUR OF ITS THINGS. Issues #1786
		// and #41. The list because `UCataclysmFloorContents::ClearTheFloor` has
		// already destroyed those actors and a stale list would count them
		// against the cap and stop the tentacles entirely; the clock so the first
		// of a floor does not arrive on its first beat carrying the last floor's
		// wait; the grab because a player who took the stairs is not still held
		// by a tentacle they left behind; and the applied figure because the call
		// above has already taken the reduction off the character, so leaving it
		// would make the next beat believe it was still applied.
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
		// CLOCK. Issues #1820 and #41. The circle because
		// `UCataclysmFloorContents::ClearTheFloor` has already destroyed it and
		// a stale pointer would stop the rule placing another; the warning
		// because a shell must not land on a floor where nobody saw the circle
		// that announced it; the clock so the first strike of a floor does not
		// arrive on its first beat carrying the last floor's wait.
		//
		// THIS RULE KEEPS NOTHING ACROSS THE STAIRS, unlike the Edict of Silence
		// directly above. Its row says nothing about the dungeon, only about a
		// repeating thirty seconds, so there is no sentence here asking a clock
		// to survive a floor.
		ArtilleryStrikeCircle = nullptr;
		ArtilleryStrikeWarningSoFar = 0.0f;
		ArtilleryStrikeSecondsSinceLast = 0.0f;

		// AND HALLOWED GROUNDFALL FORGETS ITS CRATERS AND ITS CLOCK. Issues
		// #1820 and #41. The list because
		// `UCataclysmFloorContents::ClearTheFloor` has already destroyed those
		// actors and a stale list would have the beat empowering creatures
		// standing where craters used to be; the clock so the first bombardment
		// of a floor does not arrive on its first beat carrying the last floor's
		// wait.
		//
		// NOTHING ELSE TO FORGET. The empowerment is a status effect on a
		// creature with its own one-second life, not a figure this rule holds,
		// and the creatures it was on were destroyed with the floor.
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
	ApplyFloorRulesToPlayer();

	return true;
}
