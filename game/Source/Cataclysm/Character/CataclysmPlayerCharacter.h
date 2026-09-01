// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "CataclysmPlayerCharacter.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UCameraComponent;
class UCataclysmEquipmentComponent;
class UCataclysmInventoryComponent;
class UCataclysmWeaponSlotsComponent;
class USpringArmComponent;
struct FOnAttributeChangeData;

/**
 * The player pawn. Its ability system component lives on the player state, so
 * that it survives the death and respawn the design treats as routine.
 */

/**
 * Whether a character is expected to be wearing a weapon at this moment.
 *
 * BECAUSE ONE MOMENT IN A CHARACTER'S LIFE IS NOT LIKE THE OTHERS, and until
 * issue #933 the difference was reported as a fault. `PossessedBy` runs BEFORE
 * `BeginPlay` for the pawn a game mode spawns at level start -- measured on
 * 2026-08-25, not assumed -- and `GiveStartingWeapon` runs from `BeginPlay`. So
 * at possession no weapon is worn yet and that is the design working.
 *
 * A WEAPON IS EXPECTED EVERYWHERE ELSE, and there the complaint is the whole
 * point. Issue #840 was a character wearing nothing and swinging a Greataxe
 * anyway, which nothing on screen said and which made equipping a whip look
 * like the character getting weaker for no reason.
 */
UENUM()
enum class ECataclysmWeaponExpected : uint8
{
	/** Something is meant to be worn. Say so in the log when nothing is. */
	Yes,

