// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/Function.h"
#include "CataclysmRemains.generated.h"

class ACataclysmMinion;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;
struct FCataclysmDeathNotice;

/**
 * One ailment a creature died carrying, as its remains keep it: Festering counts them and Plague Pit passes them on.
 * The Death finish's slice D (2026-11-26).
 */
struct FCataclysmRemainsAilment
{
	/** Which of `UCataclysmAilments::Kinds` it is, by its `Ailment` name: "Bleed", "Cripple". */
	FString Ailment;

	/** The tag it granted the creature. */
	FGameplayTag Tag;

	/** The seconds it had left when the creature died. */
	float SecondsLeft = 0.0f;

	/** What it dealt a second, for a damage over time ailment; nought for one that deals no damage. */
	float DamagePerSecond = 0.0f;

	/** Whether that figure is a share of current health a second, as a Void Splinter's is. */
	bool bShareOfHealth = false;
};

/**
 * REMAINS: a small marker left where a creature died, which only the player it was left for sees and uses. The Death
 * finish's slice D (2026-11-26), on the design the coordinating session approved that day, for the Necromancer's
 * Ossuary limb, Bone Colossus and Many Made One's vow.
 *
 * WHAT LEAVES ONE. Bone Fuse: "Dead creatures leave remains: a small marker where each one fell, lasting 10 seconds,
 * which only you see." For every player holding `RemainsLeftStat` above nought (Bone Fuse, or the Bone Colossus vow),
 * each hostile creature's death leaves one, whoever killed it (`UCataclysmRemains::HearDeath`). And Fallen Again: a
 * minion or thrall of the player's killed leaves one at its chance (`UCataclysmRemains::LeaveForAFollower`). The owner
 * asked for no bodies: "it shouldn't be the actual corpse. Maybe a pile of meat or a gravestone or something for less
 * clutter" (2026-11-24).
 *
 * WHAT IT RECORDS: the dead creature's maximum health, its blow, its rarity rung, its kind, and every ailment it died
 * carrying with the seconds each had left. Read when it is left; nothing about the creature is read again.
 *
 * HOW LONG IT LASTS: `BaseSeconds`, longer by Gathered Remains (`RemainsDurationStat`, increased), as the engine's
 * lifespan. `UCataclysmFloorContents::ClearTheFloor` removes every one with the floor, as it removes a Soul.
 *
 * WHAT IS SEEN: a low bone-white disc on the floor where the creature stood, seen by its owner only
 * (`UPrimitiveComponent::SetOnlyOwnerSee`), and the tag "Remains" drawn over it by `ACataclysmHUD` for the player whose
 * remains they are. NOTHING COLLIDES; nothing ticks.
 *
 * NOT SAVED, as a Soul is not.
 */
UCLASS()
class CATACLYSM_API ACataclysmRemains : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmRemains();

	/** How long remains last with nothing lengthening them: Bone Fuse's "lasting 10 seconds". */
	static constexpr float BaseSeconds = 10.0f;

	/** The words on its tag. */
	static const TCHAR* NameText;

	/** The one colour its tag and its disc are drawn in: bone white, which no rarity of gear or of enemy uses. */
	static FLinearColor NameColour();

	/** How far above where it lies its tag is drawn, as a Soul's is. */
	static constexpr float NameHeightCm = 40.0f;

	/** The disc's width and height, in centimetres: small, for little clutter. */
	static constexpr float DiscWidthCm = 50.0f;
	static constexpr float DiscHeightCm = 4.0f;

	/** The player it was left for: the only one who sees it, raises on it, detonates it or feeds it. */
	TWeakObjectPtr<AActor> LeftFor;

	/** The dead creature's maximum health when it died. Mantle of Bone's detonation and the Bone Colossus read it. */
	float MaximumHealth = 0.0f;

	/** The dead creature's own blow, as a creature's attack damage or a minion's damage a hit. */
	float DamagePerHit = 0.0f;

	/** Its rarity rung; -1 for a minion or anything that is not a creature of the enemy class. */
	int32 RarityStep = -1;

	/** What it was: a creature's archetype row, or a minion's type. */
	FName Kind;

	/** Every ailment it died carrying, with the seconds each had left. */
	TArray<FCataclysmRemainsAilment> Ailments;

	/** The world's time when it was left. */
	float LeftAtSeconds = 0.0f;

	/** The disc on the floor. */
	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Remains")
	TObjectPtr<UStaticMeshComponent> Disc;

	/** Engine content found by path, as `ACataclysmTelegraphMarker` finds its own, so nothing is added to the project. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> DiscMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DiscMaterial;

	/** Paint the disc its colour and sit it on the floor `FloorBelowCm` under the actor. */
	void ShowDisc(float FloorBelowCm);
};

