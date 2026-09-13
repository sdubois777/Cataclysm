// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
// For the side it is on. A hazard has no team of its own and reads one from
// whatever owns it, so the side a floor's hazards take is the side of this.
#include "GenericTeamAgentInterface.h"
#include "CataclysmFloorHazardSource.generated.h"

class UCataclysmAbilitySystemComponent;
class USceneComponent;

/**
 * Whose name a floor's hazards are dealt in.
 *
 * IT DOES NOTHING AND EXISTS ONLY TO BE ASKED. It has no mesh, no tick, no
 * behaviour and no state that changes. The next reader will reasonably wonder
 * why an empty actor is spawned at all, so the answer is first:
 *
 * EVERY ROUTE THAT APPLIES ANYTHING TO A TARGET REFUSES UNLESS THE SOURCE
 * ACTOR RESOLVES TO AN ABILITY SYSTEM COMPONENT. In
 * `UCataclysmSkillEffects`, `ApplyDirectDamage`, `ApplyNamedEffect`, `ApplyPin`
 * and `ApplyTagForDuration` all open with the same two lines -- the source's
 * component and the target's, and a refusal if either is missing.
 * `UCataclysmTargeting::AbilitySystemOf` answers through
 * `UAbilitySystemGlobals::GetAbilitySystemComponentFromActor`, so an actor that
 * carries no component and implements no interface has nothing for it to find.
 *
 * THE TRAP IS `ApplyNamedEffect`. When the effect moves no stats, or when the
 * source has no component, it falls back to `ApplyTagForDuration` -- which reads
 * as "at least the bare tag still lands". It does not. That function repeats the
 * identical refusal, so the fallback fails for exactly the reason that sent you
 * to it.
 *
 * SO THE DUNGEON GAME MODE CANNOT OWN A FLOOR HAZARD. A game mode carries no
 * ability system component. One owned by it would spawn, sweep, find everyone
 * standing in it, and then silently do nothing to any of them -- which is worse
 * than not building the hazard, because the floor would report it as built.
 *
 * AND NEITHER CAN THE CREATURE THAT LEFT IT, WHICH IS THE CHEAPER ANSWER AND IS
 * RULED OUT BY THE DESIGN RATHER THAN BY TASTE. Two of the eight dungeon
 * modifiers that want a hazard place one at the instant its would-be owner dies:
 * Withered Ground leaves "patches of Barren Earth on death", and Leech Spores
 * explodes "from their corpse". An owner that must be alive cannot serve either.
 * Issue #1605 carries the eight rows.
 *
 * ONE PER WORLD, FOUND OR MADE BY `ForFloor`. It is destroyed with the rest of
 * the floor's contents by `UCataclysmFloorContents::ClearTheFloor`, and the next
 * floor's first hazard makes a fresh one. That is why nothing has to spawn it at
 * floor creation time, and why `ACataclysmDungeonGameMode::BuildFloor` is
 * untouched by this.
 */
