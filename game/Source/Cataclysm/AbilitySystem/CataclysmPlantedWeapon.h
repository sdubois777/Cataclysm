// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmPlantedWeapon.generated.h"

class ACataclysmGroundZone;

/**
 * A weapon driven into the ground and left standing there until it is pulled
 * free.
 *
 * WHAT ASKS FOR IT. The Greatsword's Buried Fire: "drive the greatsword into
 * the ground and leave it there. It burns everything within 4 meters and grows
 * hotter every second it stands. Pull it free within 10 seconds to erupt for
 * damage that rises with how long you left it. You fight unarmed until you do."
 * Its row states `DisarmsUntilRecalled=1`, and until 2026-09-02 nothing read it.
 *
 * AN ACTOR RATHER THAN STATE ON THE ABILITY, AND THE PROJECT OWNER CHOSE THIS
 * ROUTE OVER TWO OTHERS. The reason is `UCataclysmWeaponSlotsComponent::
 * UnequipWeapon`: its first act is `GrantedHandles.TakeFromAbilitySystem`, which
 * revokes every ability the weapon granted. Any design where the disarm actually
 * takes the weapon's abilities away destroys the skill instance that would be
 * holding the sword's position -- the skill would delete the state its own
 * second press needs. A world object cannot be revoked by an ability system.
 *
 * IT IS ALSO WHERE THE QUESTION "IS THIS CHARACTER HOLDING ANYTHING?" IS
 * ANSWERED. `HeldBy` is asked by `UCataclysmSkillTemplate::CanActivateAbility`,
 * which refuses the weapon's other skills while the sword is in the ground, and
 * by `ACataclysmPlayerCharacter::RefreshWeaponMeshes`, which draws empty hands.
 * That is the same shape `UCataclysmMovementSkill::IsBeingWalkedByASkill` takes
 * and its header gives the reason: asking means nothing has to be written onto
 * the character and nothing has to be cleared when the owner dies.
 *
 * NOTHING IS ACTUALLY REVOKED, THEREFORE. The disarm is a refusal rather than a
 * removal. Two things follow and both are wanted: the skill that planted the
 * sword is still granted, so the second press reaches it; and a character cannot
 * be left permanently weaponless by a plant that went wrong, because there is
 * nothing to give back -- only an actor to destroy.
 *
 * ITS LIFETIME BELONGS TO THE SKILL THAT PLANTED IT, WHICH IS THE OPPOSITE OF
 * `ACataclysmTether`. That one outlives the projectile that made it, because the
 * bolt lands and the line holds for eight seconds afterwards. This one does not
 * outlive anything: Buried Fire stays active for as long as the sword stands, so
 * that the second press arrives as input to a running ability rather than as a
 * fresh activation the Special slot's five second cooldown would refuse. One
 * owner, one end.
 *
 * NO MESH. The sword in the ground is invisible, for the reason
 * `ACataclysmGroundZone` gives about the patch it stands in: this project has
 * almost no art content, and the effect is the only evidence either is there.
 * What the character is holding IS emptied, so a planted greatsword is visible
 * as empty hands. Issue #811 covers the rest.
 */