/**
 * Everything remains do, read by the stats the Ossuary's rows write. The Death finish's slice D (2026-11-26).
 */
UCLASS()
class CATACLYSM_API UCataclysmRemains : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The flag that makes deaths leave remains for a player: Bone Fuse, and the Bone Colossus vow. Flat. */
	static const TCHAR* RemainsLeftStat;
	/** Gathered Remains: how much longer they last. Increased. */
	static const TCHAR* RemainsDurationStat;
	/** Gravefinder: metres from where a minion rises within which it is raised on remains. Flat. */
	static const TCHAR* RaiseReachMetresStat;
	/** Bone-Bound: the per cent less Fervour a minion raised on remains reserves. Flat. */
	static const TCHAR* ReservePercentLessStat;
	/** Two From One: the per cent chance a raise does not consume the remains. Flat. */
	static const TCHAR* KeptOnRaiseChanceStat;
	/** Mantle of Bone: whether weapon skills landing on remains detonate them. Flat. */
	static const TCHAR* DetonateStat;
	/** Mantle of Bone: the per cent of the dead creature's maximum health a detonation deals. Flat. */
	static const TCHAR* DetonationPercentStat;
	/** Festering: the per cent increased detonation damage for each ailment the creature died with. Flat. */
	static const TCHAR* IncreasedPerAilmentStat;
	/** Plague Pit: whether a detonation passes the creature's ailments on. Flat. */
	static const TCHAR* PassAilmentsStat;
	/** Fallen Again: the per cent chance a follower killed leaves remains. Flat. */
	static const TCHAR* FollowerLeavesChanceStat;

	/** The tag a minion raised on remains carries, which Raised on Bones' rows require. */
	static const TCHAR* RaisedOnRemainsTagName;
	static FGameplayTag RaisedOnRemainsTag();

	/** How far a detonation reaches from the remains: Mantle of Bone's "within 3 metres". */
	static constexpr float DetonationRadiusCm = 300.0f;

	/**
	 * A roll a test pins, in per cent, or below nought to roll. Read by Two From One and Fallen Again: each succeeds
	 * when the roll is below its chance.
	 */
	static float PinnedRollForTests;

	/** A stat off `Player` with no tags; nought for anything without a Cataclysm ability system. */
	static float StatOf(const AActor* Player, const TCHAR* Stat);

	/**
	 * Leave remains of `Dead` for `Player` at `Where`, recording what `Dead` was. Nothing for a player missing or holding
	 * `RemainsLeftStat` at nought, unless `bEvenWithoutTheFlag`, which Fallen Again passes (its own chance decides).
	 *
	 * @return the remains left, or null
	 */
	static ACataclysmRemains* LeaveFor(AActor* Player, AActor* Dead, const FVector& Where, bool bEvenWithoutTheFlag = false);

	/**
	 * A death `Player`'s character heard: remains for a hostile creature's death when the player holds the flag, then
	 * the Bone Colossus and the Abomination (`UCataclysmBoneGiants`). The one call `ACataclysmPlayerCharacter::
	 * OnSomethingDied` makes.
	 */
	static void HearDeath(AActor* Player, const FCataclysmDeathNotice& Notice);

	/**
	 * Fallen Again: "Each of your minions that dies without being detonated has a 6% chance per point to leave
	 * remains." Called by the commander's death hook (`UCataclysmVitalAttributeSet::NotifyIfHealthReachedZero`) before
	 * the follower's death is handled, so for exactly the deaths that hook answers; a detonation is an explosion and not
	 * a death there, so it never reaches this. A machine leaves none: it is placed and not raised, as
	 * `UCataclysmSkillTemplate::DetonateOwnMinionsWhere` reads a minion.
	 */
	static ACataclysmRemains* LeaveForAFollower(AActor* Commander, AActor* Follower, const FVector& Where);

	/** Every remains of `Player`'s, nearest to `Place` first, within `RadiusCm` along the floor. */
	static TArray<ACataclysmRemains*> OwnRemainsNear(const AActor* Player, const FVector& Place, float RadiusCm);

	/** How far each of `Player`'s own remains within `WithinCm` lies from it, in metres along the floor. */
	static void DistancesOfOwnRemains(const AActor* Player, float WithinCm, TArray<float>& OutMetres);

	/** The remains whose tags `Viewer`'s HUD draws: its own, within `Range` of `Standing`, nearest first. */
	static void RemainsToName(const AActor* Viewer, const FVector& Standing, float Range,
							  TArray<ACataclysmRemains*>& Out);

	/**
	 * Gravefinder: the remains a minion `Summoner`'s summon skill raises at `RisePoint` is raised on, the nearest of
	 * its own within `RaiseReachMetresStat` metres; null with none, or with no Gravefinder.
	 */
	static ACataclysmRemains* ChooseToRaiseOn(const AActor* Summoner, const FVector& RisePoint);

	/** Bone-Bound: `Reserve` less `ReservePercentLessStat` per cent, never below nought. */
	static float ReserveOnRemains(const AActor* Summoner, float Reserve);

	/**
	 * Raise `Minion` on `Remains`: it carries `RaisedOnRemainsTag` (and its health is taken again with Raised on Bones'
	 * rows), `minion_raised_on_remains` is raised on its summoner naming it, and the remains are consumed unless Two
	 * From One's roll keeps them, which raises `remains_consumed`. Called by `UCataclysmSummonSkill::SummonOne`.
	 *
	 * @return whether the remains were consumed
	 */
	static bool RaiseOn(AActor* Summoner, ACataclysmMinion* Minion, ACataclysmRemains* Remains);

	/**
	 * Mantle of Bone, at a weapon skill's landing: every remains of `Caster`'s that `IsInside` accepts is detonated, when
	 * the caster holds `DetonateStat`. Called at the top of `UCataclysmSkillTemplate::DetonateOwnMinionsWhere`, which
	 * every weapon skill's landing reaches, before its own `Detonates=1` rule. GATHERED FIRST AND DETONATED AFTER, as the
	 * minions are.
	 *
	 * @return how many were detonated
	 */
	static int32 DetonateWhere(AActor* Caster, TFunctionRef<bool(const AActor&)> IsInside);

	/**
	 * Detonate one remains of `Caster`'s: `DetonationPercentStat` of the creature's maximum health, increased by
	 * Festering for each ailment it died with, to every enemy within `DetonationRadiusCm`, as one blow of the caster's
	 * through `UCataclysmSkillEffects::ExplodeForOverkill` (area damage, unscaled by the caster's increases, the shape
	 * The Ferryman's burst takes); then, for each enemy struck, `remains_detonation_hit` naming it (Grave Spores) and,
	 * with Plague Pit, each ailment passed on for the seconds it had left; then `remains_consumed`, and it is gone.
	 * Nothing for remains that are not the caster's. AT NOUGHT PER CENT (Mantle of Bone's first point) IT IS STILL
	 * DETONATED AND CONSUMED, and strikes nobody.
	 *
	 * @return how many enemies a blow was sent at
	 */
	static int32 Detonate(AActor* Caster, ACataclysmRemains* Remains);

	/** What a detonation of `Remains` by `Caster` deals each enemy, before the enemy's defences. */
	static float DetonationDamage(const AActor* Caster, const ACataclysmRemains* Remains);

	/** Consume `Remains` for `Player`: `remains_consumed` raised on the player, and the remains destroyed. */
	static void Consume(AActor* Player, ACataclysmRemains* Remains);

	/** A roll in per cent: `PinnedRollForTests` when pinned, otherwise drawn. */
	static float Roll();
};