UCLASS()
class CATACLYSM_API ACataclysmFloorHazardSource : public AActor,
												  public IAbilitySystemInterface,
												  public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ACataclysmFloorHazardSource();

	/**
	 * The one for this world, spawning it if there is none yet.
	 *
	 * @return the source, or null if the world is null
	 */
	static ACataclysmFloorHazardSource* ForFloor(UWorld* World);

	/**
	 * The one for this world, or null. Never spawns one.
	 *
	 * Read by tests, which need to be able to tell "there is none" from "there
	 * is one" without creating the thing they are asking about.
	 */
	static ACataclysmFloorHazardSource* Existing(const UWorld* World);

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	//~ End IGenericTeamAgentInterface

	/**
	 * Which of the player's eight resistances this hazard's damage is met by.
	 *
	 * WITHOUT THIS, A HAZARD MEETS NO RESISTANCE AT ALL.
	 * `ACataclysmGroundZone::Sweep` damages through `ApplyDirectDamage` with this
	 * actor as the source, and that path asks
	 * `UCataclysmSkillEffects::DamageTypeOf` for the source's type to decide the
	 * hit's element. Until this existed the answer was nothing, so a Demonic
	 * modifier's burning ground ignored a player's Demonic resistance -- harsher
	 * than any creature attack and a whole player stat worth nothing against it.
	 *
	 * IT IS READ OFF THE PLACING MODIFIER'S ROW, NOT CHOSEN. Every row of
	 * `game/Data/DungeonModifiers.csv` carries a `CataclysmType`, so the modifier
	 * that places a hazard says what it deals and the two cannot disagree.
	 *
	 * EMPTY MEANS UNTYPED, which is both the safe default and the right answer
	 * for the one row typed `Generic`: the player's resistances are War, Demonic,
	 * Death, Pestilence, Famine, Celestial, Chaos and Void, and Generic is not
	 * among them, so there is nothing for a type to select. That row places no
	 * hazard today in any case -- it is a creature that hunts the player.
	 *
	 * TYPING THIS APPLIES THE PROJECT OWNER'S RULE RATHER THAN WIDENING IT. The
	 * second paragraph of `DamageTypeOf`'s comment says a projectile's hit is
	 * typed by whoever fired it, "which is the same rule and not a special case".
	 * A hazard is that shape: a thing sent by a source.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Floor Hazard")
	FName DamageType;

protected:
	/**
	 * An empty root so the actor has a position at all.
	 *
	 * NOT DECORATION, and `ACataclysmGroundZone` and `ACataclysmTerrain` both
	 * carry this comment for the same reason: an actor whose components are all
	 * non-scene components has no root, and an actor with no root reports its
	 * location as the world origin however it was spawned. Nothing reads this
	 * actor's position today -- a hazard carries its own -- so this is here to
	 * keep it from being a special case rather than because a caller needs it.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Floor Hazard")
	TObjectPtr<USceneComponent> Anchor;

	/**
	 * The whole reason the actor exists. See the class comment.
	 *
	 * NO ATTRIBUTE SETS, DELIBERATELY. `UCataclysmDamageCalculation::Resolve`
	 * reads the DEFENDER's combat attributes and not the source's, so a hazard
	 * deals what its modifier row states rather than what a caster's stats would
	 * make of it. That is the right answer here: a floor rule is not a character
	 * and has no gear, no passives and no buffs to scale by.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Floor Hazard")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

private:
	/**
	 * Which side its hazards are on. Monsters, and chosen rather than inherited.
	 *
	 * WHY A SIDE AT ALL. `UCataclysmTeams::TeamOf` walks the owner chain, so a
	 * hazard with no team of its own answers with this one. Leaving it unset
	 * would not make a hazard neutral: that class records that having no team
	 * means hostile to everything, including the creatures a floor spawns.
	 *
	 * AND THE SIDE ALONE DOES NOT CARRY THE BEHAVIOUR, WHICH IS THE PART THAT
	 * READS WRONG IF IT IS NOT SAID. Four of the eight rows in issue #1605 do
	 * one thing to the player and the opposite to creatures standing in the same
	 * patch -- Necrotic Ground damages the player and regenerates enemies,
	 * Hallowed Groundfall burns the player and empowers enemies, Leech Spores
	 * drains the player to heal enemies, Plague Harbingers damages the player
	 * and amplifies enemy stats. A hazard serving those has to search for
	 * everyone and then branch per target, so this decides which way round the
	 * branch reads and not who is affected.
	 *
	 * AND THE SEARCH CANNOT MAKE THAT BRANCH FOR A HAZARD, WHICH IS WHY THE
	 * HAZARD HAS TO. `UCataclysmTargeting::Gather` takes a `bEveryone` path,
	 * and on that path it never consults attitude at all: it asks only that the
	 * actor is valid, carries an ability system and is not dead. So a hazard
	 * searching with `FindEveryoneInLine` is handed both sides mixed together
	 * and must sort them itself. The side recorded here is what it sorts them
	 * against.
	 */
	FGenericTeamId TeamId;
};
