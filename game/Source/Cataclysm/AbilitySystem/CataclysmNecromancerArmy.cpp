// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmNecromancerArmy.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRisingCurse.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Cataclysm.h"
#include "Components/PrimitiveComponent.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

/**
 * Pins every chance this file rolls, 0-100, for tests. -1 rolls for real. A chance succeeds when the roll is below it,
 * so 0 always succeeds where there is any chance and 100 never does.
 */
static TAutoConsoleVariable<float> CVarNecromancerArmyRoll(
	TEXT("Cataclysm.NecromancerArmyRoll"),
	-1.0f,
	TEXT("Pins the Necromancer army's chance rolls, 0-100. -1 rolls normally."),
	ECVF_Default);

const TCHAR* UCataclysmNecromancerArmy::ReforgedHealPercentStat = TEXT("summon_without_room_heals_permanent_percent");
const TCHAR* UCataclysmNecromancerArmy::CritShareOfYoursPercentStat = TEXT("minion_crit_share_of_yours_percent");
const TCHAR* UCataclysmNecromancerArmy::RisesAgainChanceStat = TEXT("minion_rises_again_chance");
const TCHAR* UCataclysmNecromancerArmy::RegenPercentPerSecondStat = TEXT("minion_health_regen_percent_per_second");
const TCHAR* UCataclysmNecromancerArmy::MorePermanentRaisedStat = TEXT("summon_raises_more_permanent_minions");
const TCHAR* UCataclysmNecromancerArmy::RaisesASecondChanceStat = TEXT("permanent_minion_raises_a_second_chance");
const TCHAR* UCataclysmNecromancerArmy::PermanentKillRisesAsHuskChanceStat =
	TEXT("permanent_minion_kill_rises_as_husk_chance");
const TCHAR* UCataclysmNecromancerArmy::DeathRisesAsHuskStat = TEXT("minion_death_rises_as_husk");
const TCHAR* UCataclysmNecromancerArmy::KillRisesAsSkeletonChanceStat = TEXT("kill_rises_as_skeleton_chance");
const TCHAR* UCataclysmNecromancerArmy::MovementSwapsWithMinionStat = TEXT("movement_swaps_with_minion");
const TCHAR* UCataclysmNecromancerArmy::SkillsRaiseHusksSecondsStat =
	TEXT("skills_raise_husks_seconds_after_resource_full");
const TCHAR* UCataclysmNecromancerArmy::DamageSplitWithMinionsStat = TEXT("damage_taken_split_with_minions");
const TCHAR* UCataclysmNecromancerArmy::LoneRevenantHeldStat = TEXT("lone_revenant_held");
const TCHAR* UCataclysmNecromancerArmy::ReserveStat = TEXT("minion_reserve");

namespace
{
	/**
	 * A minion's own critical strike multiplier, before Master's Edge: the game's default for every character, 150%
	 * (`UCataclysmCombatAttributeSet`'s initial `CritMultiplier`, and `Default_crit_multiplier` in ClassStats.csv). Path of
	 * Exile and Last Epoch both give a minion a multiplier of its own on the player's pattern; a minion's critical strike
	 * CHANCE of its own stays nought, "a minion never critically strikes", so with no row nothing changes.
	 */
	constexpr float MinionOwnCritMultiplierPercent = 150.0f;

	const UCataclysmAbilitySystemComponent* ArmySystemOf(const AActor* Actor)
	{
		return Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Actor));
	}

	/** A stat of `Commander`'s, asked with `Tags`. Nought for a commander with no stat line. */
	float CommanderStat(const AActor* Commander, const TCHAR* Stat, const FGameplayTagContainer& Tags)
	{
		const UCataclysmAbilitySystemComponent* Theirs = ArmySystemOf(Commander);
		return Theirs ? Theirs->StatForSkill(FName(Stat), Tags, 0.0f) : 0.0f;
	}

	/** The tags a follower's own figures are asked with: its type's, or a thrall's. */
	FGameplayTagContainer TagsOfFollower(const AActor* Follower)
	{
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		return Minion ? Minion->TypeTags : UCataclysmCommand::ThrallMinionTags();
	}

	bool IsAMachine(const AActor* Follower)
	{
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		return Minion && Minion->IsDeployable();
	}

	/** A follower's maximum health, or nought when it has none. */
	float MaximumHealthOf(const AActor* Follower)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Follower);
		return System && System->HasAttributeSetForAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
			? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute())
			: 0.0f;
	}

	/** Keep `Added` of a thrall's own figure on `Attribute`, re-reading its own when something else rewrote it. */
	void KeepAdded(UAbilitySystemComponent& System, const FGameplayAttribute& Attribute, float Wanted, float& Added,
				   float& Written)
	{
		const float Now = System.GetNumericAttribute(Attribute);
		const float Own = FMath::IsNearlyEqual(Now, Written) ? Now - Added : Now;
		if (FMath::IsNearlyZero(Wanted) && FMath::IsNearlyZero(Added))
		{
			return;
		}
		const float Then = Own + Wanted;
		System.SetNumericAttributeBase(Attribute, Then);
		Added = Wanted;
		Written = System.GetNumericAttribute(Attribute);
	}
}

// ==========================================================================
// The mark
// ==========================================================================

