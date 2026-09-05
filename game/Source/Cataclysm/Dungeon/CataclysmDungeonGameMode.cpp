// Copyright Stephen Dubois. All Rights Reserved.

#include "Dungeon/CataclysmDungeonGameMode.h"

#include "Cataclysm.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmCorruptedSentinelCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmHellhoundCharacter.h"
#include "Character/CataclysmImpCharacter.h"
#include "Character/CataclysmSuccubusCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Dungeon/CataclysmDungeonFloor.h"
#include "Dungeon/CataclysmDungeonStairs.h"
#include "Dungeon/CataclysmFloorContents.h"
#include "Dungeon/CataclysmFloorGenerator.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Engine/World.h"
#include "Player/CataclysmGameInstance.h"
#include "HAL/IConsoleManager.h"
#include "Save/CataclysmSaveWriter.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

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

	FCataclysmFloorRequest Request;
	Request.DungeonSeed = ChooseSeed();
	Request.FloorNumber = ChooseFloorNumber();
	Request.Layout = ChooseLayout();

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
}

int32 ACataclysmDungeonGameMode::PopulateFloor()
{
	UWorld* World = GetWorld();
	if (!World || !CurrentFloor || !CurrentFloor->IsBuilt())
	{
		return 0;
	}

	// FIRST, because this is called again every time the floor is replaced.
	ClearFloorEnemies();

	const FCataclysmFloorPopulation Population = FCataclysmFloorPopulator::Populate(
		CurrentFloor->GetPlan(), ChooseEnemyScale());

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector Entrance = CurrentFloor->EntranceWorld();

	int32 Spawned = 0;
	for (const FCataclysmEnemyPlacement& Placement : Population.Enemies)
	{
		const TSubclassOf<ACataclysmEnemyCharacter> Class = ClassFor(Placement.Creature);
		if (!Class)
		{
			continue;
		}

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
		FVector Toward = Entrance - Where;
		Toward.Z = 0.0f;
		const FRotator Facing = Toward.IsNearlyZero()
			? FRotator::ZeroRotator : Toward.Rotation();

		ACataclysmEnemyCharacter* Enemy = World->SpawnActor<ACataclysmEnemyCharacter>(
			Class, Where, Facing, SpawnParams);
		if (!Enemy)
		{
			continue;
		}

		ApplyDesignedStats(Enemy, Placement.Creature);

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

		FloorEnemies.Add(Enemy);
		++Spawned;
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("Put %d creatures on the dungeon floor in %d groups: %d Imps, %d "
			 "Hellhounds, %d Brutes, %d Abyssal Wardens, %d Corrupted Sentinels "
			 "and %d Succubi. The floor has %d walkable cells and the density "
			 "asked for %d. No creature stands within %d cells of where the "
			 "player arrives, and no two group middles are within %d cells of "
			 "each other."),
		Spawned, Population.PackCount,
		Population.HowMany(ECataclysmDungeonCreature::Imp),
		Population.HowMany(ECataclysmDungeonCreature::Hellhound),
		Population.HowMany(ECataclysmDungeonCreature::Brute),
		Population.HowMany(ECataclysmDungeonCreature::AbyssalWarden),
		Population.HowMany(ECataclysmDungeonCreature::CorruptedSentinel),
		Population.HowMany(ECataclysmDungeonCreature::Succubus),
		CurrentFloor->GetPlan().FloorCount(), Population.Wanted,
		FCataclysmFloorPopulator::LeastCellsFromEntrance,
		FCataclysmFloorPopulator::LeastCellsBetweenPacks);

	return Spawned;
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

	// FLOOR 1 IS A FLOOR, AND A FLOOR COSTS A DAY. Walking N floors costs N
	// days: one for arriving and one for each descent.
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
	if (bReplacingAFloor)
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
	PlaceStairs();

	// AND THE PLAYER IS STOOD ON IT, AFTER the floor is built and not before, or
	// they would be placed at the previous floor's entrance.
	//
	// NOTHING TO MOVE DURING `StartPlay`, where the pawn does not exist yet and
	// the caller does this again afterwards.
	APawn* Moving = PawnToMove;
	if (!Moving)
	{
		const APlayerController* Controller =
			GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		Moving = Controller ? Controller->GetPawn() : nullptr;
	}

	PlaceAtEntrance(Moving);

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

	return true;
}
