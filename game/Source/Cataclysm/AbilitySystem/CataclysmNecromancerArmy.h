// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmNecromancerArmy.generated.h"

class ACataclysmMinion;
class UAbilitySystemComponent;
class UCataclysmSummonSkill;
struct FCataclysmStatConditions;

/**
 * What one follower carries for the Necromancer's army nodes: the figures its live health is kept from, whether it rose
 * from the dead, and what Master's Edge last wrote on a thrall's own critical strike. The Death line's finishing slice B
 * (2026-11-26). A COMPONENT, added on demand, for the reason `UCataclysmRisingCurse` gives: it is destroyed with its
 * follower, so nothing has to be cleaned up when the follower dies or is released.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmArmyMark : public UActorComponent
{
	GENERATED_BODY()

public:
	/** The mark on `Follower`, made when it has none. Null for a null follower. */
	static UCataclysmArmyMark* On(AActor* Follower);

	/** The mark on `Follower`, or null when it has none. */
	static const UCataclysmArmyMark* Of(const AActor* Follower);

	/**
	 * Whether this follower rose from the dead: made by a Death summon skill, risen from a dying creature, or raised by
	 * one of this file's rules. Second Death raises nothing for one that did.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Necromancer")
	bool bRaisedFromTheDead = false;

	/** Whether its maximum health is kept live from `TypeHealth` (a permanent minion made from a type row). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Necromancer")
	bool bHealthKeptLive = false;

	/** Its type row's health at its summoner's level, before any summoner's row: what the live figure multiplies. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Necromancer")
	float TypeHealth = 0.0f;

	/** The part of its maximum health its summoner's `minion_health` rows give it, as last written. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Necromancer")
	float HealthFromRows = 0.0f;

	/** What Master's Edge last added to a thrall's critical strike chance and multiplier, and what the two then read. */
	float CritChanceAdded = 0.0f;
	float CritMultiplierAdded = 0.0f;
	float CritChanceWritten = -1.0f;
	float CritMultiplierWritten = -1.0f;
};

/**
 * The Necromancer's army: the Legion limb's permanent minions, the spine's husks raised on a kill, and four of its vow
 * options. The Death line's finishing slice B, 2026-11-26.
 *
 * A PERMANENT MINION stands until it is killed (the tree's own glossary, `Necromancer_tree_summary.md`): one a summon
 * row marks `Permanent=1` (Barrow Guard, Exhume's Bone Reaper, Bone Choir's mages), and every thrall. Such a minion
 * carries `Minion.Permanent` among its tags (`MakePermanent`, `UCataclysmCommand::ThrallMinionTags`), so a row
 * requiring the tag reaches it and nothing that lasts seconds. A placed machine is never one.
 *
 * A HUSK THAT LASTS 8 SECONDS is the Greataxe's Shallow Graves' husk, read from its Summon row as Death Rattle's is
 * (`UCataclysmRisingCurse::FindTheRowThatRaises`): its lifetime, its reserve, its taunt and its explosion's reach. Every
 * raise here asks the pool for room first, by the owner's ruling of 2026-11-20 (issue #2787).
 */