UCataclysmArmyMark* UCataclysmArmyMark::On(AActor* Follower)
{
	if (!IsValid(Follower))
	{
		return nullptr;
	}
	UCataclysmArmyMark* Mark = Follower->FindComponentByClass<UCataclysmArmyMark>();
	if (!Mark)
	{
		Mark = NewObject<UCataclysmArmyMark>(Follower);
		if (Mark)
		{
			Mark->RegisterComponent();
		}
	}
	return Mark;
}

const UCataclysmArmyMark* UCataclysmArmyMark::Of(const AActor* Follower)
{
	return Follower ? Follower->FindComponentByClass<UCataclysmArmyMark>() : nullptr;
}

// ==========================================================================
// What a permanent minion is
// ==========================================================================

FGameplayTag UCataclysmNecromancerArmy::PermanentTag()
{
	return FGameplayTag::RequestGameplayTag(FName(TEXT("Minion.Permanent")), /*ErrorIfNotFound=*/false);
}

void UCataclysmNecromancerArmy::MakePermanent(ACataclysmMinion* Minion)
{
	if (!IsValid(Minion))
	{
		return;
	}

	// "STANDS UNTIL IT IS DESTROYED": the engine's `SetLifeSpan(0)` is "never expires", as a skill gadget's is.
	Minion->SetLifeSpan(0.0f);

	// AND THE TAG EVERY READ OF ITS FIGURES IS ASKED WITH, so a row requiring it reaches this minion's blow, its explosion,
	// the damage it takes, its reserve's kind and its health.
	const FGameplayTag Permanent = PermanentTag();
	if (Permanent.IsValid())
	{
		Minion->TypeTags.AddTag(Permanent);
	}

	// AND ITS HEALTH KEPT LIVE FROM ITS TYPE ROW FROM NOW ON, starting from what `Spawn` gave it. A judgement of this
	// slice: the owner's ruling that minions update live (2026-09-13) is recorded as not built for health, and a minion
	// that stands until killed is where it matters, so a row taken after it rose -- Standing Army's next point, Lone
	// Revenant -- and First Among the Dead, whose minion changes, all reach it.
	const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Minion);
	const FCataclysmMinionTypeRow* Type =
		ACataclysmMinion::FindType(ACataclysmMinion::LoadTypeTable(), Minion->TypeName);
	UCataclysmArmyMark* Mark = UCataclysmArmyMark::On(Minion);
	if (Type && System && Mark)
	{
		float Damage = 0.0f;
		ACataclysmMinion::TypeFiguresAtLevel(*Type, ACataclysmMinion::LevelOfSummonerFor(Minion->Summoner),
											 Mark->TypeHealth, Damage);
		const UCataclysmAbilitySystemComponent* Own = Cast<UCataclysmAbilitySystemComponent>(System);
		Mark->HealthFromRows = MaximumHealthOf(Minion) - (Own ? Own->BoundBonesHealthAdded : 0.0f);
		Mark->bHealthKeptLive = Mark->TypeHealth > 0.0f && Mark->HealthFromRows > 0.0f;
		HealthStep(Minion);
	}
}

bool UCataclysmNecromancerArmy::IsPermanentMinion(const AActor* Follower)
{
	if (!IsValid(Follower) || UCataclysmSkillEffects::IsDead(Follower) || !UCataclysmCommand::CommanderOf(Follower))
	{
		return false;
	}
	const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
	if (!Minion)
	{
		// A THRALL: a taken creature stands until it is killed.
		return true;
	}
	return !Minion->IsDeployable() && Minion->TypeTags.HasTagExact(PermanentTag());
}

FGameplayTagContainer UCataclysmNecromancerArmy::TagsOfARaise(const FGameplayTagContainer& SkillTags,
															   bool bRaisesPermanent)
{
	FGameplayTagContainer Tags = SkillTags;
	const FGameplayTag Permanent = PermanentTag();
	if (bRaisesPermanent && Permanent.IsValid())
	{
		Tags.AddTag(Permanent);
	}
	return Tags;
}

float UCataclysmNecromancerArmy::ReserveMultiplier(const AActor* Commander, const FGameplayTagContainer& Tags)
{
	const UCataclysmAbilitySystemComponent* Theirs = ArmySystemOf(Commander);
	return Theirs ? FMath::Max(0.0f, Theirs->MultiplierForStatAgainst(FName(ReserveStat), Tags, nullptr)) : 1.0f;
}

bool UCataclysmNecromancerArmy::LoneRevenantHeld(const AActor* Commander)
{
	return CommanderStat(Commander, LoneRevenantHeldStat, FGameplayTagContainer()) > 0.0f;
}

void UCataclysmNecromancerArmy::NoteMadeBy(ACataclysmMinion* Minion, const FGameplayTagContainer& SkillTags)
{
	const FGameplayTag Death = FGameplayTag::RequestGameplayTag(FName(TEXT("Element.Death")), /*ErrorIfNotFound=*/false);
	if (Minion && Death.IsValid() && SkillTags.HasTagExact(Death))
	{
		if (UCataclysmArmyMark* Mark = UCataclysmArmyMark::On(Minion))
		{
			Mark->bRaisedFromTheDead = true;
		}
	}
}

