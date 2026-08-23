// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "CataclysmPlayerCharacter.generated.h"

class UCameraComponent;
class UCataclysmEquipmentComponent;
class UCataclysmInventoryComponent;
class UCataclysmWeaponSlotsComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FOnAttributeChangeData;

/**
 * The player pawn. Its ability system component lives on the player state, so
 * that it survives the death and respawn the design treats as routine.
 */
UCLASS()
class CATACLYSM_API ACataclysmPlayerCharacter : public ACataclysmCharacterBase
{
	GENERATED_BODY()

public:
	ACataclysmPlayerCharacter();

	/**
	 * How fast the player walks before any class has been chosen, in centimetres
	 * per second.
	 *
	 * WHAT IT REPLACED. Nothing set a walk speed at all until this constant
	 * existed, so the player ran at Unreal's engine default of 600 -- see
	 * `MaxWalkSpeed` in
	 * Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp.
	 * The design gives the three Demonic classes 4.6, 3.5 and 4.0 metres per
	 * second and not one of them reached the game. Issue #391.
	 *
	 * WHY THIS FIGURE AND NOT A NEW ONE. It is the shared `Default` line in
	 * game/Data/ClassStats.csv, `movement_speed` 4.0, which is also what
	 * UCataclysmCombatAttributeSet starts the MovementSpeed attribute at and what
	 * the Masochist walks at. There is no class selection yet, so a character
	 * that has chosen nothing walks at the line every class inherits rather than
	 * at an engine constant.
	 *
	 * ONLY THE STARTING POINT. The pawn follows the MovementSpeed attribute from
	 * the moment there is an ability system to read it from, so gear, passives
	 * and effects move it. See InitAbilityActorInfo.
	 */
	static constexpr float DefaultWalkSpeedCmPerSecond = 400.0f;

	/**
	 * Centimetres in a metre.
	 *
	 * The design and the simulation state movement speed in metres per second;
	 * UCharacterMovementComponent walks in centimetres per second. The factor is
	 * applied in ApplyMovementSpeed and nowhere else.
	 */
	static constexpr float CentimetresPerMetre = 100.0f;

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/**
	 * Writes a movement speed, stated in metres per second, onto the movement
	 * component.
	 *
	 * A NON-POSITIVE SPEED IS REFUSED RATHER THAN WRITTEN. An ability system that
	 * holds no combat attribute set reports zero rather than failing, and a
	 * MaxWalkSpeed of zero is a character who cannot move with nothing on screen
	 * to say why. Refusing leaves whatever speed was last valid, which for a pawn
	 * whose attributes have not arrived yet is DefaultWalkSpeedCmPerSecond.
	 *
	 * NOTHING IN THE PROJECT ROOTS THE PLAYER, so nothing is being blocked by
	 * this. A designed root is a status effect and would stop movement through
	 * the movement mode rather than by setting a speed of zero.
	 *
	 * Public so a test can drive it without building an ability system. The game
	 * reaches it through the MovementSpeed attribute.
	 */
	void ApplyMovementSpeed(float MetresPerSecond);

	/** Server: the pawn has been possessed and the player state is available. */
	virtual void PossessedBy(AController* NewController) override;

	/**
	 * Puts the class stat line named by `Cataclysm.PlayerClass`, resolved at the
	 * level named by `Cataclysm.PlayerLevel`, onto this character.
	 *
	 * CALLED FROM PossessedBy AND NOWHERE ELSE. Not from InitAbilityActorInfo,
	 * which is documented as safe to run twice and does: a whole stat line
	 * written from there would overwrite anything else that had set an attribute
	 * and would refill the character's health each time.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT, which is the same reason the projectile
	 * exposes `Step` and the strike template exposes `SwingOnce`. A test world
	 * built with `UWorld::CreateWorld` has no controller to possess with, so
	 * possession itself cannot be reached; this is the one step that matters.
	 */
	void ApplyChosenClassStats();