UCLASS()
class CATACLYSM_API ACataclysmPlantedWeapon : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmPlantedWeapon();

	/**
	 * Drive a weapon into the ground and return it, or null if it could not be.
	 *
	 * @param InCaster        whose skill this is, and who fights unarmed until
	 *                        it is pulled free
	 * @param Where           the spot it is driven into
	 * @param InWeaponType    what is standing in the ground. Recorded rather than
	 *                        acted on; see the field below
	 * @param InFire          the patch of burning ground the plant left, which
	 *                        this grows hotter. Null leaves nothing to grow
	 * @param InMorePerSecond percentage points of more damage per second stood,
	 *                        which is the row's `MoreDamagePer` beside
	 *                        `ScalingSource=Second`
	 */
	static ACataclysmPlantedWeapon* Plant(AActor* InCaster, const FVector& Where,
										  const FString& InWeaponType,
										  ACataclysmGroundZone* InFire,
										  float InMorePerSecond);

	/**
	 * The weapon this character has left in the ground, or null.
	 *
	 * A SEARCH RATHER THAN A REGISTER, and the world holds one of these at a
	 * time in ordinary play. The alternative -- a map from character to sword
	 * kept on a static -- would have to be swept for characters that died, were
	 * destroyed, or left the level, which is the argument
	 * `UCataclysmCurseSpread` gives for being a component rather than a map.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Planted Weapon")
	static ACataclysmPlantedWeapon* HeldBy(const AActor* Who);

	/**
	 * How long it has stood, in seconds.
	 *
	 * COUNTED RATHER THAN SUBTRACTED FROM THE WORLD CLOCK, and the reason is
	 * that both things that read it have to be testable. A world built by
	 * `UWorld::CreateWorld` is never ticked, so its clock does not advance and
	 * its timers never fire; a test drives `GrowHotter` by hand instead, exactly
	 * as it drives `UCataclysmSelfBuffSkill::SecondPassed` and
	 * `UCataclysmGroundZone::Sweep`. Two clocks would let the heat and the
	 * eruption disagree about how long the same sword had been standing.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Planted Weapon")
	float SecondsPlanted() const { return static_cast<float>(SecondsStood); }

	/**
	 * One second of standing: count it, and make the fire around it fiercer.
	 *
	 * "IT BURNS EVERYTHING WITHIN 4 METERS AND GROWS HOTTER EVERY SECOND IT
	 * STANDS." The row's `MoreDamagePer=12` with `ScalingSource=Second` is one
	 * number read by two things -- this, and the eruption's damage -- because the
	 * sentence after it says the eruption rises the same way. Two parameters
	 * would let the burning and the eruption drift apart, and the row states one.
	 *
	 * FROM THE FIGURE THE PATCH STARTED AT, NOT FROM THE ONE IT HAS NOW. Raising
	 * the current figure by a proportion every second would compound: at 12% a
	 * second, ten seconds would be 3.1 times rather than 2.2. The design's `more`
	 * bucket is a rate multiplied by a count, which is what
	 * `UCataclysmSkillTemplate::ScaledDamagePercent` does with the same two
	 * numbers.
	 *
	 * Public so a test can drive it without a world that ticks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Planted Weapon")
	void GrowHotter();

	/**
	 * Which weapon type is standing in the ground.
	 *
	 * RECORDED RATHER THAN LOAD-BEARING, AND THAT IS WORTH SAYING PLAINLY. It is
	 * what the log names and what anything asking "what has this character left
	 * behind" would read. Nothing is given back from it, because nothing was
	 * taken: the disarm is a refusal, and the hands are filled again from what is
	 * still worn. It would become load-bearing the day the disarm really
	 * unequips, which is issue #1166's territory.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Planted Weapon")
	FString WeaponType;

	/** Whose sword it is. Empty hands and refused skills are theirs. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Planted Weapon")
	TWeakObjectPtr<AActor> Caster;

	/** How many seconds it has stood. Read by tests; see `SecondsPlanted`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Planted Weapon")
	int32 SecondsStood = 0;

	/** The patch of burning ground it stands in, which it makes fiercer. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Planted Weapon")
	TWeakObjectPtr<ACataclysmGroundZone> Fire;

	/** Percentage points of more damage per second stood. `MoreDamagePer`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Planted Weapon")
	float MorePerSecond = 0.0f;

	/** What that patch dealt per second when the sword went in. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Planted Weapon")
	float FireStartedAt = 0.0f;

	/** Seconds between one count of standing and the next. */
	static constexpr float SecondsPerCount = 1.0f;

protected:
	virtual void BeginPlay() override;

	/**
	 * Fills the character's hands again, whatever destroyed the sword.
	 *
	 * HERE RATHER THAN IN THE SKILL, so that a sword destroyed by something the
	 * skill never hears about -- a level change, an editor delete -- still leaves
	 * the character holding something. The skill's own end destroys this actor
	 * and therefore comes through here too.
	 */
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	/**
	 * Gives the actor a position at all.
	 *
	 * For the reason `ACataclysmGroundZone` states: an actor with no root
	 * component reports its location as the world origin however it was spawned,
	 * and where the sword is standing is the whole of what the eruption needs.
	 */
	UPROPERTY()
	TObjectPtr<class USceneComponent> Anchor;

	/** Asks the player character to draw its hands again. Harmless for anyone else. */
	void RedrawTheHandsOf(AActor* Who) const;

	FTimerHandle CountTimer;
};