bool UCataclysmNecromancerArmy::RoseFromTheDead(const AActor* Follower)
{
	const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
	const UCataclysmArmyMark* Mark = UCataclysmArmyMark::Of(Follower);
	return (Mark && Mark->bRaisedFromTheDead) || (Minion && Minion->RisenAtSeconds >= 0.0f);
}

// ==========================================================================
// The pipeline's state about one minion
// ==========================================================================

void UCataclysmNecromancerArmy::FillAskingMinionState(FCataclysmStatConditions& State, const AActor* Commander,
													  const AActor* AskingMinion)
{
	State.bAskingMinionHasTheMostMaximumHealth = false;
	const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(AskingMinion);
	if (!Body || !Commander)
	{
		return;
	}

	// OLD BONES' "EVERY FULL 30 SECONDS IT HAS STOOD": since it came under command, summoned or taken. Only when nothing
	// has said already (a machine's blow states its own age, `StatNamingTagAppliedTo`).
	const UWorld* World = Body->GetWorld();
	if (State.MinionSecondsActive < 0.0f && World && Body->CommandedSinceSeconds >= 0.0f)
	{
		State.MinionSecondsActive = FMath::Max(0.0f, World->GetTimeSeconds() - Body->CommandedSinceSeconds);
	}

	State.bAskingMinionHasTheMostMaximumHealth = HasTheMostMaximumHealth(Commander, AskingMinion);
}

bool UCataclysmNecromancerArmy::HasTheMostMaximumHealth(const AActor* Commander, const AActor* Follower)
{
	if (!Commander || !IsPermanentMinion(Follower))
	{
		return false;
	}
	const AActor* Best = nullptr;
	float BestHealth = -1.0f;
	float BestSince = 0.0f;
	int64 BestOrder = 0;
	for (const AActor* Each : UCataclysmCommand::ThingsCommandedBy(Commander))
	{
		if (!IsPermanentMinion(Each))
		{
			continue;
		}
		const float Health = MaximumHealthOf(Each);
		const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Each);
		const float Since = Body && Body->CommandedSinceSeconds >= 0.0f ? Body->CommandedSinceSeconds : TNumericLimits<float>::Max();
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Each);
		const int64 Order = Minion ? Minion->SpawnOrder : TNumericLimits<int64>::Max();
		// THE MOST MAXIMUM HEALTH; A TIE TO THE ONE COMMANDED LONGEST, THEN THE EARLIER MADE. A judgement: nearly equal
		// figures are a tie, so two Barrow Guards raised together do not trade the bonus back and forth.
		const bool bMore = Health > BestHealth + KINDA_SMALL_NUMBER;
		const bool bTie = FMath::IsNearlyEqual(Health, BestHealth, 0.01f);
		if (!Best || bMore || (bTie && (Since < BestSince || (Since == BestSince && Order < BestOrder))))
		{
			Best = Each;
			BestHealth = Health;
			BestSince = Since;
			BestOrder = Order;
		}
	}
	return Best == Follower;
}

int32 UCataclysmNecromancerArmy::KindsBeyondTheFirst(const TArray<AActor*>& Commanded)
{
	// EVERY TYPE OF MINION IS ITS OWN KIND: each type row, and every thrall together as one kind, the sentence's own
	// list. A placed machine is a minion type too, as `minions_held` counts it, so a multiclass Saboteur's gadget is a
	// kind: a judgement, since the sentence calls this the multiclass node. A minion with no type row is "untyped".
	TSet<FString> Kinds;
	for (const AActor* Each : Commanded)
	{
		if (!IsValid(Each) || UCataclysmSkillEffects::IsDead(Each))
		{
			continue;
		}
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Each);
		Kinds.Add(Minion ? (Minion->TypeName.IsEmpty() ? FString(TEXT("untyped")) : Minion->TypeName.ToLower())
						 : FString(TEXT("thrall")));
	}
	return FMath::Max(0, Kinds.Num() - 1);
}

// ==========================================================================
// What time does
// ==========================================================================