	/** Possession, which happens before the starting weapon is put on. */
	NotYet,
};

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

	/**
	 * Works the speed out again and writes it, asking for any bonus that depends
	 * on the character's state rather than reading the attribute.
	 *
	 * WHY THE ATTRIBUTE IS NOT ENOUGH SINCE ISSUE #959. The Masochist's Desperate
	 * Measures node gives movement speed only "while at or below 50% health".
	 * Such a bonus is deliberately never written onto the attribute -- it would
	 * be stale the moment health moved -- so a speed read straight off the
	 * attribute is the speed of a character with no condition on it.
	 *
	 * CALLED FROM TWO PLACES AND THEY ARE TWO DIFFERENT EVENTS. The attribute
	 * changing is one: gear, a level, an attribute point. Health crossing the
	 * threshold is the other, and nothing writes the attribute when that happens.
	 * Both go through here, so the two cannot produce different speeds.
	 */
	void RefreshMovementSpeed();

	/**
	 * The character's health moved, so a bonus that depends on it may have come
	 * on or gone off. Issue #959.
	 */
	virtual void HealthChanged() override;

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

	/**
	 * Makes the character match what was chosen in the character creator.
	 *
	 * THREE THINGS, AND ALL THREE HAVE TO MOVE TOGETHER. The chosen weapon type
	 * decides which item is worn, the chosen damage type decides which six
	 * skills that weapon grants, and the ability slots then have to be filled
	 * again from the pair. Moving one without the others is exactly issue #840
	 * one field along: a character holding a whip and swinging a Greataxe.
	 *
	 * IT DOES NOTHING AT ALL WHEN NOBODY HAS CHOSEN, and that is what makes it
	 * safe to call from possession. `ACataclysmPlayerState::HasChosenAtCreation`
	 * is false for every character stood up by an automation test and for every
	 * character that existed before the creator did, so all of them keep exactly
	 * the Greataxe and the Demonic damage type they had.
	 *
	 * THE OLD WEAPON GOES INTO THE BAG rather than being destroyed, which is the
	 * rule `UCataclysmWearing` exists to keep.
	 *
	 * PUBLIC SO THE SCREEN AND A TEST CAN BOTH DRIVE IT, which is the same
	 * reason `ApplyChosenClassStats` above is public.
	 *
	 * @return false when nothing was chosen, when there is no player state, or
	 *         when the weapon could not be changed. Says so in the log.
	 */
	bool ApplyCreationChoice();

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

	/**
	 * Draws whatever is worn in the two weapon slots, and clears a hand that
	 * holds nothing. Issue #1125.
	 *
	 * CLEARS AS WELL AS SETS, WHICH IS THE HALF THAT BREAKS SILENTLY. Taking a
	 * weapon off has to remove its mesh, and a version that only ever assigned
	 * would leave the last weapon in the character's hand for ever -- looking
	 * exactly like a weapon that was still equipped, while every number said
	 * otherwise. That is issue #840 one step along: the character's hand and
	 * the character's stats disagreeing, with nothing on screen to say so.
	 *
	 * HUNG ON OnEquipmentChanged RATHER THAN A NEW MOMENT, because that already
	 * runs on every equipment change and already recomputes the stat line and
	 * the ability slots from the same event.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT, which is the same reason
	 * `ApplyChosenClassStats` and `Revive` are public. A test world built with
	 * `UWorld::CreateWorld` has no controller to possess with, so a test cannot
	 * rely on the equipment broadcast reaching a handler bound at BeginPlay.
	 */
	void RefreshWeaponMeshes();

	/**
	 * Swings when the character uses any skill, including the basic attack.
	 * Issue #1126.
	 *
	 * THROUGH THE ANIMATION BLUEPRINT'S SLOT, UNLIKE THE DEATH CLIP. `ABP_Unarmed`
	 * carries a `DefaultSlot`, so an attack blends over the locomotion graph and
	 * back out again rather than cutting to it and cutting back. A death is the
	 * opposite case and is played onto the component directly, because it has to
	 * hold its last frame; see PlayDeathAnimation.
	 *
	 * ROOT MOTION IS SWITCHED OFF ON THE MONTAGE. All four attack clips carry
	 * root motion, measured on 2026-09-01, and a character movement component
	 * takes root motion from montages by default. Left on, every swing would
	 * walk the character forward, which is not what a basic attack should do
	 * when it fires by itself at whatever is in reach.
	 *
	 * THE CLIPS ARE LONGER THAN THE INTERVAL THEY FIT IN, so the rate is raised
	 * rather than the clip being cut off. Attack speed in
	 * `game/Data/ItemBases.csv` runs 1.2 to 1.5 swings a second, an interval of
	 * 0.833 down to 0.667 seconds, and the shortest clip is 1.0 second. The rule
	 * is the Abyssal Warden's: never slower than authored, only faster, and only
	 * when it must be.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT, which is the same reason `Revive` and
	 * `RefreshWeaponMeshes` above are. The base class declares this public, so
	 * an override tucked into the protected section narrows it and stops
	 * anything outside the class from calling it -- which the build refuses
	 * rather than allowing quietly.
	 */
	virtual void PlayAttackAnimation() override;

	// ----------------------------------------------------------------------
	// Art. Issue #1124.
	// ----------------------------------------------------------------------
	//
	// WHERE THESE ASSETS CAME FROM AND WHY THEY ARE COPIES. They were copied
	// out of the engine's own template resources, at
	// Engine/Templates/TemplateResources/High/Characters/Content/Mannequins.
	// That is the folder Unreal copies into a project made from the Third
	// Person template, so the assets already record /Game/Characters/Mannequins
	// as their own package path and an ordinary file copy put them in the right
	// place with every reference intact.
	//
	// NOT LOADED FROM THE ENGINE WHERE THEY SIT, because template resources are
	// not a mounted content root: the editor cannot see them at all until they
	// are copied into a project. game/docs/player-source-assets.md records what
	// was taken and what was deliberately left behind.

	/** The skeletal mesh. `SKM_Manny_Simple` is the body; `SK_Mannequin`
	 *  beside it is the Skeleton every clip below is bound to. */
	static const TCHAR* BodyMeshPath;

	/**
	 * The animation Blueprint's generated class. The `_C` suffix is what makes
	 * it the class rather than the asset, and without it
	 * `TryLoadClass<UAnimInstance>` returns null and the character holds its
	 * reference pose.
	 *
	 * EPIC'S OWN, AND IT DRIVES AN ORDINARY `ACharacter`. `ABP_Unarmed` casts
	 * its owner to `ACharacter` and reads velocity and falling state off the
	 * standard `UCharacterMovementComponent`, which is exactly what this pawn
	 * has, so it needed no change to work here. Its blend space blends idle,
	 * walk and run by speed and direction.
	 *
	 * THE OTHER MANNEQUIN ANIMATION BLUEPRINT IN THE ENGINE DOES NOT WORK HERE.
	 * `ABP_Manny`, in the experimental MoverExamples plugin, casts to
	 * `MoverExamplesCharacter` and reads a `CharacterMoverComponent`. This
	 * project does not use the Mover plugin, so that cast fails every frame and
	 * the blend space gets no speed at all.
	 */
	static const TCHAR* AnimationBlueprintPath;

	/** The folder holding the six death clips, and their names in it. */
	static const TCHAR* DeathAnimationFolder;
	static const TCHAR* DeathAnimationNames[6];

	/**
	 * The folder holding the attack clips, and the three that are used.
	 *
	 * THREE OF THE FOUR THE ENGINE SHIPS. `MM_ChargedAttack` is copied beside
	 * them and is deliberately not in this list: at 1.8333 seconds it is nearly
	 * three times a fast weapon's swing interval, so cycling it into an attack
	 * that fires by itself would mean playing it at close to triple speed. It is
	 * there for a skill that deserves a heavier swing, which is work this does
	 * not do.
	 *
	 * MEASURED, NOT ESTIMATED. `MM_Attack_01` and `MM_Attack_02` are 1.0 second
	 * each and `MM_Attack_03` is 1.6667, read through the editor on 2026-09-01.
	 * `game/docs/player-source-assets.md` records all four.
	 */
	static const TCHAR* AttackAnimationFolder;
	static const TCHAR* AttackAnimationNames[3];

	/**
	 * The animation Blueprint slot an attack is played into.
	 *
	 * `ABP_Unarmed` carries a slot node named `DefaultSlot`, which is what lets
	 * an attack blend over the locomotion graph instead of replacing it.
	 * `ACataclysmAbyssalWardenCharacter` wanted exactly this and could not have
	 * it, because no animation Blueprint has ever been authored for that
	 * creature.
	 */
	static const FName AttackSlotName;

	/**
	 * How long an attack takes to blend in and out of the locomotion graph.
	 *
	 * SHORTER IN THAN OUT, WHICH IS DELIBERATE. A swing should arrive promptly
	 * or it reads as late against the damage, which already lands at the start
	 * of the ability. Leaving is not urgent, so a longer blend out settles back
	 * into walking without a visible step.
	 *
	 * A JUDGEMENT. Nothing measured these; they are a starting point and are
	 * expected to change once somebody watches a fight.
	 */
	static constexpr float AttackBlendInSeconds = 0.10f;
	static constexpr float AttackBlendOutSeconds = 0.25f;

	/**
	 * The most an attack clip may be sped up to fit the swing interval.
	 *
	 * A CEILING RATHER THAN A HOPE, the same shape as the Abyssal Warden's
	 * MaximumPlayRate and the ceiling on how long a corpse is kept. Attack speed
	 * is a stat that affixes and passives raise, and nothing in the design caps
	 * it, so a character stacked far enough would otherwise ask for a clip at
	 * ten times speed, which is a blur rather than a swing. Past this the
	 * animation simply stops keeping up with the damage.
	 */
	static constexpr float MaximumAttackPlayRate = 2.5f;

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
	 * Puts the Mannequin body, its animation Blueprint and its death clips on
	 * this character. Issue #1124.
	 *
	 * WHAT IT REPLACED. Two engine primitives from /Engine/BasicShapes: a
	 * cylinder for the body and a cone stuck on the front, because with a bare
	 * cylinder there was no way to see which way the character faced. Nothing
	 * could be hung on either of them and neither could play anything, so a
	 * player character had no death animation at all.
	 *
	 * CALLED FROM BeginPlay AND NOT FROM THE CONSTRUCTOR. Loading assets in a
	 * constructor also loads them for the class default object, which is built
	 * during module startup. This is the same place and the same reason
	 * `ACataclysmAbyssalWardenCharacter::ResolveBody` is called from.
	 *
	 * @return false when the mesh could not be loaded, which leaves an
	 *         invisible capsule that still walks and still fights. That is the
	 *         state a checkout without the Mannequin assets is in, and it says
	 *         so in the log rather than failing.
	 */
	bool ResolveBody();

	/**
	 * Points the mesh component at `ABP_Unarmed`, or falls back to playing
	 * single clips when it cannot be loaded.
	 *
	 * SEPARATE FROM ResolveBody SO REVIVING CAN REACH IT ON ITS OWN. Dying
	 * switches the component into single-node mode to hold the last frame of a
	 * death clip, so coming back has to put the animation Blueprint back
	 * without reloading the mesh and the six clips again.
	 *
	 * @return whether an animation Blueprint is now driving the mesh
	 */
	bool ResolveAnimationBlueprint(USkeletalMeshComponent* MeshComponent);

	/**
	 * Draws one of the six death clips, at its authored speed.
	 *
	 * SINGLE-NODE MODE RATHER THAN THE ANIMATION BLUEPRINT'S SLOT, and the
	 * reason is timing rather than taste. `ABP_Unarmed` does have a
	 * `DefaultSlot` -- that is what issue #1126 should swing attacks through --
	 * but a montage blends back out to the locomotion graph when its clip ends.
	 * The death clips are around 1.1 seconds and `RespawnDelaySeconds` is 3, so
	 * through the slot the corpse would stand back up in an idle pose and wait
	 * there for nearly two seconds. Played onto the component directly it holds
	 * its last frame, which is what a body on the floor should do. `Revive`
	 * puts the animation Blueprint back.
	 *
	 * @return the clip's length in seconds, or 0 when there was nothing to play
	 */
	float PlayDeathAnimation();

	/**
	 * The six death clips, in the order `Anims/Death` lists them.
	 *
	 * A NULL ENTRY IS KEPT rather than dropped, which is the rule
	 * `ACataclysmEnemyCharacter::PlayDeathAnimation` already follows: dropping
	 * one would change how many clips there are and therefore which one every
	 * death draws.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> DeathAnimations;

	/**
	 * The attack clips, drawn in turn rather than at random.
	 *
	 * IN TURN, UNLIKE THE DEATH CLIPS, and the difference is how often they are
	 * seen. A death happens once and a random draw stops two deaths in a row
	 * looking identical. A basic attack fires every two thirds of a second, and
	 * a random draw over three clips repeats one about a third of the time,
	 * which reads as the animation sticking. Cycling is what
	 * `ACataclysmAbyssalWardenCharacter` does with its left and right swings.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> AttackAnimations;

	/** Which attack clip comes next. Wraps. */
	int32 NextAttackAnimation = 0;

	/**
	 * What is drawn in each hand. Issue #1125.
	 *
	 * WHAT THEY REPLACED: nothing at all. No weapon was drawn anywhere in this
	 * game. A player equipped a Greataxe, its stats and its six skills changed,
	 * and nothing on screen changed.
	 *
	 * ONE PER HAND, BECAUSE THE DESIGN HAS AN OFF-HAND. The Shield is a
	 * one-handed base, so a character can hold a sword and a shield at once.
	 * `ECataclysmGearSlot::Weapon1` is drawn in the right hand and `Weapon2` in
	 * the left, which is also how the equipment component already thinks about
	 * them.
	 *
	 * A TWO-HANDED WEAPON DRAWS IN THE RIGHT HAND ONLY, and that is a
	 * limitation rather than a decision. `UCataclysmEquipmentComponent` puts a
	 * two-handed weapon in Weapon1 and blocks Weapon2, so the left hand is
	 * empty. Making both hands hold it needs an animation authored for it, and
	 * there is no two-handed grip pose in anything this project owns.
	 *
	 * ATTACHED TO THE MESH'S HAND SOCKETS, not to the capsule, so they follow
	 * the animation. `SK_Mannequin` ships `HandGrip_R` and `HandGrip_L`, so
	 * neither socket had to be authored.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Weapon")
	TObjectPtr<UStaticMeshComponent> RightHandWeapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Weapon")
	TObjectPtr<UStaticMeshComponent> LeftHandWeapon;

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
	void FillAbilitySlotsFromWornWeapon(
		ECataclysmWeaponExpected Expected = ECataclysmWeaponExpected::Yes);

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
