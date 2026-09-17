// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// For the Status.* tag a zone may lay on whatever stands in it. The Wand's
// Foul Wake strips resistance from anything that walks into its ground.
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "CataclysmGroundZone.generated.h"

class UNiagaraComponent;

/**
 * A patch of burning ground that hurts what stands in it, then goes away.
 *
 * A RIDER ON A SHAPE, NOT A SHAPE. Eight of the sixteen designed Demonic skills
 * leave one of these behind on top of whatever else they do: Molten Cleave
 * "drags a line of molten slag", Emberhurl leaves "its flight path burning",
 * Infernal Plunge leaves "a pool of lava". Issue #37's own table of shapes says
 * "persistent ground zone: used by most of the above", where every other entry
 * names the skills that use it -- which is the hint that it is a component of
 * the others rather than a seventh peer.
 *
 * IT RE-TESTS WHO IS INSIDE ON EVERY TICK rather than remembering who was there
 * when it was created. Standing in it is the cost, so walking out has to stop
 * it and walking in has to start it.
 *
 * IT IS A CAPSULE, NOT A CIRCLE. A zone has a start, a far end and a width, and
 * a circle is the case where the two ends are the same point. That is what lets
 * Emberhurl leave "its flight path burning" as one actor along the whole throw
 * rather than one patch where the axe stopped. Issue #167. The two cases share
 * one search because UCataclysmTargeting::IsInLine already treats a segment of
 * no length as a circle at its start, so there is no branch to get wrong.
 *
 * NO MESH AND NO PARTICLE EFFECT. This project has no art content at all -- 14
 * data tables, 10 input actions and one map -- so the zone is invisible and its
 * effect is the only evidence it is there. That is a content gap, not a
 * behaviour gap.
 */