void UCataclysmNecromancerArmy::Step(AActor* Character, float SecondsInStep)
{
	const AActor* Commander = UCataclysmCommand::CommanderOf(Character);
	if (!Commander || IsAMachine(Character) || UCataclysmSkillEffects::IsDead(Character))
	{
		return;
	}

	HealthStep(Character);

	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Character);
	if (!System || !System->HasAttributeSetForAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()))
	{
		return;
	}
	const FGameplayTagContainer Tags = TagsOfFollower(Character);

	// MENDING DEAD: "regenerate 1% of their maximum health per second", asked with the follower's own tags so the row's
	// `Minion.Permanent` reaches only a permanent minion. Written straight onto health, as a thrall's heal at its take is.
	const float Percent = CommanderStat(Commander, RegenPercentPerSecondStat, Tags);
	if (Percent > 0.0f && SecondsInStep > 0.0f)
	{
		const float Maximum = System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
		const float Health = System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
		if (Health > 0.0f && Health < Maximum)
		{
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(),
											FMath::Min(Maximum, Health + Maximum * Percent / 100.0f * SecondsInStep));
		}
	}

	// MASTER'S EDGE ON A THRALL, whose blows read its own critical strike: the share of its commander's added to its
	// own figures and kept there, re-read when something else rewrote them (a raised minion's blow rolls its own in
	// `CriticalFactorForBlow`).
	if (!Character->IsA<ACataclysmMinion>()
		&& System->HasAttributeSetForAttribute(UCataclysmCombatAttributeSet::GetCritChanceAttribute()))
	{
		const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Commander);
		const float Share = CommanderStat(Commander, CritShareOfYoursPercentStat, Tags);
		UCataclysmArmyMark* Mark = UCataclysmArmyMark::On(Character);
		if (Theirs && Mark && (Share > 0.0f || !FMath::IsNearlyZero(Mark->CritChanceAdded)
							   || !FMath::IsNearlyZero(Mark->CritMultiplierAdded))
			&& Theirs->HasAttributeSetForAttribute(UCataclysmCombatAttributeSet::GetCritChanceAttribute()))
		{
			const float Fraction = FMath::Max(0.0f, Share) / 100.0f;
			KeepAdded(*System, UCataclysmCombatAttributeSet::GetCritChanceAttribute(),
					  Theirs->GetNumericAttribute(UCataclysmCombatAttributeSet::GetCritChanceAttribute()) * Fraction,
					  Mark->CritChanceAdded, Mark->CritChanceWritten);
			KeepAdded(*System, UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(),
					  Theirs->GetNumericAttribute(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute()) * Fraction,
					  Mark->CritMultiplierAdded, Mark->CritMultiplierWritten);
		}
	}
}

void UCataclysmNecromancerArmy::HealthStep(AActor* Character)
{
	ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Character);
	UCataclysmArmyMark* Mark = Minion ? Minion->FindComponentByClass<UCataclysmArmyMark>() : nullptr;
	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Minion);
	if (!Mark || !Mark->bHealthKeptLive || !System || UCataclysmSkillEffects::IsDead(Minion)
		|| !IsValid(Minion->Summoner))
	{
		return;
	}

	// ITS TYPE'S HEALTH TIMES ITS SUMMONER'S `minion_health` NOW, asked with its own tags and as the minion asking, so
	// First Among the Dead's condition is judged for it.
	const float Wanted = Mark->TypeHealth
		* UCataclysmCommand::SummonerMultiplierFor(Minion->Summoner, TEXT("minion_health"), Minion->TypeTags, Minion);
	const float Change = Wanted - Mark->HealthFromRows;
	if (Wanted <= 0.0f || FMath::IsNearlyZero(Change, 0.01f))
	{
		return;
	}

	// ONLY THE DIFFERENCE IS WRITTEN, as Bound Bones writes its own, so what another rule added stays: a rise is added to
	// the maximum and to health, and a fall comes off the maximum with health held at the new maximum.
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float NewMaximum = FMath::Max(1.0f, System->GetNumericAttribute(MaxHealth) + Change);
	Mark->HealthFromRows = Wanted;
	System->SetNumericAttributeBase(MaxHealth, NewMaximum);
	const float NewHealth = Change > 0.0f
		? System->GetNumericAttribute(Health) + Change
		: FMath::Min(System->GetNumericAttribute(Health), NewMaximum);
	System->SetNumericAttributeBase(Health, FMath::Clamp(NewHealth, 1.0f, NewMaximum));
}

void UCataclysmNecromancerArmy::CriticalFiguresFor(const ACataclysmMinion* Minion, float& OutChance,
												   float& OutMultiplier)
{
	OutChance = 0.0f;
	OutMultiplier = MinionOwnCritMultiplierPercent;
	if (!Minion || !IsValid(Minion->Summoner) || Minion->IsDeployable())
	{
		return;
	}
	const float Share = CommanderStat(Minion->Summoner, CritShareOfYoursPercentStat, Minion->TypeTags);
	const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Minion->Summoner);
	if (Share <= 0.0f || !Theirs
		|| !Theirs->HasAttributeSetForAttribute(UCataclysmCombatAttributeSet::GetCritChanceAttribute()))
	{
		return;
	}
	OutChance = FMath::Clamp(
		Theirs->GetNumericAttribute(UCataclysmCombatAttributeSet::GetCritChanceAttribute()) * Share / 100.0f, 0.0f, 100.0f);
	OutMultiplier += Theirs->GetNumericAttribute(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute()) * Share / 100.0f;
}

float UCataclysmNecromancerArmy::CriticalFactorForBlow(const ACataclysmMinion* Minion, bool* bOutCritical)
{
	if (bOutCritical)
	{
		*bOutCritical = false;
	}
	float Chance = 0.0f;
	float Multiplier = MinionOwnCritMultiplierPercent;
	CriticalFiguresFor(Minion, Chance, Multiplier);
	if (Chance <= 0.0f || Roll() >= Chance)
	{
		return 1.0f;
	}
	if (bOutCritical)
	{
		*bOutCritical = true;
	}
	UE_LOG(LogCataclysm, Verbose, TEXT("%s struck critically for %.0f%%, at a chance of %.1f%% (Master's Edge)."),
		   *GetNameSafe(Minion), Multiplier, Chance);
	return Multiplier / 100.0f;
}

// ==========================================================================
// Summoning
// ==========================================================================