	/** Client: the player state has replicated. There is no PossessedBy here. */
	virtual void OnRep_PlayerState() override;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Moves the camera nearer or further by whole wheel notches.
	 *
	 * Positive zooms in, which is what a wheel pushed forward reports and what
	 * every game in the genre does with it. The result is clamped, so a player
	 * holding the wheel down cannot end up inside the character or looking at the
	 * whole level.
	 */
	void AddCameraZoom(float Notches);

	/**
	 * The distance the camera is moving toward, in centimetres.
	 *
	 * This is not the same as the boom's current length: the camera eases toward
	 * this over a few frames rather than jumping. Reading the boom mid-glide
	 * gives an intermediate value, which is why the target is exposed separately.
	 */
	float GetTargetCameraDistance() const { return TargetCameraDistance; }

	/**
	 * How long the player lies dead before coming back, in seconds.
	 *
	 * A JUDGEMENT, AND LABELLED AS ONE. No design document describes the moment
	 * of death at all -- there is no death screen, no prompt, no input lockout
	 * and no stated timing anywhere in `docs/`. Three seconds is long enough
	 * that the death is visible as an event rather than a flicker, and short
	 * enough that testing combat by dying repeatedly is not tedious. It is
	 * expected to change once the death moment is designed.
	 */
	static constexpr float RespawnDelaySeconds = 3.0f;

	/**
	 * Stop, and come back after `RespawnDelaySeconds`.
	 *
	 * WHAT THIS DOES AND DOES NOT DO. It marks the player dead, halts them, and
	 * schedules `Revive`. It does NOT charge the death penalty, because the
	 * penalty is measured in days off the empire clock and the running game has
	 * no day clock to charge. See the note on `Revive`.
	 */
	virtual void HandleDeath() override;

	/**
	 * Undo the death: clear the mark, refill, and stand up at the player start.
	 *
	 * Public so a test can run it without waiting out a timer, and so the moment
	 * of coming back is one function rather than a lambda inside the timer.
	 */
	void Revive();

	/** Whether the player is currently dead and waiting to come back. */
	bool IsAwaitingRespawn() const;

	/**
	 * What the character carries, and what it is wearing.
	 *
	 * ACCESSORS RATHER THAN MAKING THE COMPONENTS PUBLIC, because the
	 * pointers are protected so that only this class decides when they are
	 * replaced. Reading them is safe; reassigning them is not.
	 */
	UCataclysmInventoryComponent* GetInventory() const { return Inventory; }

	/** What the character is wearing. Issue #828. */
	UCataclysmEquipmentComponent* GetEquipment() const { return Equipment; }

	/** The ItemBases row the character begins wearing. Issue #840. */
	FName GetStartingWeaponBase() const { return StartingWeaponBase; }

protected:
	virtual void InitAbilityActorInfo() override;

