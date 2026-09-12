// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "CataclysmCombatEvents.generated.h"

class ACataclysmCharacterBase;
class UCataclysmAbilitySystemComponent;
struct FGameplayEffectModCallbackData;

/**
 * One blow, as the character it landed on resolved it. Issue #41, slice 4.
 *
 * BUILT ONCE AND HANDED OUT BY REFERENCE, AND VALID ONLY WHILE IT IS BEING
 * HANDED OUT. The tag containers point at the effect's and the skill's own
 * tags rather than holding copies, because a Horde wave on fire resolves
 * hundreds of ticks a second and a copy each would be hundreds of allocations
 * nobody asked for. A listener that wants to keep anything copies it.
 *
 * A PLAIN STRUCT AND NOT A USTRUCT, for the same reason: nothing reflects it,
 * nothing saves it and every listener is C++.
 */
struct CATACLYSM_API FCataclysmHitNotice
{
	/**
	 * The character the blow is credited to, which is the effect's instigator.
	 * A minion's blow is credited to its summoner. Null for damage with no
	 * instigator.
	 */
	AActor* Attacker = nullptr;

	/**
	 * The actor that dealt it. A minion for a minion's blow, which is recorded as
	 * the effect context's source object; otherwise the effect's causer, which
	 * for every other blow in the game is the same actor as `Attacker`.
	 */
	AActor* DealtBy = nullptr;

	/** The character it landed on. */
	AActor* Target = nullptr;

	/**
	 * The skill that dealt it, or None when no skill did: a creature's attack,
	 * a minion's blow, retaliation. Read from the skill INSTANCE the effect
	 * context carries; see `FCataclysmHitDelivery::Skill`.
	 */
	FName SkillName;

	/**
	 * That skill's own tags, its whole set. Null when no skill dealt the blow.
	 * Points at the skill's own container, which outlives the notice.
	 */
	const FGameplayTagContainer* SkillTags = nullptr;

	/**
	 * What reached the target after every mitigation step: mana, energy shield
	 * and health together. Zero for a blow that was evaded or fully mitigated.
	 */
	float Landed = 0.0f;

	/** The part of `Landed` that reached health. */
	float DealtToHealth = 0.0f;

	bool bCritical = false;
	bool bBlocked = false;
	bool bEvaded = false;
	bool bDamageOverTime = false;
	bool bArea = false;

	/**
	 * Whether the blow was a melee attack, a ranged attack or a spell, and
	 * whether a boss dealt it, copied from `FCataclysmIncomingHit`, which issue
	 * #666 fills from the effect's tags and its causer. A spell that fires a
	 * projectile is both a spell and ranged. A damage-over-time tick is none of
	 * the first three.
	 */
	bool bIsMelee = false;
	bool bIsRanged = false;
	bool bIsSpell = false;
	bool bFromBoss = false;

	/**
	 * Metres from `DealtBy` to `Target` when the blow landed, or -1 when either is
	 * missing. The number the enchantment and Demonic tree sessions call the
	 * opponent's distance, which is the same from either side of the blow.
	 */
	float DistanceMetres = -1.0f;

	/** Where the target stood. */
	FVector Location = FVector::ZeroVector;

	/**
	 * The tags the effect carried to the target. NOT THE SKILL'S WHOLE TAG SET:
	 * only what `UCataclysmSkillEffects::ApplyTypedSpec` puts on a damage effect
	 * reaches this side -- the element, area, damage over time, melee and the
	 * minion's exclusions. The skill's whole set is `SkillTags`. Null when the
	 * notice is not for an effect.
	 */
	const FGameplayTagContainer* EffectTags = nullptr;

	/**
	 * The tags the effect grants the target while it runs. For a damage-over-time
	 * tick this is where its ailment is -- `Keyword.DoT.Bleed` rather than the
	 * bare `Keyword.DoT` above -- because `ApplyDamageOverTime` grants the
	 * ailment's tag rather than carrying it. Null for anything but a tick.
	 */
	const FGameplayTagContainer* GrantedTags = nullptr;

	/** Whether either container carries this tag, or one below it. */
	bool HasTag(const FGameplayTag& Tag) const;
};