AActor* UCataclysmNecromancerArmy::MostWoundedPermanentMinion(const AActor* Commander)
{
	AActor* Worst = nullptr;
	float WorstShare = 1.0f;
	for (AActor* Each : UCataclysmCommand::ThingsCommandedBy(Commander))
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Each);
		const float Maximum = MaximumHealthOf(Each);
		if (!IsPermanentMinion(Each) || !System || Maximum <= 0.0f)
		{
			continue;
		}
		// THE LARGEST SHARE OF ITS MAXIMUM MISSING, a judgement: "most wounded" asks how hurt it is, and a share treats a
		// Barrow Guard and a Bone Reaper alike. One at full health is not wounded and is never chosen.
		const float Share = System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) / Maximum;
		if (Share < WorstShare)
		{
			Worst = Each;
			WorstShare = Share;
		}
	}
	return Worst;
}

int32 UCataclysmNecromancerArmy::AfterASummon(AActor* Commander, UCataclysmSummonSkill* Skill, int32 Raised,
											  bool bRefusedForRoom)
{
	if (!IsValid(Commander) || !Skill || !Skill->Params.bPermanent || Skill->Params.bPossess)
	{
		return 0;
	}
	const FGameplayTagContainer Tags = TagsOfARaise(Skill->SkillTags, /*bRaisesPermanent=*/true);

	// BONE LORD: "raise 2 more permanent minions each time they raise any, if your Fervour has room for them". Each
	// through the press's own `SummonOne`, which asks the pool for room and refuses with nothing made.
	int32 More = 0;
	if (Raised > 0)
	{
		const int32 Extra = MorePermanentToRaise(Commander, Skill);
		for (int32 Index = 0; Index < Extra; ++Index)
		{
			if (Skill->SummonOne())
			{
				++More;
			}
		}
		if (More > 0)
		{
			UE_LOG(LogCataclysm, Log, TEXT("BONE LORD: '%s' raised %d more permanent minions for %s."),
				   *Skill->SkillName, More, *GetNameSafe(Commander));
		}
		return More;
	}

	// REFORGED: "Using a summon skill when your Fervour has no room for another permanent minion restores 10% per point
	// of the health of your most wounded permanent minion, rather than raising nothing." 10% of its maximum health.
	const float Percent = CommanderStat(Commander, ReforgedHealPercentStat, Tags);
	if (bRefusedForRoom && Percent > 0.0f)
	{
		if (AActor* Wounded = MostWoundedPermanentMinion(Commander))
		{
			UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Wounded);
			const float Maximum = MaximumHealthOf(Wounded);
			const float Health = System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute());
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(),
											FMath::Min(Maximum, Health + Maximum * Percent / 100.0f));
			UE_LOG(LogCataclysm, Log, TEXT("REFORGED: '%s' had no room and restored %.0f%% of %s's health instead."),
				   *Skill->SkillName, Percent, *GetNameSafe(Wounded));
		}
	}
	return 0;
}

float UCataclysmNecromancerArmy::ChanceToRaiseASecond(const AActor* Commander, const UCataclysmSummonSkill* Skill)
{
	if (!Commander || !Skill || !Skill->Params.bPermanent || Skill->Params.bPossess)
	{
		return 0.0f;
	}
	return CommanderStat(Commander, RaisesASecondChanceStat, TagsOfARaise(Skill->SkillTags, /*bRaisesPermanent=*/true));
}

ACataclysmMinion* UCataclysmNecromancerArmy::RollToRaiseASecond(AActor* Commander, UCataclysmSummonSkill* Skill,
																 const ACataclysmMinion* Raised)
{
	const float Chance = ChanceToRaiseASecond(Commander, Skill);
	if (!IsValid(Raised) || Chance <= 0.0f || Roll() >= Chance)
	{
		return nullptr;
	}
	// BESIDE IT, a metre to its side, a judgement; and through the replacement path, which asks the pool for room ("if
	// your Fervour has room for it") and is not a press, so the second does not roll again.
	const FVector Beside = Raised->GetActorLocation() + Raised->GetActorRightVector() * SecondRisesBesideCm;
	ACataclysmMinion* Second = Skill->SummonReplacementAt(Beside);
	if (Second)
	{
		UE_LOG(LogCataclysm, Log, TEXT("RISE TOGETHER: '%s' raised a second %s beside %s for %s."), *Skill->SkillName,
			   *Second->TypeName, *GetNameSafe(Raised), *GetNameSafe(Commander));
	}
	return Second;
}

int32 UCataclysmNecromancerArmy::MorePermanentToRaise(const AActor* Commander, const UCataclysmSummonSkill* Skill)
{
	if (!Commander || !Skill || !Skill->Params.bPermanent || Skill->Params.bPossess)
	{
		return 0;
	}
	const float More = CommanderStat(Commander, MorePermanentRaisedStat,
									 TagsOfARaise(Skill->SkillTags, /*bRaisesPermanent=*/true));
	return FMath::Max(0, FMath::FloorToInt32(More + 0.001f));
}

// ==========================================================================
// Deaths
// ==========================================================================

