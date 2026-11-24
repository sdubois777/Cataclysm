// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CataclysmBuriedWeapon.generated.h"

class UStaticMeshComponent;

/**
 * A weapon left in a creature, which tears free when it dies and buries itself
 * in the next one.
 *
 * WHAT ASKS FOR IT. The Axe's Harrower: "bury a burning axe in an enemy up to 10
 * meters away, setting them alight. The axe stays where it lands. When that
 * enemy dies it tears free and buries itself in the nearest living enemy within
 * 10 meters, and it goes on doing so until nothing is left in reach." Its row
 * writes `OnDeath=Leap` and `OnDeathRange=10`, and nothing read either.
 *
 * A SEPARATE COMPONENT FROM `UCataclysmCurseSpread`, WHICH IT SITS BESIDE.
 * Both are set off by the same death and both reach for the nearest enemy, and
 * there the resemblance stops: that one copies whatever tags its host happens to
 * carry and applies no damage, while this one carries a damage figure, the
 * firing skill's own tags and its identity, and deals a blow. Folding them into
 * one component with a mode would put two payloads on one object and make every
 * reader check which was in use.
 *
 * IT MOVES ITSELF ON RATHER THAN BEING RESPAWNED. When the axe leaps, it marks
 * its new host with the same numbers, which is what "goes on doing so until
 * nothing is left in reach" means. It stops when nothing is in range, and there
 * is nothing else to stop it -- the row states no limit and neither does this.
 *
 * ONE PER ACTOR, REFRESHED RATHER THAN ADDED TO, the same shape as the
 * one-stack rule the design states for ailments and damage over time.
 *
 * WHAT ISSUE #2872 CHANGED. The owner, playtesting on 2026-11-24: "I don't
 * think the axe skill, Harrower, is actually working." The leap was built and
 * ran in play -- the log's "HIT OUT: ranged attack" lines with Harrower's own
 * sent figure are its blows, named so because a leap carries no skill object
 * (ruled 2026-10-22, #2465) -- but three things hid it or cut it short:
 *
 * - ITS ONLY LINE WAS VERBOSE, which the log never prints. Each burial, each
 *   leap and each stop now says so at Display, under `LogPrefix`.
 * - NOTHING WAS DRAWN. The axe is now drawn in the creature it is buried in,
 *   when the weapon's mesh loads (`DrawnWeapon`), and leaves the body it tears
 *   out of.
 * - A LEAP THAT KILLED ITS NEW HOST ENDED THE CHAIN. The new host's death was
 *   recorded inside the leap's own blow, before the axe was in it, so nothing
 *   tore free and the axe stayed in a corpse. With Harrower's blow against an
 *   ordinary creature that was most leaps. Now the axe tears free of a host its
 *   own leap killed and goes on, "until nothing is left in reach".
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmBuriedWeapon : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Leave a weapon in this creature, or refresh the one already in it.
	 *
	 * @param Target          the creature it is buried in
	 * @param Caster          who threw it, credited with what it deals
	 * @param RangeCm         how far it reaches for its next host
	 * @param InDamagePercent what each blow deals, as percent of weapon damage
	 * @param InSkillTags     the firing skill's tags, so its blows scale and are
	 *                        mitigated the way that skill's blows should be
	 * @param bInBurns        whether it sets what it strikes alight
	 * @param InCastBy        the actor the throwing use was cast from, when its caller named one: a skill gadget,
	 *                        carried into every later blow. Ruled 2026-10-16, skill gadgets stack 2 (P5). A WEAK
	 *                        POINTER KEPT AS IT CAME, so a skill trap already used up is still named by its identity
	 *                        (ruled 2026-10-21, #2462): its `Get` answers null, and the identity is what the trap's
	 *                        player finds its used-up trap by.
	 * @param InSkillRow      the throwing skill's row, captured while it was there, or None. Issue #2764, slice 2a:
	 *                        every leap and its burn carry it, so they scale by that skill's level and credit it
	 * @return the component, or null if the creature cannot take one
	 */
	static UCataclysmBuriedWeapon* BuryIn(AActor* Target, AActor* Caster,
										  float RangeCm, float InDamagePercent,
										  const FGameplayTagContainer& InSkillTags,
										  bool bInBurns,
										  const TWeakObjectPtr<AActor>& InCastBy = TWeakObjectPtr<AActor>(),
										  FName InSkillRow = NAME_None);

	/**
	 * Tear free of a dying creature and bury in the nearest living enemy.
	 *
	 * CALLED FROM `UCataclysmSkillEffects::MarkDead`, the one place a death is
	 * recorded for the player and every creature, so an axe comes free whether
	 * its host was killed by a blow, by a burn or by a patch of burning ground.
	 *
	 * IT RUNS BEFORE THE DEATH IS RECORDED, so the dying creature is still an
	 * ordinary target to the search and is excluded by name instead.
	 *
	 * AND IT GOES ON FROM A HOST ITS OWN BLOW KILLED. Issue #2872. That host's
	 * death is recorded inside the blow, before the axe is in it, so it tears
	 * nothing free itself; this goes on from it at once, to the nearest living
	 * enemy within reach of it, until a host survives the blow or nothing is in
	 * reach. Every creature whose death is tearing this axe free at the moment is
	 * left out of every search, by name, because none of them is recorded dead
	 * yet; and a death already tearing an axe free is not torn again, which is
	 * what stops a second axe in a host this one killed leaping back into a
	 * creature still dying further up the same chain.
	 *
	 * WHERE IT STOPS, it stays: in the last body it reached, drawn there, and
	 * destroyed with it. A leap takes it out of the body it left.
	 *
	 * @return whether it found a new host
	 */
	static bool LeapFromDying(AActor* Dying);

	/**
	 * What every line this writes to the log begins with, at Display. Issue #2872:
	 * "BURIED WEAPON: Harrower's weapon tore free of ... and struck ...". One line
	 * when it is buried by a throw, one for each leap, and one where it stops.
	 */
	static const TCHAR* const LogPrefix;

	/**
	 * The item base whose mesh draws a weapon thrown by a skill with these tags:
	 * `Weapon_` and the last part of its `Item.Weapon.` tag, so Harrower's
	 * `Item.Weapon.Axe` is `Weapon_Axe`, the row `DT_WeaponMeshes` draws the Axe
	 * in the hand with. None when no such tag is held. Issue #2872.
	 */
	static FName WeaponBaseFor(const FGameplayTagContainer& InSkillTags);

	/**
	 * Draw the weapon in the creature it is buried in, its blade toward the
	 * creature's middle from `CameFrom`, where the throw or the leap came from.
	 * Issue #2872.
	 *
	 * ONLY WHEN THE MESH LOADS: `UCataclysmWeaponMeshes::MeshFor` the base
	 * `WeaponBaseFor` names, which is third-party art absent from every worktree
	 * (`game/docs/weapon-source-assets.md`). Without it nothing is drawn and the
	 * axe works the same. NO COLLISION, so the cursor, a shot and a body pass it.
	 * A second burial in the same creature moves the drawn weapon rather than
	 * drawing another.
	 *
	 * @return whether a weapon is drawn
	 */
	bool DrawIn(const FVector& CameFrom);

	/** The weapon drawn in the creature, or null when no mesh loaded. Read by tests. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DrawnWeapon;

	/**
	 * The name of the skill that threw it, captured when it was thrown, for the
	 * log lines. Issue #2872. Empty for a weapon buried by anything but a skill.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	FString SkillName;

	/** How far it reaches for its next host, in centimetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	float RangeCm = 0.0f;

	/** What each blow deals, as a percent of the caster's weapon damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	float DamagePercent = 0.0f;

	/** True when it sets what it strikes alight. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	bool bBurns = false;

	/** The firing skill's own tags, carried so its blows are scaled as its own. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	FGameplayTagContainer SkillTags;

	/** Who threw it. Credited with everything it deals afterwards. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	TWeakObjectPtr<AActor> Caster;

	/** How many creatures it has moved through. Read by tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Buried Weapon")
	int32 Leaps = 0;

	/**
	 * The actor the throwing use was cast from, or empty. A SKILL GADGET'S AXE IS THE GADGET'S WHILE THE GADGET STANDS:
	 * each leap's blow carries it as `FCataclysmHitDelivery::CastBy`, so it is priced as the gadget's long after the
	 * use ended; once the gadget is destroyed it is the player's. Ruled 2026-10-16, skill gadgets stack 2 (P5). A
	 * SKILL TRAP'S AXE IS THE TRAP'S AT EVERY LEAP, the trap being used up as its cast starts: this keeps the trap's
	 * identity, handed on as it is at each leap and never through `Get`, and the trap's player keeps the used-up trap
	 * (`UCataclysmAbilitySystemComponent::SpentSkillTrapNamed`). Ruled 2026-10-21, #2462.
	 */
	UPROPERTY()
	TWeakObjectPtr<AActor> CastBy;

	/**
	 * The throwing skill's row, captured when it was thrown, or None. Issue #2764, slice 2a. Each leap's blow carries it
	 * (`FCataclysmHitDelivery::SkillRowWithNoSkill`), and so does the leap's burn, so a leap is priced at that skill's
	 * level -- its figure is the skill's own percentage, which the design document's row "What a level does" multiplies
	 * -- and credits that skill with what it takes from a creature's health. The skill object is not kept: the leap
	 * names the skill's tags only, as ruled 2026-10-22 (#2465, N1, N2), and the row is all the level needs.
	 */
	UPROPERTY()
	FName SkillRow;
};