UCLASS()
class CATACLYSM_API UCataclysmNecromancerArmy : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------ the stats, each read here and nowhere else

	/** Reforged: the per cent of its maximum health a summon with no room restores to the most wounded permanent minion. */
	static const TCHAR* ReforgedHealPercentStat;
	/** Master's Edge: the per cent of the commander's critical strike chance and multiplier a minion adds to its own. */
	static const TCHAR* CritShareOfYoursPercentStat;
	/** Will Not Stay Down: the chance a dying minion rises again where it fell, asked with its tags. */
	static const TCHAR* RisesAgainChanceStat;
	/** Mending Dead: the per cent of its maximum health a minion regenerates each second, asked with its tags. */
	static const TCHAR* RegenPercentPerSecondStat;
	/** Rise Together: the chance each permanent minion a summon skill raises raises a second one beside it. */
	static const TCHAR* RaisesASecondChanceStat;
	/** Bone Lord: how many more permanent minions a summon skill raises each time it raises any. */
	static const TCHAR* MorePermanentRaisedStat;
	/** Carrion Host and Harvest of the Host: the chance an enemy a permanent minion kills rises as a husk. */
	static const TCHAR* PermanentKillRisesAsHuskChanceStat;
	/** Second Death: a flag; a minion not raised from the dead rises as a husk when it dies. */
	static const TCHAR* DeathRisesAsHuskStat;
	/** The Restless Dead: the chance an enemy you or your minions kill rises as a weak skeleton. */
	static const TCHAR* KillRisesAsSkeletonChanceStat;
	/** Grave Swap: a flag; a movement skill aimed at one of your minions trades places with it. */
	static const TCHAR* MovementSwapsWithMinionStat;
	/** Lich Form: the seconds after the bar fills during which each skill raises a husk where it lands. */
	static const TCHAR* SkillsRaiseHusksSecondsStat;
	/** Shared Flesh: a flag; a hit you take is split evenly between you and your minions. */
	static const TCHAR* DamageSplitWithMinionsStat;
	/** Lone Revenant's flag, which the Abomination (the Final Rite's first option) reads through `LoneRevenantHeld`. */
	static const TCHAR* LoneRevenantHeldStat;
	/** The reserve's "more" bucket: Lone Revenant's "reserve 300% more Fervour". Read by `ReserveMultiplier`. */
	static const TCHAR* ReserveStat;

	// ------------------------------------------------------------------ figures

	/** The Restless Dead's skeleton: a quarter of a husk's health and damage. The sentence's own figure. */
	static constexpr float SkeletonShareOfAHusk = 0.25f;
	/** Will Not Stay Down's "3 seconds later". The sentence's own figure. */
	static constexpr float RisesAgainAfterSeconds = 3.0f;
	/** Rise Together's "beside it": how far to the side of the first the second rises. A judgement, 1 m. */
	static constexpr float SecondRisesBesideCm = 100.0f;
	/** Grave Swap: how near the aimed point a minion must stand to be the one swapped with. A judgement, 2 m. */
	static constexpr float GraveSwapAimToleranceCm = 200.0f;

	// ------------------------------------------------------------------ what a permanent minion is

	/** `Minion.Permanent`, or an invalid tag when the tag table does not have it. */
	static FGameplayTag PermanentTag();

	/**
	 * Make a just-summoned minion permanent: no lifespan, `Minion.Permanent` among its tags, and its maximum health kept
	 * live from its type row from now on (`HealthStep`), so the rows that reach a permanent minion reach its health at
	 * once. Called by `UCataclysmSummonSkill::RecordTheSkillOn` for a `Permanent=1` row.
	 */
	static void MakePermanent(ACataclysmMinion* Minion);

	/** Whether `Follower` is a living permanent minion of somebody's: a minion carrying the tag, or a thrall. */
	static bool IsPermanentMinion(const AActor* Follower);

	/**
	 * The tags a summon row's reserve is asked with: the row's own, with `Minion.Permanent` added when the row makes
	 * permanent minions or takes a thrall. So Lighter Bones' and Lone Revenant's reserve rows reach those and no other.
	 */
	static FGameplayTagContainer TagsOfARaise(const FGameplayTagContainer& SkillTags, bool bRaisesPermanent);

	/** What a reserve is multiplied by for its "more" rows (`ReserveStat`), asked with `Tags`. One with none. */
	static float ReserveMultiplier(const AActor* Commander, const FGameplayTagContainer& Tags);

	/** Whether `Commander` holds Lone Revenant. The one reader of `LoneRevenantHeldStat`. */
	static bool LoneRevenantHeld(const AActor* Commander);

	/** Note that `Minion` rose from the dead when the skill that made it carries `Element.Death`. */
	static void NoteMadeBy(ACataclysmMinion* Minion, const FGameplayTagContainer& SkillTags);

	/** Whether `Follower` rose from the dead (see `UCataclysmArmyMark::bRaisedFromTheDead`, and a risen minion). */
	static bool RoseFromTheDead(const AActor* Follower);

	// ------------------------------------------------------------------ the stat pipeline's state about one minion

	/**
	 * Fill what `State` knows about the minion whose figure is being asked: how long it has stood (`minion_seconds_active`,
	 * for Old Bones) and whether it is its commander's permanent minion with the most maximum health (First Among the
	 * Dead). Nothing for no minion. Called by `UCataclysmAbilitySystemComponent::MultiplierForStatAgainst`.
	 */
	static void FillAskingMinionState(FCataclysmStatConditions& State, const AActor* Commander,
									  const AActor* AskingMinion);

	/**
	 * Whether `Follower` is the permanent minion of `Commander` with the most maximum health. A tie goes to the one
	 * commanded longest, then the earlier made. False for anything that is not a permanent minion.
	 */
	static bool HasTheMostMaximumHealth(const AActor* Commander, const AActor* Follower);

	/** How many kinds of minion `Commanded` holds beyond the first (Motley Host). Each type row is a kind; every thrall is one kind. */
	static int32 KindsBeyondTheFirst(const TArray<AActor*>& Commanded);

	// ------------------------------------------------------------------ what time does

	/**
	 * A follower's quarter-second step: its live maximum health (Standing Army, First Among the Dead, Lone Revenant),
	 * Mending Dead's regeneration, and Master's Edge on a thrall's own critical strike. Called by
	 * `UCataclysmRegeneration`'s step for every character; nothing for one that follows nobody.
	 */
	static void Step(AActor* Character, float SecondsInStep);

	/** The live maximum health half of `Step`. Public for tests. */
	static void HealthStep(AActor* Character);

	/**
	 * Master's Edge on a raised minion's blow: rolls its critical strike and answers what the blow is multiplied by.
	 * One for a minion whose commander holds no such row. Its own chance is nought and its own multiplier the game's
	 * default 150%; the row adds its share of its commander's to each.
	 */
	static float CriticalFactorForBlow(const ACataclysmMinion* Minion, bool* bOutCritical = nullptr);

	/** Master's Edge's figures for `Minion`: the critical strike chance and multiplier, in per cent, it strikes with. */
	static void CriticalFiguresFor(const ACataclysmMinion* Minion, float& OutChance, float& OutMultiplier);

	// ------------------------------------------------------------------ summoning

	/**
	 * After a summon skill's press: Bone Lord's extra permanent minions when it raised any, and Reforged's heal when it
	 * raised none for want of room. Nothing for a row that does not make permanent minions.
	 *
	 * @param Raised  how many the press raised
	 * @param bRefusedForRoom  whether a raise was refused because the pool could not hold its reserve
	 * @return how many more it raised
	 */
	static int32 AfterASummon(AActor* Commander, UCataclysmSummonSkill* Skill, int32 Raised, bool bRefusedForRoom);

	/** Bone Lord's figure: how many more permanent minions `Skill`'s press raises for `Commander` when it raises any. */
	static int32 MorePermanentToRaise(const AActor* Commander, const UCataclysmSummonSkill* Skill);

	/** Rise Together's chance, in per cent, for one permanent minion `Skill` raises for `Commander`; nought otherwise. */
	static float ChanceToRaiseASecond(const AActor* Commander, const UCataclysmSummonSkill* Skill);

	/**
	 * Rise Together: "Each permanent minion you raise has a 4% chance per point to raise a second one beside it, if your
	 * Fervour has room for it." Rolled once for each permanent minion a summon skill's press raises (`SummonOne`, Bone
	 * Lord's extras included). The second rises through `UCataclysmSummonSkill::SummonReplacementAt`, which asks the
	 * pool for room, and does not roll again.
	 *
	 * @return the second, or null
	 */
	static ACataclysmMinion* RollToRaiseASecond(AActor* Commander, UCataclysmSummonSkill* Skill,
												const ACataclysmMinion* Raised);

	/** The permanent minion of `Commander` missing the largest share of its maximum health, or null. */
	static AActor* MostWoundedPermanentMinion(const AActor* Commander);

	// ------------------------------------------------------------------ deaths

	/** What `FollowerDied` did. */
	static constexpr int32 NothingRose = 0;
	static constexpr int32 RoseAsAHusk = 1;
	static constexpr int32 RisesAgainSoon = 2;

	/**
	 * A follower of `Commander` died: Second Death and Will Not Stay Down. Called by `UCataclysmVitalAttributeSet` where
	 * the commander is told one of its minions died, while the body still stands.
	 *
	 * @return `RoseAsAHusk` when Second Death raised one, `RisesAgainSoon` when Will Not Stay Down's return is waiting,
	 *         else `NothingRose`
	 */
	static int32 FollowerDied(AActor* Follower, AActor* Commander);

	/**
	 * An enemy died: Carrion Host and Harvest of the Host for a kill by a permanent minion, and The Restless Dead for a
	 * kill by its commander or any of its followers. At most one rises from a death, a husk before a skeleton. Called by
	 * `ACataclysmEnemyCharacter::HandleDeath`.
	 *
	 * @return what rose, or null
	 */
	static ACataclysmMinion* EnemyDied(AActor* Dying);

	/**
	 * Raise Shallow Graves' husk for `Commander` at `Where`, if its pool can hold the husk's reserve. Its health and blow
	 * are its own times the two shares, and it taunts as the husk does only when `bTaunts`.
	 *
	 * @param Why  for the log
	 * @return the risen husk, or null
	 */
	static ACataclysmMinion* RaiseHusk(AActor* Commander, const FVector& Where, float HealthShare, float DamageShare,
									   bool bTaunts, const TCHAR* Why);

	/**
	 * Lich Form: a skill `Player` uses within the row's seconds of its bar filling raises a husk where it lands, its aimed
	 * point. Called by `UCataclysmAbilitySystemComponent::ActOnSkillUse`; nothing for the basic attack.
	 */
	static ACataclysmMinion* RaiseLichHusk(AActor* Player, const FVector& Aim, bool bBasicAttack);

	// ------------------------------------------------------------------ Grave Swap

	/**
	 * The minion a movement skill aimed at `Aim` trades places with: of `Self`'s followers it can see within `RangeCm`,
	 * not a placed machine, the one standing nearest the aimed point and within `GraveSwapAimToleranceCm` of it, for
	 * which both bodies fit in the other's place. False when `Self` does not hold Grave Swap or no such minion stands.
	 */
	static bool ChooseGraveSwapPartner(AActor* Self, const FVector& Aim, float RangeCm, AActor*& OutPartner,
									   FVector& OutSelfTo, FVector& OutPartnerTo);

	// ------------------------------------------------------------------ Shared Flesh

	/**
	 * Shared Flesh: split a hit `Defender` takes evenly between it and its living minions (not its placed machines).
	 * Each minion is sent its share from `Attacker`, through its own defences; the share the defender keeps is answered.
	 * `Damage` unchanged for a defender without the row, one with no minion, a tick, or no attacker.
	 */
	static float SplitWithMinions(const UAbilitySystemComponent* Defender, AActor* Attacker, float Damage,
								  bool bIsDamageOverTime);

	// ------------------------------------------------------------------ chance

	/** A roll from 0 to 100, or the pinned figure of `Cataclysm.NecromancerArmyRoll` when it is 0 or more. */
	static float Roll();
};