ACataclysmMinion* UCataclysmNecromancerArmy::RaiseHusk(AActor* Commander, const FVector& Where, float HealthShare,
													   float DamageShare, bool bTaunts, const TCHAR* Why)
{
	if (!IsValid(Commander) || UCataclysmSkillEffects::IsDead(Commander))
	{
		return nullptr;
	}

	// SHALLOW GRAVES' HUSK, EVERY FIGURE OF IT, as Death Rattle's is.
	FCataclysmWeaponSkill Row;
	if (!UCataclysmRisingCurse::FindTheRowThatRaises(TEXT("Death"), TEXT("Husk"), Row))
	{
		UE_LOG(LogCataclysm, Warning, TEXT("%s: no Death Summon row raises a Husk with a Duration, so nothing rose."), Why);
		return nullptr;
	}

	// THE POOL MUST HOLD ITS RESERVE (issue #2787): "if your Fervour has room for it".
	const float Reserve = UCataclysmCommand::ReserveFor(Commander, Row.Params.FervourReserve, Row.Tags);
	if (!UCataclysmCommand::HasRoomToReserve(Commander, Reserve))
	{
		UE_LOG(LogCataclysm, Verbose, TEXT("%s: %s's pool has no room for a husk reserving %.0f Fervour."), Why,
			   *GetNameSafe(Commander), Reserve);
		return nullptr;
	}

	ACataclysmMinion* Risen =
		ACataclysmMinion::Spawn(Commander, Where, Row.Params.Duration, Row.Params.bBurns, TEXT("Husk"));
	if (!Risen)
	{
		return nullptr;
	}

	Risen->RecordExplosionRadius(Row.Params.RadiusCm);
	if (const UWorld* World = Risen->GetWorld())
	{
		Risen->RisenAtSeconds = World->GetTimeSeconds();
	}
	if (UCataclysmArmyMark* Mark = UCataclysmArmyMark::On(Risen))
	{
		Mark->bRaisedFromTheDead = true;
	}

	// ITS SHARE OF A HUSK'S HEALTH AND BLOW, the maximum first and then the current, as `Spawn` writes them.
	if (UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Risen))
	{
		if (HealthShare > 0.0f && !FMath::IsNearlyEqual(HealthShare, 1.0f))
		{
			const float Health = FMath::Max(1.0f, MaximumHealthOf(Risen) * HealthShare);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Health);
			System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
		}
	}
	if (DamageShare >= 0.0f)
	{
		Risen->OwnDamagePerHit *= DamageShare;
	}

	UCataclysmCommand::HoldReserve(Commander, Risen, Reserve);
	if (bTaunts && Row.Params.bTaunts)
	{
		Risen->BeginTaunting(Row.Params.RadiusCm, ACataclysmMinion::RowTauntEverySeconds);
	}
	UE_LOG(LogCataclysm, Display, TEXT("%s: a husk rose for %s, lasting %.1f s and reserving %.0f Fervour."), Why,
		   *GetNameSafe(Commander), Row.Params.Duration, Reserve);
	return Risen;
}

int32 UCataclysmNecromancerArmy::FollowerDied(AActor* Follower, AActor* Commander)
{
	if (!IsValid(Follower) || !IsValid(Commander) || IsAMachine(Follower))
	{
		return NothingRose;
	}
	const FVector Where = Follower->GetActorLocation();
	const FGameplayTagContainer Tags = TagsOfFollower(Follower);

	// SECOND DEATH: "When one of your minions that was not raised from the dead dies, such as a thrall, an imp or a minion
	// an item gives you, it rises where it fell as a husk that lasts 8 seconds, as large as it was." AS LARGE AS IT WAS
	// is its maximum health and its blow, a judgement: a husk is a body, and its body is the one that fell. A husk is
	// raised from the dead, so one that rose this way does not rise again.
	if (!RoseFromTheDead(Follower)
		&& CommanderStat(Commander, DeathRisesAsHuskStat, FGameplayTagContainer()) > 0.0f)
	{
		const float Health = MaximumHealthOf(Follower);
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Follower);
		const float Blow = Minion
			? Minion->OwnDamagePerHit
			: (Its && Its->HasAttributeSetForAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute())
				   ? Its->GetNumericAttribute(UCataclysmCombatAttributeSet::GetAttackDamageAttribute())
				   : 0.0f);
		if (ACataclysmMinion* Husk = RaiseHusk(Commander, Where, 1.0f, -1.0f, /*bTaunts=*/true, TEXT("SECOND DEATH")))
		{
			UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Husk);
			if (System && Health > 0.0f)
			{
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Health);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), Health);
			}
			if (Blow > 0.0f)
			{
				Husk->OwnDamagePerHit = Blow;
			}
			Husk->SetActorScale3D(Follower->GetActorScale3D());
			return RoseAsAHusk;
		}
		return NothingRose;
	}

	// WILL NOT STAY DOWN: "Each of your permanent minions that dies has a 4% chance per point to rise again where it fell 3
	// seconds later, if your Fervour has room for it." A RAISED ONE, made again by the skill that made it, which asks the
	// pool then; a thrall is a creature that was taken rather than raised, and is not made again, a judgement.
	const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
	if (!Minion || !Minion->TypeTags.HasTagExact(PermanentTag()) || !Minion->SummonedBy.IsValid())
	{
		return NothingRose;
	}
	const float Chance = CommanderStat(Commander, RisesAgainChanceStat, Tags);
	if (Chance <= 0.0f || Roll() >= Chance)
	{
		return NothingRose;
	}
	UWorld* World = Follower->GetWorld();
	if (!World)
	{
		return NothingRose;
	}
	const TWeakObjectPtr<UCataclysmSummonSkill> Skill = Minion->SummonedBy;
	const TWeakObjectPtr<AActor> WeakCommander(Commander);
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Skill, WeakCommander, Where]()
	{
		AActor* Raiser = WeakCommander.Get();
		if (Skill.IsValid() && IsValid(Raiser) && !UCataclysmSkillEffects::IsDead(Raiser))
		{
			if (ACataclysmMinion* Again = Skill->SummonReplacementAt(Where))
			{
				UE_LOG(LogCataclysm, Log, TEXT("WILL NOT STAY DOWN: %s rose again for %s."), *GetNameSafe(Again),
					   *GetNameSafe(Raiser));
			}
		}
	}), RisesAgainAfterSeconds, /*bLoop=*/false);
	return RisesAgainSoon;
}