UCLASS()
class CATACLYSM_API ACataclysmGroundZone : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmGroundZone();

	/**
	 * Put a round one in the world.
	 *
	 * @param Owner        whose skill left it; it never hurts them
	 * @param Location     where the centre is
	 * @param RadiusCm     how wide
	 * @param Duration     how long before it goes away
	 * @param DamagePerTick how much each enemy inside takes per tick
	 * @param InDamageType  the type its damage is met by, or NAME_None for its
	 *                      owner's. See `DamageType`.
	 * @return the zone, or null if any of the numbers is not positive
	 */
	static ACataclysmGroundZone* Spawn(AActor* Owner, const FVector& Location,
									   float RadiusCm, float Duration,
									   float DamagePerTick,
									   FName InDamageType = NAME_None);

	/**
	 * Put a long one in the world, covering everything within HalfWidthCm of the
	 * segment from Start to End.
	 *
	 * ONE ACTOR, NOT A ROW OF THEM. A chain of overlapping circles laid along the
	 * path would reuse Spawn unchanged, but it spawns as many actors as the path
	 * is long over the spacing, each with its own timer and its own sweep, and an
	 * enemy standing where two of them overlap takes two ticks a second instead
	 * of one. Path of Exile's Flame Wall -- the shipped ground effect nearest to
	 * this -- is a single line-shaped area, and its own rule is that an enemy can
	 * only be damaged by one Flame Wall at a time.
	 *
	 * @param HalfWidthCm  how far either side of the line it reaches
	 * @param bBurnsEveryone  whether it burns whatever is standing in it rather
	 *                        than only the owner's enemies -- **including the
	 *                        owner**. One thing in the design needs it: the
	 *                        Hellhound's burning lane, whose model entry says
	 *                        "The fire burns other enemies and the Hellhound
	 *                        itself". Everything else leaves a zone that knows
	 *                        whose side it is on.
	 * @param InDamageType    the type its damage is met by, or NAME_None for
	 *                        its owner's. See `DamageType`.
	 */
	static ACataclysmGroundZone* SpawnAlong(AActor* Owner, const FVector& Start,
											const FVector& End, float HalfWidthCm,
											float Duration, float DamagePerTick,
											bool bBurnsEveryone = false,
											FName InDamageType = NAME_None);

	/**
	 * Put one in the world that lasts until the player leaves the floor.
	 *
	 * IT TAKES NO DURATION, AND THAT IS THE WHOLE DIFFERENCE. Five of the eight
	 * dungeon modifiers that want a hazard state no duration at all -- Withered
	 * Ground, Necrotic Ground, Leech Spores, Hallowed Groundfall and Plague
	 * Harbingers -- because the design means them to last the floor. Issue
	 * #1605.
	 *
	 * SO IT NEVER CALLS `SetLifeSpan`, AND THE FLOOR ENDING IS WHAT ENDS THIS.
	 * `UCataclysmFloorContents::ClearTheFloor` destroys every zone in the world
	 * when the next floor is a new arena, and
	 * `ACataclysmDungeonGameMode::ApplyFloorRulesToPlayer` destroys every zone the
	 * floor's rules placed when it is not, which is a Horde dungeon's next wave
	 * (issue #1925). The alternative considered was a very large duration, which
	 * would be a false number sitting in the data for a reader to find and
	 * believe.
	 *
	 * BESIDE `Spawn` AND `SpawnAlong` RATHER THAN CHANGING EITHER. Both refuse
	 * a non-positive duration and both still do; no existing caller behaves
	 * differently because this exists.
	 *
	 * @param bAffectsEveryone  whether it acts on whatever is standing in it
	 *                          rather than only the owner's enemies. A floor
	 *                          hazard owned by an
	 *                          `ACataclysmFloorHazardSource` wants this when its
	 *                          row does one thing to the player and another to
	 *                          creatures in the same patch, which four of the
	 *                          eight rows do.
	 * @param InDrawnAsType  which element's colours to draw it in, or NAME_None
	 *                       to draw it in its damage type's, or with neither
	 *                       its owner's. See `DrawnAsType`.
	 * @param InDamageType   the type its damage is met by, or NAME_None for its
	 *                       owner's. See `DamageType`.
	 * @return the patch, or null if the width is not positive
	 */
	static ACataclysmGroundZone* SpawnForTheFloor(AActor* Owner,
												  const FVector& Start,
												  const FVector& End,
												  float HalfWidthCm,
												  float DamagePerTick,
												  bool bAffectsEveryone = false,
												  FName InDrawnAsType = NAME_None,
												  FName InDamageType = NAME_None);

	/**
	 * Whether it burns whatever is standing in it, including its own owner.
	 *
	 * **NOTHING SETS THIS. It is false for every zone in the game.** The
	 * Hellhound's burning trail was the one caller until 2026-08-20, when the
	 * project owner set a general rule that a creature does not burn itself or
	 * its own side. The Gatekeeper's Soulfall was designed to set it too and
	 * never did, because the rule arrived before that creature was built.
	 *
	 * **IT IS KEPT RATHER THAN DELETED, BY THE OWNER'S CHOICE**, so the option
	 * is on the record as considered and rejected rather than never thought of.
	 * `docs/DECISIONS.md` carries the reversal.
	 *
	 * IF SOMETHING SETS IT AGAIN, that is a design decision and not a detail:
	 * `test_nothing_burns_its_own_side` in
	 * `tools/tests/test_hellhound_matches_the_model.py` refuses it, and the
	 * failure names the documents that would have to change with it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	bool bBurnsEveryone = false;

	/**
	 * Also lay a named status effect on whatever is standing in it.
	 *
	 * THE WAND'S FOUL WAKE ASKS FOR IT: "the ground you fled burns for 6 seconds
	 * and strips the Demonic resistance of anything that walks into it". Until
	 * 2026-09-01 a zone could only deal damage, so that row's `Effect=Shred`
	 * reached nothing.
	 *
	 * SET AFTER SPAWNING RATHER THAN PASSED IN. `SpawnAlong` already takes seven
	 * arguments and four more would make every existing call site harder to read
	 * for the sake of the one zone that wants them.
	 *
	 * APPLIED ON EVERY SWEEP, which refreshes rather than stacks: every
	 * player-applied effect in this design is single stack. So the curse lasts
	 * its own duration from the moment the target last stood in the zone, which
	 * is what "anything that walks into it" means.
	 *
	 * @param EffectTag  the Status.* tag to lay, or an invalid tag for none
	 * @param Seconds    how long it lasts on the target
	 * @param Magnitude  its size, or zero to take the effect's designed strength
	 * @param InDamageType  the caster's damage type, deciding which resistance a
	 *                      Shred reduces
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Ground Zone")
	void AlsoApply(FGameplayTag EffectTag, float Seconds, float Magnitude,
				   FName InDamageType);

	/**
	 * Also make this patch heal whoever left it faster while they stand in it.
	 *
	 * ONE ROW ASKS. The Fist's Blood Pyre: "standing in your own pyre does you
	 * no harm AND DOUBLES YOUR HEALTH REGENERATION." Issue #1162.
	 *
	 * A SCALE ON THE RATE AND NOT A SECOND SOURCE OF HEALING, so a character
	 * with no regeneration at all still gets none. "Doubles your health
	 * regeneration" is a multiplier and reads as one.
	 *
	 * @param Scale  what the owner's health regeneration is multiplied by while
	 *               it stands inside. Two for "doubles". One or less changes
	 *               nothing
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Ground Zone")
	void AlsoHealItsOwner(float Scale);

	/**
	 * What this character's health regeneration is multiplied by, from any
	 * patch of ground it left and is standing in.
	 *
	 * ONE, WHICH IS THE ANSWER FOR EVERY CHARACTER IN THE GAME BUT ONE STANDING
	 * IN ITS OWN BLOOD PYRE. Read by `UCataclysmRegeneration::ApplyStep`.
	 *
	 * THE LARGEST OF THEM RATHER THAN THEIR PRODUCT, when a character somehow
	 * stands in two of its own. Nothing can produce that today -- Blood Pyre is
	 * the only row that asks and its cooldown is longer than its ground lasts --
	 * and multiplying two doublings into a quadrupling is the wrong answer for a
	 * pair of patches that each promise "doubles".
	 *
	 * IT WALKS THE WORLD'S ZONES, which is what `HeldBy` does for a planted
	 * weapon and for the same reason: a patch of ground has no component on the
	 * character and registering one would need clearing on every way a zone can
	 * end. There are single figures of these in a level.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Ground Zone")
	static float RegenerationScaleFor(const AActor* Who);

	/** Seconds between one sweep of who is standing in it and the next. */
	static constexpr float TickSeconds = 1.0f;

	/**
	 * How long ONE DRAWING of a floor-lasting patch is told to last.
	 *
	 * THREE SECONDS BECAUSE THAT IS THE SHORTEST GROUND DURATION ALREADY IN THE
	 * DATA, and the number is a bound rather than a preference. The visual
	 * components a patch asks for are not attached to it -- see
	 * `UCataclysmGroundEffect::PlayFor` -- so they keep playing after the patch
	 * is destroyed, including after a floor change. That is issue #1660 and it
	 * happens today for timed patches, whose stated durations run from 3 to 10
	 * seconds.
	 *
	 * DRAWING FOR THREE MEANS A FLOOR-LASTING PATCH LEAVES AT MOST WHAT THE
	 * MILDEST TIMED ONE ALREADY LEAVES, and under a third of the worst. So this
	 * cannot make #1660 worse, which is checkable rather than asserted.
	 *
	 * IT DOES NOT REMOVE THE OVERHANG. The floor transition is immediate --
	 * `BuildFloor`, `ClearTheFloor`, `PopulateFloor` and `PlaceStairs` run in
	 * one pass and the player is teleported in the same one, and there is no
	 * fade or level load anywhere in this module to hide it behind. The
	 * leftover is visible and bounded, not hidden. Removing it is #1660.
	 */
	static constexpr float FloorDrawSeconds = 3.0f;

	/**
	 * How often a floor-lasting patch asks to be drawn again.
	 *
	 * SHORTER THAN `FloorDrawSeconds` ON PURPOSE, AND TIDYING THE TWO TO MATCH
	 * REINTRODUCES A FLICKER. A drawing that lasts three seconds, re-asked every
	 * three, has the old one ending as the new one begins: any timer jitter, or
	 * simply the order things tick in, leaves a frame with nothing drawn, and a
	 * patch that blinks out once every three seconds is a bug a player would
	 * report. Asking early means the two overlap briefly instead.
	 *
	 * IT DOES NOT WEAKEN THE BOUND ABOVE. What can outlive a floor change is one
	 * DRAWING, so the overhang is bounded by `FloorDrawSeconds`. A shorter
	 * period only ever closes the gap between drawings; it never extends how
	 * long one of them lasts.
	 */
	static constexpr float FloorRedrawSeconds = 2.5f;

	/** Whether it lasts until the floor ends rather than for a stated time. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	bool bLastsTheFloor = false;

	/**
	 * How many times it has asked to be drawn again. Read by tests.
	 *
	 * THE ONLY THING A TEST CAN SEE ABOUT ANY OF THIS. No automation test in
	 * this project can observe a drawn effect: the run passes `-nullrhi` and
	 * Niagara creates no component when `FApp::CanEverRender()` is false. So a
	 * hazard that stopped asking would be invisible in play and silent in the
	 * suite, and this counter plus `UCataclysmGroundEffect::TimesAsked` is what
	 * makes the redraw assertable at all.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	int32 RedrawsAsked = 0;

	/** How wide it is, in centimetres. Its radius when round, half its width when long. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	float RadiusCm = 0.0f;

	/**
	 * The far end, in world space. Equal to the actor's own location when round.
	 *
	 * The near end is the actor's own location, so a zone is where it says it is
	 * in the outliner and its two ends cannot disagree.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	FVector FarEnd = FVector::ZeroVector;

	/** Whether it covers a path rather than a point. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Ground Zone")
	bool IsLong() const { return !FarEnd.Equals(GetActorLocation()); }

	/** What one tick deals to each enemy inside. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	float DamagePerTick = 0.0f;

	/** How many times it has swept. Read by tests; there is nothing else to see. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	int32 TicksElapsed = 0;

	/** How many enemies the last sweep found. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	int32 LastSweepCount = 0;

	/** The status effect each sweep lays, or an invalid tag for none. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	FGameplayTag AppliedEffect;

	/** How long that effect lasts on whatever the sweep found. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	float AppliedEffectSeconds = 0.0f;

	/** Its size, or zero to take the effect's own designed strength. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	float AppliedEffectMagnitude = 0.0f;

	/** The caster's damage type, deciding which resistance a Shred reduces. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	FName AppliedEffectDamageType;

	/**
	 * Which element's colours this patch is drawn in, or NAME_None for its
	 * owner's. Issues #1820 and #41.
	 *
	 * BEFORE THIS, A PATCH'S APPEARANCE WAS ITS OWNER'S AND NOTHING ELSE.
	 * `BeginPlay` and `Redraw` both passed
	 * `UCataclysmSkillEffects::DamageTypeOf(GetOwner())` straight into
	 * `UCataclysmGroundEffect::PlayFor`, so every patch one owner leaves was the
	 * same colour as every other. That is right for a creature's zones and for a
	 * floor rule that places one kind of hazard, and it cannot express a rule
	 * that places two kinds a player is meant to tell apart.
	 *
	 * IT IS ONLY THE APPEARANCE AND NOT THE DAMAGE, which matters because the
	 * value is a damage type name. Nothing reads this to decide what a hit is
	 * met by: that is `DamageType` below, or the owner's type when that is unset.
	 * So a patch drawn in one element's colours still deals its own type's
	 * element, and a patch that deals nothing -- `DamagePerTick` of zero and no
	 * curse, which `Sweep` returns early on -- never asks about an element at all.
	 *
	 * ONE FLOOR HAS ONE HAZARD SOURCE, which is the other half of why this is per
	 * zone. `ACataclysmFloorHazardSource::ForFloor` answers the same actor for
	 * every rule on the floor, so nothing held on that actor can say which rule
	 * placed a patch. Until issue #1924 its one type field was written by three
	 * rules and read by every patch, so a patch's colour could be changed by a
	 * later rule placing something else. A value held on the patch cannot be.
	 *
	 * NAME_None RATHER THAN A FLAG, so the unset state and "draw it untyped" are
	 * the same request. `UCataclysmElementVisuals::ColoursFor` answers false for
	 * NAME_None and leaves the system's authored white, and
	 * `UCataclysmSkillEffects::DamageTypeOf` answers NAME_None for a player, so a
	 * player's zone already drew white before this existed and still does.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	FName DrawnAsType;

	/**
	 * Which of the defender's resistances this patch's damage is met by, or
	 * NAME_None for its owner's. Issue #1924.
	 *
	 * SET BY A DUNGEON FLOOR RULE, FROM ITS ROW. A floor's patches all belong to the
	 * floor's one hazard source, which carries no type, so the rule that places a
	 * patch says what it deals by passing its row's `CataclysmType`. A patch placed
	 * by another rule afterwards cannot change it.
	 *
	 * NAME_None FOR EVERY OTHER PATCH, whose owner types it as before: a creature's
	 * burning ground deals the creature's element and a player's deals none.
	 * `Sweep` puts this on each blow's delivery, where
	 * `UCataclysmSkillEffects::ApplyTypedSpec` prefers it to the owner's, and
	 * `TypeItIsDrawnAs` draws the patch in it when `DrawnAsType` is unset.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	FName DamageType;

	/**
	 * Which element's colours this patch is actually drawn in.
	 *
	 * ONE FUNCTION FOR THE TWO PLACES THAT DRAW. `BeginPlay` draws once and
	 * `Redraw` draws again every few seconds for a floor-lasting patch; the two
	 * deciding separately is how a patch could change colour partway through its
	 * life without anything reporting it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Ground Zone")
	FName TypeItIsDrawnAs() const;

	/**
	 * What the owner's health regeneration is multiplied by while it stands in
	 * this patch. One for every patch but Blood Pyre's.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground Zone")
	float OwnersRegenerationScale = 1.0f;

	/** Whether this point falls inside this patch. Read by tests. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Ground Zone")
	bool Covers(const FVector& Point) const;

	/** Burn everything standing in it now. Called on a timer, and by tests. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Ground Zone")
	void Sweep();

	/**
	 * Make this patch travel, in centimetres per second along each axis.
	 *
	 * WHAT ASKS FOR IT. Funereal Procession: "A slow-moving line of spectral
	 * pallbearers moves through the dungeon; contact causes heavy damage and
	 * fear." Nothing in the game could move an area. Issue #1649.
	 *
	 * ZERO MEANS IT STAYS WHERE IT WAS PUT, which is every patch in the game
	 * before this and every one after it that does not ask otherwise.
	 *
	 * THIS IS WHAT TURNS TICKING ON. A patch that does not travel still does
	 * not tick, for the reason the constructor gives: it finds who is standing
	 * in it on a timer a second apart, and ticking would ask sixty times as
	 * often for the same answer.
	 */
	void TravelAt(const FVector& CentimetresPerSecond);

	/**
	 * Move the patch by one step of travel. Does nothing if it does not travel.
	 *
	 * PUBLIC SO A TEST CAN DRIVE IT, which is not a style choice: automation
	 * tests build a world with `UWorld::CreateWorld` and never tick it, so a
	 * patch that moved only from `Tick` could not be tested at all. Every
	 * repeating thing in this project exposes its one repetition the same way
	 * -- `Sweep` above, `ACataclysmProjectile::Step`, `SwingOnce`, `Pulse`.
	 *
	 * IT CARRIES THREE THINGS AND NOT ONE. The actor, the far end, and the
	 * visual effects. See the definition for why each has to be moved by hand.
	 */
	void TravelStep(float StepSeconds);

	/**
	 * How far along each axis this patch moves in a second. Zero: it does not.
	 *
	 * IN CENTIMETRES BECAUSE EVERY DISTANCE IN THIS FILE IS. `RadiusCm` above
	 * and `AuraRadiusCm` beside it are both centimetres, and one file mixing
	 * units is how a patch ends up a hundred times too fast.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground")
	FVector TravelPerSecond = FVector::ZeroVector;

	/** How far this patch has travelled in total, in centimetres. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Ground")
	float TravelledCm = 0.0f;

protected:
	virtual void BeginPlay() override;

	/** Drives `TravelStep` in play. Only ever enabled on a patch that travels. */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Called only when a life span runs out, which is what makes it useful.
	 *
	 * IT IS THE ONLY WAY TO TELL THE TWO ENDINGS APART. A patch whose life
	 * span expires and a patch destroyed by something else both arrive at
	 * `EndPlay` with the reason `Destroyed`, so the reason cannot separate
	 * them. `AActor::LifeSpanExpired` is virtual and the engine calls it from
	 * the life span timer and from nowhere else, so overriding it records the
	 * one case where the drawings should be left to finish.
	 */
	virtual void LifeSpanExpired() override;

	/**
	 * End the drawings unless this patch finished its own life. Issue #1660.
	 *
	 * THE RULE IS "EXPIRED, OR ANYTHING ELSE", NOT "FLOOR CHANGE OR EXPIRY",
	 * AND THAT IS DELIBERATE. A floor change is the case that was reported, but
	 * a patch cut short for any other reason has the same problem: its owner
	 * dies, a level is torn down, something calls Destroy. The recorded reason
	 * for leaving drawings detached is about a timed effect finishing its own
	 * life and says nothing about one cut short. A patch that disappears
	 * leaving fire burning where it was is the bug in every one of those cases.
	 *
	 * So do not add a second condition narrowing this to floor changes. There
	 * is one case where the drawings are left alone and it is the expiry above.
	 */
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/**
	 * An empty root so the actor has a position at all.
	 *
	 * NOT DECORATION. An actor whose components are all non-scene components has
	 * no root component, and an actor with no root component reports its
	 * location as the world origin however it was spawned. This class had no
	 * components at all, so every patch of burning ground in the project swept
	 * around (0,0,0) rather than around where the skill left it. It carries no
	 * collision: a zone finds who is standing in it by overlapping the world, so
	 * nothing ever needs to overlap the zone itself.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Ground Zone")
	TObjectPtr<USceneComponent> Anchor;

private:
	/**
	 * Ask to be drawn again, so a floor-lasting patch does not go invisible.
	 *
	 * ONLY A FLOOR-LASTING PATCH RUNS THIS. A timed one is drawn once, for its
	 * whole life span, exactly as before.
	 */
	void Redraw();

	/** A timed patch's lifetime is SetLifeSpan; the sweep needs its own timer. */
	FTimerHandle SweepTimer;

	/**
	 * The drawings this patch asked for, so it can end them if it is cut short.
	 *
	 * WEAK, BECAUSE THIS PATCH DOES NOT OWN THEM. The engine outers each
	 * component to the world settings actor and each destroys itself when its
	 * time is up, so one being gone already is the ordinary case rather than an
	 * error -- and a raw pointer would dangle every time a drawing finished on
	 * its own schedule.
	 *
	 * NOT A `UPROPERTY`, because Unreal Header Tool refuses a container of weak
	 * pointers as a Blueprint type and a weak pointer needs no garbage
	 * collection tracking of its own. `ACataclysmTerrain::InsideLastSweep`
	 * carries the same note for the same reason.
	 *
	 * EMPTY IN EVERY AUTOMATION TEST. Niagara makes no component when
	 * `FApp::CanEverRender()` is false and the run passes -nullrhi, so what a
	 * test sees is the decision to end them rather than the ending.
	 */
	TArray<TWeakObjectPtr<UNiagaraComponent>> Drawings;

	/** Whether a life span ran out, rather than something else ending it. */
	bool bExpiredNaturally = false;

	/** Only a floor-lasting patch uses this. See `Redraw`. */
	FTimerHandle RedrawTimer;
};