	/**
	 * How near and how far the camera may get, in centimetres.
	 *
	 * The range is a judgement, not something the genre settles. Path of Exile 2,
	 * Last Epoch and Diablo 4 all put zoom on the wheel between a fixed minimum
	 * and maximum, and all three keep the range deliberately narrow, but their
	 * numbers are in their own units and their own art scale and do not transfer.
	 * These were chosen by looking at the game and are expected to change.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Camera", meta = (ClampMin = "1.0"))
	float MinCameraDistance = 500.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Camera", meta = (ClampMin = "1.0"))
	float MaxCameraDistance = 1200.0f;

	/** How far one wheel notch moves the camera, in centimetres. Seven notches
	 *  cover the whole range, which is a short flick of the wheel. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Camera", meta = (ClampMin = "1.0"))
	float CameraZoomStep = 100.0f;

	/**
	 * How quickly the camera reaches a new distance. Larger is faster.
	 *
	 * Eased rather than snapped, which is a judgement. One notch is an eighth of
	 * the whole range, and moving that far between two frames reads as the view
	 * cutting rather than the camera moving. At 10 the camera covers most of a
	 * notch in about a fifth of a second.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Camera", meta = (ClampMin = "0.1"))
	float CameraZoomInterpSpeed = 10.0f;

	/** Holds the camera above and behind. Uses absolute rotation, so it does not
	 *  spin when the character turns. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Camera")
	TObjectPtr<UCameraComponent> TopDownCamera;

	/**
	 * A stand-in body, so there is something to see before there is any art.
	 *
	 * Two engine primitives: a cylinder for the body and a cone for the nose.
	 * The nose is not decoration. The character turns to face the direction it
	 * is moving, and with a bare cylinder that rotation is invisible, so there
	 * is no way to tell whether facing is working.
	 *
	 * Both come from /Engine/BasicShapes, so this adds no asset to the project
	 * and nothing to Git LFS. Replaced when there is a real mesh.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderFacingMarker;

	/**
	 * Fills the six ability slots from the equipped weapon.
	 *
	 * On the pawn rather than the player state, unlike the ability system
	 * component: what is held is a property of the body, and a respawned
	 * character equips again rather than inheriting what the corpse held.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Weapon")
	TObjectPtr<UCataclysmWeaponSlotsComponent> WeaponSlots;

	/**
	 * The 48 carried slots. Issue #714.
	 *
	 * On the pawn for the same reason the weapon slots are: what is
	 * carried is a property of the body. What happens to it on death is
	 * not decided here and is not decided anywhere yet -- the design's
	 * lethality modes say what happens to the CHARACTER, and issue #529
	 * is what would have to record either answer.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Items")
	TObjectPtr<UCataclysmInventoryComponent> Inventory;

	/**
	 * What the character is wearing. Issue #828.
	 *
	 * On the pawn for the same reason the carried inventory and the weapon
	 * slots are: what is worn is a property of the body.
	 *
	 * WHAT IT IS FOR. Until it existed, a character's stats came from the
	 * class line alone, so every character at a given level was identical and
	 * nothing the player found changed anything. ApplyChosenClassStats now
	 * asks it for the modifiers the worn items grant, and OnEquipmentChanged
	 * recomputes when they change.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Items")
	TObjectPtr<UCataclysmEquipmentComponent> Equipment;

	/**
	 * The weapon the character begins wearing. A row name in ItemBases.
	 *
	 * A REAL ITEM, WORN IN A REAL SLOT, and that is the point of it. Issue #840
	 * was reported as equipping a whip making the character much weaker than
	 * holding nothing. It was not holding nothing: the weapon slot was empty and
	 * UCataclysmWeaponSlotsComponent::StartingWeaponType made it swing a
	 * Greataxe anyway, worth 72 attack damage doubled for being two-handed
	 * against a whip's 32. Nothing on screen said so, because there was no item
	 * to show. Giving the character an actual Everyday Greataxe means the gear
	 * panel draws it, Cataclysm.ShowEquipment lists it, and putting a whip on is
	 * a swap the player can see rather than a silent downgrade.
	 *
	 * IT MUST NAME THE SAME WEAPON TYPE AS UCataclysmWeaponSlotsComponent'S
	 * StartingWeaponType, which is still what the fallback in OnEquipmentChanged
	 * uses. The two are checked against each other by
	 * Cataclysm.WeaponSlots.TheStartingWeaponItemMatchesTheStartingWeaponType.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Items")
	FName StartingWeaponBase = TEXT("Weapon_Greataxe");

private:
	/**
	 * Wears StartingWeaponBase, if the character is wearing no weapon at all.
	 *
	 * ASKS WHETHER A WEAPON IS WORN RATHER THAN KEEPING A FLAG, so that running
	 * twice cannot produce two axes. Possession happens more than once on a
	 * listen server, and everything else in InitAbilityActorInfo is written to
	 * survive that.
	 */
	void GiveStartingWeapon();