ACataclysmMinion* UCataclysmNecromancerArmy::EnemyDied(AActor* Dying)
{
	const UCataclysmAbilitySystemComponent* Its = ArmySystemOf(Dying);
	if (!Its || !Its->GetLastBlow().IsOnRecord() || UCataclysmCommand::CommanderOf(Dying))
	{
		// No blow on record, or the dying one is somebody's follower and no enemy.
		return nullptr;
	}

	// WHO KILLED IT: the follower that dealt the last blow, or the attacker it is credited to.
	const FCataclysmLastBlow& Last = Its->GetLastBlow();
	AActor* Killer = Last.DealtBy.IsValid() ? Last.DealtBy.Get() : Last.Attacker.Get();
	if (!IsValid(Killer))
	{
		return nullptr;
	}
	AActor* Commander = UCataclysmCommand::CommanderOf(Killer);
	const bool bByAFollower = Commander != nullptr;
	if (!Commander)
	{
		Commander = Killer;
	}
	// THE SIDES COMPARED DIRECTLY, as `CataclysmKillFervour.cpp` and `CataclysmRisenImps.cpp` compare them: the enemy is
	// already marked dead when `HandleDeath` calls here, and `UCataclysmTargeting::IsHostileTo` answers false for any dead
	// actor, so this returned before every roll. Window dfb-w1 found it (Harvest of the Host, The Restless Dead).
	if (UCataclysmTeams::AttitudeBetween(Commander, Dying) != ETeamAttitude::Hostile)
	{
		return nullptr;
	}
	const FVector Where = Dying->GetActorLocation();

	// CARRION HOST AND HARVEST OF THE HOST: an enemy a permanent minion kills rises as a husk, at the row's chance.
	if (bByAFollower && IsPermanentMinion(Killer))
	{
		const float Chance = CommanderStat(Commander, PermanentKillRisesAsHuskChanceStat, FGameplayTagContainer());
		if (Chance > 0.0f && Roll() < Chance)
		{
			if (ACataclysmMinion* Husk = RaiseHusk(Commander, Where, 1.0f, 1.0f, /*bTaunts=*/true, TEXT("CARRION HOST")))
			{
				return Husk;
			}
		}
	}

	// THE RESTLESS DEAD: "Enemies you or your minions kill have a chance to rise on their own as weak skeletons for 8
	// seconds, each with a quarter of a husk's health and damage. Each reserves 3 Fervour." A HUSK'S BODY AT A QUARTER, A
	// JUDGEMENT: there is no skeleton type row, and the husk is the figure the sentence measures it by. It does not taunt:
	// turning enemies on itself is the husk's own rule.
	const float Chance = CommanderStat(Commander, KillRisesAsSkeletonChanceStat, FGameplayTagContainer());
	if (Chance > 0.0f && Roll() < Chance)
	{
		return RaiseHusk(Commander, Where, SkeletonShareOfAHusk, SkeletonShareOfAHusk, /*bTaunts=*/false,
						 TEXT("THE RESTLESS DEAD"));
	}
	return nullptr;
}

ACataclysmMinion* UCataclysmNecromancerArmy::RaiseLichHusk(AActor* Player, const FVector& Aim, bool bBasicAttack)
{
	const UCataclysmAbilitySystemComponent* Theirs = ArmySystemOf(Player);
	if (bBasicAttack || !Theirs)
	{
		return nullptr;
	}
	const float Seconds = Theirs->StatForSkill(FName(SkillsRaiseHusksSecondsStat), FGameplayTagContainer(), 0.0f);
	const float Since = Theirs->SecondsSinceClassResourceFull();
	if (Seconds <= 0.0f || Since < 0.0f || Since > Seconds)
	{
		return nullptr;
	}
	// WHERE IT LANDS: the point the skill was aimed at, a judgement; at the player's feet for a skill aimed nowhere.
	const FVector Where = Aim.IsNearlyZero() ? Player->GetActorLocation() : Aim;
	return RaiseHusk(Player, Where, 1.0f, 1.0f, /*bTaunts=*/true, TEXT("LICH FORM"));
}

// ==========================================================================
// Grave Swap
// ==========================================================================

