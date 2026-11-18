// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/CataclysmGameMode.h"
#include "CataclysmCapitalGameMode.generated.h"

/**
 * The game mode of the capital's level, `L_Capital`: the empire map's home. Issue #2733 slice 2, ruled by the project
 * owner on 2026-11-17: "The empire map's home is a plain capital level, `L_Capital`. It is a new, nearly empty level
 * that opens the empire map as soon as the player arrives. Character select's Start and Continue go there."
 *
 * AN `ACataclysmGameMode`, as `ACataclysmDungeonGameMode` is, for what that one does on arrival: `StartPlay` begins
 * saving a run (`BeginSavingThisRun`) and loads the character the select screen chose (`LoadThePendingCharacter`) --
 * made, loaded, or refused and sent back to `L_FrontEnd` -- exactly as `L_Sandbox` does. What it turns off is the
 * sandbox's creatures; what it adds is the empire map, opened once the pending load has settled.
 *
 * NO FLOOR. The run record says nobody is on a floor here (`RunFloorNumber` 0), so a character saved in the capital
 * loads again with no floor to match. A record saved on the sandbox's floor is accepted here too, without that floor's
 * creatures: the coordinating session's ruling of 2026-11-17, Q1 (`FCataclysmSaveLoad::CharacterAndRun`).
 */
UCLASS()
class CATACLYSM_API ACataclysmCapitalGameMode : public ACataclysmGameMode
{
	GENERATED_BODY()

public:
	ACataclysmCapitalGameMode();

	virtual void StartPlay() override;

	/** And, once the player has started, the map, if the load settled here rather than in `StartPlay`. */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	/**
	 * Opens the empire map on the player's screen once the pending load has settled, and answers whether it did now.
	 *
	 * SETTLED MEANS: nothing is pending on the game instance (no new character to make, no chosen character to load),
	 * the load did not send the player back to the front end (`FrontEndRequested`), the save writer is writing, and a
	 * player's character stands. `StartPlay` and `HandleStartingNewPlayer` both ask, after the load they each try, so
	 * whichever comes second opens it; the order between them does not matter, as for the load. ONCE: a second call
	 * answers false and does nothing.
	 *
	 * A RUN IS BEGUN WHEN THERE IS NONE (`UCataclysmGameInstance::EmpireRunFor` asked to start one), so Play pressed on
	 * `L_Capital` with nothing pending shows a run standing its opening wave rather than an empty map: the coordinating
	 * session's ruling of 2026-11-17, Q2. A made or loaded character's run is already there.
	 *
	 * THE SCREEN IS NOT SHOWN IN AN AUTOMATION TEST, which has no viewport to show it on, as a level is not opened in
	 * one (`FrontEndRequested`): the decision is kept (`HasOpenedTheEmpireMap`) and the screen is
	 * `ACataclysmPlayerController::OpenEmpireMap`'s, seen in play.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, as `LoadThePendingCharacter` is.
	 */
	bool OpenTheEmpireMapIfReady();

	/** Whether `OpenTheEmpireMapIfReady` has opened the map in this level. */
	bool HasOpenedTheEmpireMap() const { return bOpenedTheEmpireMap; }

	/**
	 * The capital's Walk: take the character to `L_Dungeon` to walk an empire dungeon standing on the map, which that
	 * level enters once the character is loaded there (`ACataclysmDungeonGameMode::EnterThePendingWalkIfReady`). Issue
	 * #2733 slice 3, ruled by the coordinating session on 2026-11-17.
	 *
	 * `FCataclysmWalkFromTheScreen::Walk` calls it from the city screen's and the empire map's Walk buttons, after its
	 * own refusals: no run, and no such dungeon standing.
	 *
	 * @return the refusal to show, or empty when the trip began: `TravelTo`, with the dungeon to walk.
	 */
	FString TravelToWalk(int32 DungeonId);

	/** The capital's game mode in a world, or null: the level's own, found as `ACataclysmDungeonGameMode::InWorld` finds its. */
	static ACataclysmCapitalGameMode* InWorld(UWorld* World);

	/** The name the run record gives the capital, with floor 0: nobody is on a floor here. */
	static constexpr const TCHAR* CapitalFloorName = TEXT("Capital");

	/** No floor: see the class comment. */
	virtual FName RunFloorName() const override { return FName(CapitalFloorName); }
	virtual int32 RunFloorNumber() const override { return 0; }

private:
	bool bOpenedTheEmpireMap = false;
};