	/**
	 * Fills the six ability slots from the weapon that is worn.
	 *
	 * SEPARATE FROM OnEquipmentChanged BECAUSE THAT ONE ALSO WRITES ATTRIBUTES,
	 * and writing them is only correct when something actually changed.
	 * OnEquipmentChanged calls UCataclysmEquipmentComponent::RefreshAttributes,
	 * which applies the whole class stat line. InitAbilityActorInfo runs more
	 * than once -- the server reaches it from PossessedBy and a client from
	 * OnRep_PlayerState -- so applying a stat line from there overwrites any
	 * attribute something else has already set. Three tests exist because that
	 * happened once already: Cataclysm.Player.MovementSpeedFollowsTheAttribute,
	 * Cataclysm.PlayerStats.APlayerCharacterLeavesThePlaceholderBehind and
	 * Cataclysm.Death.APlayerStandsBackUpRatherThanBeingRemoved.
	 *
	 * So possession fills the ability slots and nothing else.
	 */
	void FillAbilitySlotsFromWornWeapon();

	/** Passes the attribute's new value, in metres per second, to
	 *  ApplyMovementSpeed. Bound in InitAbilityActorInfo. */
	void OnMovementSpeedChanged(const FOnAttributeChangeData& Data);

	/** So that a second InitAbilityActorInfo replaces the binding rather than
	 *  adding a second one. That function runs from both PossessedBy and
	 *  OnRep_PlayerState, and on a listen server both happen. */
	FDelegateHandle MovementSpeedChangedHandle;

	/**
	 * Recomputes the stat line and refills the ability slots after a change
	 * to what is worn.
	 *
	 * BOTH, AND NOT JUST THE STATS. A weapon is the one piece of gear that
	 * decides which abilities the character has, so equipping one has to reach
	 * UCataclysmWeaponSlotsComponent as well. Everything else only moves
	 * numbers.
	 */
	void OnEquipmentChanged();

	/** What the starting ability set granted, so it can be removed on unequip. */
	FCataclysmAbilitySetHandles GrantedHandles;

	/** Where the camera is heading. Set from the boom's own length at BeginPlay,
	 *  so the resting distance is stated once, in the constructor. */
	float TargetCameraDistance = 0.0f;

	/** Counts down `RespawnDelaySeconds` from the moment of death. */
	FTimerHandle RespawnTimer;

	/** Where to stand up again, chosen at death rather than at revival so a
	 *  level with no player start still puts the character back where it fell
	 *  instead of at the world origin. */
	FVector RespawnLocation = FVector::ZeroVector;
	FRotator RespawnRotation = FRotator::ZeroRotator;

	// --- The automatic basic attack. Issues #36 and #647 -------------------
	//
	// THE DESIGN SAYS IT FIRES BY ITSELF: "The basic attack is on no key. It
	// fires automatically... Nothing the player presses triggers it", and "The
	// Basic Attack is automatic, so the weapon's attack speed sets its rate."
	//
	// EVERY JUDGEMENT LIVES IN UCataclysmBasicAttack so it can be tested. What
	// is here is only the clock, and the clock re-arms itself after each attempt
	// rather than looping, because the interval is the equipped weapon's attack
	// speed and that changes when the weapon does.

	/** Makes one attempt to swing, then re-arms itself. */
	void BasicAttackTick();

	/** Re-arms BasicAttackTimer for the interval the weapon currently sets. */
	void ScheduleNextBasicAttack(float SecondsBetweenSwings);

	/** Counts down to the next attempt to swing. Never loops; see above. */
	FTimerHandle BasicAttackTimer;

	/**
	 * How long to wait before looking again when the character has no rate at
	 * all -- holding nothing, or holding something that states no attack speed.
	 *
	 * A RE-CHECK RATHER THAN STOPPING, because equipping a weapon has to start
	 * the basic attack without anything else having to remember to start it. A
	 * clock that stopped would mean the first weapon equipped after spawning
	 * never swung.
	 */
	static constexpr float NoWeaponRecheckSeconds = 0.5f;
};