/**
 * One death, with who is credited with it. Issue #41, slice 4.
 *
 * VALID ONLY WHILE IT IS BEING HANDED OUT, like the hit notice above.
 */
struct CATACLYSM_API FCataclysmDeathNotice
{
	AActor* Victim = nullptr;

	/**
	 * The character the last blow on record was credited to, or null when there
	 * is none. A damage-over-time tick is a blow here, so a bleed kill names the
	 * character who applied the bleed.
	 */
	AActor* Killer = nullptr;

	/**
	 * The actor that dealt the last blow on record. A minion for a minion's kill
	 * -- which credits its SUMMONER as `Killer`, under today's placeholder minion
	 * model, issue #340 -- and otherwise the same actor as `Killer`.
	 */
	AActor* KillingCauser = nullptr;

	/**
	 * The skill that dealt the last blow on record, or None when no skill did
	 * or when that skill has since been taken away.
	 */
	FName KillingSkillName;

	/** That skill's own tags, or null. */
	const FGameplayTagContainer* KillingSkillTags = nullptr;

	bool bByDamageOverTime = false;

	/** The same four facts, for the last blow on record. See `FCataclysmHitNotice`. */
	bool bIsMelee = false;
	bool bIsRanged = false;
	bool bIsSpell = false;
	bool bFromBoss = false;

	/**
	 * Seconds between the last blow on record and the death, or -1 when there is
	 * none. A death that did not come from a blow -- health written directly, a
	 * console command -- still names the last attacker, and this is how a
	 * listener tells the two apart.
	 */
	float SecondsSinceLastBlow = -1.0f;

	FVector Location = FVector::ZeroVector;

	/**
	 * The killing blow's tags, what it carried and what it granted together.
	 * Empty when the last blow on record did not bring health to zero, which is
	 * a death that came from somewhere else. Never null.
	 */
	const FGameplayTagContainer* KillingTags = nullptr;
};

/**
 * One skill used. Issue #41, slice 4, with the names the enchantment session
 * proposed on 2026-09-11.
 *
 * SENT WHEN THE SKILL IS PAID FOR, NOT WHEN IT IS PRESSED. A skill the cost or
 * the cooldown refuses sends nothing.
 */
struct CATACLYSM_API FCataclysmSkillUsedNotice
{
	AActor* User = nullptr;
	FName SkillName;

	/**
	 * The skill's own tags. Null for a creature's ability, which carries none:
	 * `FCataclysmEnemyAbility` has a name and a shape and no tag container.
	 */
	const FGameplayTagContainer* SkillTags = nullptr;

	/** The slot it sits in. None for a creature's ability. */
	ECataclysmAbilitySlot Slot = ECataclysmAbilitySlot::None;

	FVector Location = FVector::ZeroVector;
};

/**
 * The last blow a character took, kept on its ability system so a death can
 * say who dealt it.
 *
 * WHY IT HAS TO BE KEPT AT ALL. `UCataclysmSkillEffects::MarkDead` is where every
 * death is recorded, and it is handed the victim and nothing else. The blow that
 * killed it resolved a moment earlier in `UCataclysmVitalAttributeSet`, which is
 * the one place that knows who struck.
 *
 * THE TAGS ARE COPIED ONLY FOR A BLOW THAT BRINGS HEALTH TO ZERO. Every other
 * blow writes the cheap fields and leaves the tags empty, so an ordinary
 * hit costs no allocation here.
 */
struct CATACLYSM_API FCataclysmLastBlow
{
	TWeakObjectPtr<AActor> Attacker;
	TWeakObjectPtr<AActor> DealtBy;
	/** The skill that dealt it, if one did. Weak, so it keeps no skill alive. */
	TWeakObjectPtr<const UGameplayAbility> Skill;
	bool bDamageOverTime = false;
	bool bIsMelee = false;
	bool bIsRanged = false;
	bool bIsSpell = false;
	bool bFromBoss = false;

	/** World time of the blow. Negative means no blow is on record. */
	double WorldSeconds = -1.0;

	/** See the struct's comment. Empty unless the blow was lethal. */
	FGameplayTagContainer KillingTags;