bool UCataclysmNecromancerArmy::ChooseGraveSwapPartner(AActor* Self, const FVector& Aim, float RangeCm,
													   AActor*& OutPartner, FVector& OutSelfTo, FVector& OutPartnerTo)
{
	OutPartner = nullptr;
	UWorld* World = Self ? Self->GetWorld() : nullptr;
	UPrimitiveComponent* SelfBody = Self ? Cast<UPrimitiveComponent>(Self->GetRootComponent()) : nullptr;
	if (!World || !SelfBody || CommanderStat(Self, MovementSwapsWithMinionStat, FGameplayTagContainer()) <= 0.0f)
	{
		return false;
	}

	// THE MINIONS NEAREST THE AIMED POINT FIRST, within the skill's own range and in sight, as Vesselstep's are.
	TArray<AActor*> Candidates;
	for (AActor* Each : UCataclysmCommand::ThingsCommandedInSightOf(Self, RangeCm))
	{
		if (!IsAMachine(Each) && FVector::Dist2D(Each->GetActorLocation(), Aim) <= GraveSwapAimToleranceCm)
		{
			Candidates.Add(Each);
		}
	}
	Candidates.Sort([&Aim](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), Aim) < FVector::DistSquared2D(B.GetActorLocation(), Aim);
	});

	const FVector Here = Self->GetActorLocation();
	for (AActor* Partner : Candidates)
	{
		UPrimitiveComponent* PartnerBody = Cast<UPrimitiveComponent>(Partner->GetRootComponent());
		if (!PartnerBody)
		{
			continue;
		}
		// EACH ONE'S FEET WHERE THE OTHER'S WERE, as Vesselstep's trade (`UCataclysmMovementSkill::ChooseTradePartner`).
		const FVector There = Partner->GetActorLocation();
		const double SelfHalf = SelfBody->GetCollisionShape().GetExtent().Z;
		const double PartnerHalf = PartnerBody->GetCollisionShape().GetExtent().Z;
		FVector SelfTo(There.X, There.Y, There.Z - PartnerHalf + SelfHalf);
		FVector PartnerTo(Here.X, Here.Y, Here.Z - SelfHalf + PartnerHalf);

		const bool bSelfIgnoredIt = SelfBody->GetMoveIgnoreActors().Contains(Partner);
		const bool bItIgnoredSelf = PartnerBody->GetMoveIgnoreActors().Contains(Self);
		SelfBody->IgnoreActorWhenMoving(Partner, true);
		PartnerBody->IgnoreActorWhenMoving(Self, true);
		const bool bBothFit = World->FindTeleportSpot(Self, SelfTo, Self->GetActorRotation())
			&& World->FindTeleportSpot(Partner, PartnerTo, Partner->GetActorRotation());
		if (!bSelfIgnoredIt)
		{
			SelfBody->IgnoreActorWhenMoving(Partner, false);
		}
		if (!bItIgnoredSelf)
		{
			PartnerBody->IgnoreActorWhenMoving(Self, false);
		}
		if (bBothFit)
		{
			OutPartner = Partner;
			OutSelfTo = SelfTo;
			OutPartnerTo = PartnerTo;
			return true;
		}
	}
	return false;
}

// ==========================================================================
// Shared Flesh
// ==========================================================================

float UCataclysmNecromancerArmy::SplitWithMinions(const UAbilitySystemComponent* Defender, AActor* Attacker,
												  float Damage, bool bIsDamageOverTime)
{
	const UCataclysmAbilitySystemComponent* Theirs = Cast<UCataclysmAbilitySystemComponent>(Defender);
	AActor* Self = Theirs ? Theirs->GetAvatarActor() : nullptr;
	// A HIT AND NEVER A TICK, a judgement on the genre's shape: Path of Exile 2's Romira's Requital and Diablo III's Life
	// Link share hits only, and a tick split among an army would be dozens of blows a second.
	if (!Self || !Attacker || Damage <= 0.0f || bIsDamageOverTime
		|| Theirs->StatForSkill(FName(DamageSplitWithMinionsStat), FGameplayTagContainer(), 0.0f) <= 0.0f)
	{
		return Damage;
	}

	// EVERY LIVING MINION, WHEREVER IT STANDS, AND NO PLACED MACHINE: "your minions", with no distance, as Bound Bones
	// counts them. A thrall is one (ruled 2026-09-16).
	TArray<AActor*> Sharing;
	for (AActor* Each : UCataclysmCommand::ThingsCommandedBy(Self))
	{
		if (!IsAMachine(Each) && Each != Attacker)
		{
			Sharing.Add(Each);
		}
	}
	if (Sharing.IsEmpty())
	{
		return Damage;
	}

	// SPLIT EVENLY, the defender one part among them. Each minion's part is sent from the attacker and meets its own
	// defences, as Interpose's whole hit does; it cannot be evaded, since the hit already reached its defender, and is
	// not retaliated against. A minion can die of its part; what it could not take is not passed back.
	const float Part = Damage / static_cast<float>(Sharing.Num() + 1);
	FCataclysmHitDelivery Taken;
	Taken.bCannotBeEvaded = true;
	Taken.bCannotBeRetaliatedAgainst = true;
	for (AActor* Each : Sharing)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Each, Part, Taken);
	}
	return Part;
}

// ==========================================================================
// Chance
// ==========================================================================

float UCataclysmNecromancerArmy::Roll()
{
	const float Pinned = CVarNecromancerArmyRoll.GetValueOnAnyThread();
	return Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
}