	bool IsOnRecord() const { return WorldSeconds >= 0.0; }
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCataclysmOnHit, const FCataclysmHitNotice&);
DECLARE_MULTICAST_DELEGATE_OneParam(FCataclysmOnDeath, const FCataclysmDeathNotice&);
DECLARE_MULTICAST_DELEGATE_OneParam(FCataclysmOnSkillUsed, const FCataclysmSkillUsedNotice&);

/**
 * The one place a hit, a death or a skill used is announced. Issue #41, slice 4.
 *
 * WHAT WAS HERE BEFORE IT. No hit or death notice existed. The game's multicast
 * delegates were for a projectile finishing, the stairs being taken, a choice
 * button and the equipment changing, and this sits beside them and replaces
 * none. Every reaction to a hit was a direct call written into the attribute set
 * -- leech, retaliation, the Living Pyre's return, Infernal Brand, Beguiling --
 * and those stay where they are. This is for listeners that are not part of the
 * damage pipeline: dungeon modifiers, enchantment triggers and the Demonic
 * trees' kill payouts.
 *
 * A WORLD SUBSYSTEM RATHER THAN A COMPONENT ON EACH CHARACTER, which the
 * coordinating session approved on 2026-09-11. A floor rule that listens for
 * every creature's death binds once here rather than to every creature as it
 * spawns, and a listener for one character compares the notice's actors.
 * `UCataclysmTargetCandidates` is the precedent for the shape.
 *
 * NOTHING IS BUILT WHEN NOTHING LISTENS. Every sender asks whether its delegate
 * is bound before building a notice, so a world with no listeners pays one
 * subsystem lookup per hit and nothing else.
 */
UCLASS()
class CATACLYSM_API UCataclysmCombatEvents : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** This world's announcer, or null in a world that has no subsystems. */
	static UCataclysmCombatEvents* In(const UWorld* World);

	FCataclysmOnHit OnHit;
	FCataclysmOnDeath OnDeath;
	FCataclysmOnSkillUsed OnSkillUsed;

	/**
	 * Announces a blow that has just resolved, and keeps it as the victim's last
	 * blow. Called by `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`
	 * once the damage result is known, for evaded and blocked blows as well.
	 *
	 * @param EffectTags  the effect's asset tags, which the attribute set has
	 *                    already gathered once. Passed in so they are not
	 *                    gathered a second time on every blow
	 * @param bLethal  whether this blow brings the target's health to zero, which
	 *                 is the one case the last-blow record copies the tags for
	 */
	static void NoteBlow(const FGameplayEffectModCallbackData& Data,
						 const FCataclysmIncomingHit& Hit,
						 const FCataclysmDamageResult& Outcome,
						 const FGameplayTagContainer& EffectTags, bool bLethal);

	/**
	 * Announces a death. Called by `UCataclysmSkillEffects::MarkDead`, the one
	 * place a death is recorded, after the victim is marked dead -- so a listener
	 * already sees it as a corpse, and `MarkDead` refusing a second time is what
	 * makes this fire once a death.
	 */
	static void NoteDeath(AActor* Victim);

	/**
	 * Announces a skill paid for. Called by
	 * `UCataclysmSkillTemplate::CommitAndBegin` once `CommitAbility` succeeds,
	 * which every one of the eight skill shapes and the basic attack pass through.
	 */
	static void NoteSkillUsed(AActor* User, const FString& SkillName,
							  const FGameplayTagContainer& SkillTags,
							  ECataclysmAbilitySlot Slot);

	/**
	 * Announces a creature starting one of its abilities. Called by
	 * `ACataclysmEnemyController` straight after each of its two
	 * `UseEnemyAbility` calls, because no creature ability goes through
	 * `CommitAndBegin`.
	 */
	static void NoteCreatureAbility(ACataclysmCharacterBase* Creature,
									int32 AbilityIndex);

	/** How many of each have been sent in this world. Read by tests. */
	uint32 HitsSent() const { return Hits; }
	uint32 DeathsSent() const { return Deaths; }
	uint32 SkillUsesSent() const { return SkillUses; }

private:
	uint32 Hits = 0;
	uint32 Deaths = 0;
	uint32 SkillUses = 0;
};
