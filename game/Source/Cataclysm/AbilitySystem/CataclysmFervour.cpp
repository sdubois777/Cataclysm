// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmFervour.h"
#include "Cataclysm.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmDebuffs.h"
// For whether a running buff holds Fervour from decay. Issue #2885, N8.
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmTargetCandidates.h"
#include "AbilitySystemComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

const TCHAR* UCataclysmFervour::FromDamageStat = TEXT("fervour_from_damage");
const TCHAR* UCataclysmFervour::FromCostStat = TEXT("fervour_from_cost");
const TCHAR* UCataclysmFervour::LostToHealingStat =
	TEXT("fervour_lost_to_healing");
const TCHAR* UCataclysmFervour::LossSuppressedStat =
	TEXT("fervour_loss_suppressed");
const TCHAR* UCataclysmFervour::FloorAgainstHealingStat =
	TEXT("fervour_floor_against_healing");
const TCHAR* UCataclysmFervour::PerSecondStat = TEXT("fervour_per_second");
const TCHAR* UCataclysmFervour::PerCastStat = TEXT("fervour_per_cast");
const TCHAR* UCataclysmFervour::OnDroppingLowStat =
	TEXT("fervour_on_dropping_low");
const TCHAR* UCataclysmFervour::FromMinionsStat =
	TEXT("fervour_from_minions");
const TCHAR* UCataclysmFervour::OnMinionDeathStat =
	TEXT("fervour_on_minion_death");
const TCHAR* UCataclysmFervour::PerEnemyInReachStat =
	TEXT("fervour_per_enemy_in_reach");
const TCHAR* UCataclysmFervour::DecayPerSecondStat =
	TEXT("fervour_decay_per_second");
const TCHAR* UCataclysmFervour::DecayGraceMetresStat =
	TEXT("fervour_decay_grace_metres");
const TCHAR* UCataclysmFervour::HealthRestoredOnKillStat =
	TEXT("health_restored_on_kill");
const TCHAR* UCataclysmFervour::IncreasedDamageBoughtPerExtraEnemyHitStat =
	TEXT("increased_damage_bought_per_extra_enemy_hit");
const TCHAR* UCataclysmFervour::PerEnemyHitStat = TEXT("fervour_per_enemy_hit");
const TCHAR* UCataclysmFervour::HealthRestoredOnKillAtNoCostStat =
	TEXT("health_restored_on_kill_at_no_cost");
const TCHAR* UCataclysmFervour::DecayGraceSecondsStat =
	TEXT("fervour_decay_grace_seconds");
const TCHAR* UCataclysmFervour::HealthRestoredOnFullStat =
	TEXT("health_restored_on_fervour_full");
const TCHAR* UCataclysmFervour::OnEnemyDeathNearbyStat =
	TEXT("fervour_on_enemy_death_nearby");
const TCHAR* UCataclysmFervour::PerKillStat = TEXT("fervour_per_kill");
const TCHAR* UCataclysmFervour::KillDecayPerSecondStat = TEXT("fervour_kill_decay_per_second");
const TCHAR* UCataclysmFervour::KillDecayGraceSecondsStat = TEXT("fervour_kill_decay_grace_seconds");
const TCHAR* UCataclysmFervour::HoldRestartedByAnyGainStat = TEXT("fervour_hold_restarted_by_any_gain");
const TCHAR* UCataclysmFervour::HoldExtendedOnGainSecondsStat = TEXT("fervour_hold_extended_on_gain_seconds");
const TCHAR* UCataclysmFervour::DecayFloorPercentStat = TEXT("fervour_decay_floor_percent");
const TCHAR* UCataclysmFervour::HealthPercentLostPerFervourLostStat = TEXT("health_percent_lost_per_fervour_lost");
const TCHAR* UCataclysmFervour::OverflowAttackSpeedSecondsStat = TEXT("fervour_overflow_attack_speed_seconds");
const TCHAR* UCataclysmFervour::KillChainSecondsStat = TEXT("kill_chain_seconds");
const TCHAR* UCataclysmFervour::DecayGivenBackOnKillSecondsStat = TEXT("decay_given_back_on_kill_seconds");
const TCHAR* UCataclysmFervour::ClaimedWithinSecondsStat = TEXT("fervour_claimed_within_seconds");
const TCHAR* UCataclysmFervour::FollowerKillsCountAsYoursStat = TEXT("follower_kills_count_as_yours");
const TCHAR* UCataclysmFervour::PerCriticalStrikeStat = TEXT("fervour_per_critical_strike");
const TCHAR* UCataclysmFervour::BossTicksPayAKillEverySecondsStat = TEXT("boss_ticks_pay_a_kill_every_seconds");
const TCHAR* UCataclysmFervour::BossHitsCountedAsAKillEveryStat = TEXT("boss_hits_counted_as_a_kill_every");

FString UCataclysmFervour::SoulCollectorRowKeyPrefix()
{
	// THE NAME A PASSIVE ROW'S ENCHANTMENT IS GIVEN, FOR A NODE NAMED ONLY BY ITS TREE, so the prefix follows the one
	// place that names passive rows.
	return UCataclysmPassiveTree::EnchantmentNameFor(FName(TEXT("SoulCollector_")), 0);
}

float UCataclysmFervour::GiveBackDecayOnKill(UAbilitySystemComponent* AbilitySystem)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return 0.0f;
	}
	const float Seconds = Cataclysm->StatForSkill(FName(DecayGivenBackOnKillSecondsStat), FGameplayTagContainer(), 0.0f);
	if (Seconds <= 0.0f)
	{
		return 0.0f;
	}
	const float Taken = Cataclysm->TakeFervourDecayedWithin(Seconds);
	if (Taken <= 0.0f)
	{
		return 0.0f;
	}
	// A KILL'S PAYMENT, UNSCALED, as ruled: what decay took comes back as it was taken, and Kindred Fires and Borrowed
	// Time leave the hold to the kill's own stamp.
	Cataclysm->BeginKillPayment();
	ON_SCOPE_EXIT
	{
		Cataclysm->EndKillPayment();
	};
	return RaisePool(AbilitySystem, Taken);
}

float UCataclysmFervour::ClaimOnDeath(UAbilitySystemComponent* AbilitySystem, const AActor* Slain)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || !Slain)
	{
		return 0.0f;
	}
	const float Within = Cataclysm->StatForSkill(FName(ClaimedWithinSecondsStat), FGameplayTagContainer(), 0.0f);
	if (Within <= 0.0f)
	{
		return 0.0f;
	}
	// THE RECORD IS KEPT ON THE STRUCK, keyed by the striker: `NoteStruckBy`. -1 is never struck by this character.
	const UCataclysmAbilitySystemComponent* Theirs =
		Cast<const UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Slain));
	const float Since = Theirs ? Theirs->SecondsSinceStruckBy(AbilitySystem) : -1.0f;
	if (Since < 0.0f || Since > Within)
	{
		return 0.0f;
	}
	return GainOnOwnKill(AbilitySystem, Slain);
}

void UCataclysmFervour::BurstTheSlain(AActor* Killer, const AActor* Slain, const FVector& At, float SlainMaximum,
									  float SharePercent)
{
	if (!Killer || SlainMaximum <= 0.0f || SharePercent <= 0.0f)
	{
		return;
	}
	const TWeakObjectPtr<AActor> Burster(Killer);
	const TWeakObjectPtr<const AActor> Body(Slain);
	TFunction<void()> Burst = [Burster, Body, At, SlainMaximum, SharePercent]()
	{
		if (Burster.IsValid())
		{
			UCataclysmSkillEffects::ExplodeForOverkill(Burster.Get(), Body.Get(), At, SlainMaximum, SharePercent,
													   UCataclysmSkillEffects::SlainBurstRadiusCm);
		}
	};
	if (UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(Killer->GetWorld()))
	{
		Events->AfterThisDeathIsHeard(MoveTemp(Burst));
	}
	else
	{
		Burst();
	}
}

void UCataclysmFervour::MarkNearAKill(UAbilitySystemComponent* Killer, const AActor* Slain, const FVector& At)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(Killer);
	AActor* KillerActor = Cataclysm ? Cataclysm->GetAvatarActor() : nullptr;
	const UWorld* World = KillerActor ? KillerActor->GetWorld() : nullptr;
	const float ReachMetres =
		Cataclysm ? Cataclysm->WidestReachOfCondition(ECataclysmStatCondition::TargetNearYourKillWithinSeconds) : 0.0f;
	if (!World || ReachMetres <= 0.0f)
	{
		return;
	}
	for (AActor* Near : UCataclysmTargeting::FindEnemiesInSphere(World, KillerActor, At, ReachMetres * 100.0f))
	{
		if (Near == Slain)
		{
			continue;
		}
		if (UCataclysmAbilitySystemComponent* Marked =
				Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Near)))
		{
			Marked->NoteNearKillBy(Killer);
		}
	}
}

bool UCataclysmFervour::CreditAFollowersKill(UAbilitySystemComponent* AbilitySystem, AActor* Commander,
											 const FCataclysmDeathNotice& Notice)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || !Commander || !Notice.Killer || !Notice.Victim || Notice.Killer == Commander
		|| UCataclysmCommand::CommanderOf(Notice.Killer) != Commander
		|| Cataclysm->StatForSkill(FName(FollowerKillsCountAsYoursStat), FGameplayTagContainer(), 0.0f) <= 0.0f)
	{
		return false;
	}

	// IN THE ORDER THE CHARACTER'S OWN KILL TAKES: what decay took given back, the payment, then the stamp.
	GiveBackDecayOnKill(AbilitySystem);
	GainOnOwnKill(AbilitySystem, Notice.Victim);
	NoteOwnKill(AbilitySystem, Notice.Victim);
	// AND GRIM WAKE'S MARK on the enemies near the body (the Death trees' slice C4c2), as for an own kill.
	MarkNearAKill(AbilitySystem, Notice.Victim, Notice.Location);
	// AND WEARER OF THE DEAD'S MODIFIERS, as ruled (W5): a follower's kill Death's Toll makes the character's lends them
	// too. The Death trees' slice C4d3 (2026-11-25).
	WearTheModifiersOf(AbilitySystem, Notice.Victim);

	// `kill`, RAISED TO THE SOUL COLLECTOR'S OWN ROWS ONLY (Kill Chain's stacks among them), carrying the slain creature
	// and its maximum health as an own kill's does. Not for a kill a row made, for the reason the own kill gives.
	if (!Notice.bWhileTheKillersRowsActed)
	{
		const UAbilitySystemComponent* Slain = UCataclysmTargeting::AbilitySystemOf(Notice.Victim);
		const float SlainMaximum =
			Slain ? Slain->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : 0.0f;
		const FString Prefix = SoulCollectorRowKeyPrefix();
		Cataclysm->ActOnEvent(FName(TEXT("kill")), Notice.KillingSkillTags, SlainMaximum, /*bLanded=*/true,
							  Notice.Victim, *Prefix);

		// AND THE FERRYMAN'S BURST, which a Soul Collector row on `kill` asked for: this kill is the character's for
		// this tree, so the burst is the character's and its kills are too (the Death trees' slice C4c1). Passed On's
		// explosion is not made: its row asks for the character's own hit, which this kill was not.
		BurstTheSlain(Commander, Notice.Victim, Notice.Location, SlainMaximum,
					  Cataclysm->TakePendingSlainBurstSharePercent());
	}

	// AND LAST BREATH, the Soul Collector's health on a kill at no cost.
	RestoreHealthOnKillAtNoCost(AbilitySystem);
	return true;
}

float UCataclysmFervour::RaisePool(UAbilitySystemComponent* AbilitySystem, float Change)
{
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem ? AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>() : nullptr;
	if (!Resource || Change <= 0.0f)
	{
		return 0.0f;
	}
	// CLAMPED HERE AS WELL AS BY EVERY CALLER, so a change that does not fit never leaves the stored base above the
	// maximum, which `ApplyModToAttribute` would (issue #1036): what does not fit is lost, as a gain to a full bar is.
	const float Before = AbilitySystem->GetNumericAttribute(Pool);
	const float Added = FMath::Min(Change, Resource->MaximumClassResourceAsked() - Before);
	// WHAT DID NOT FIT IS OVERFLOW'S, above the full bar after reserves (the Death trees' slice C6c, 2026-11-25, as
	// ruled): every gain to the pool comes through here, so every gain can overflow.
	if (UCataclysmAbilitySystemComponent* Overflowing = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		const float Overflow = Change - FMath::Max(0.0f, Added);
		const float Window = Overflow > 0.0f
			? Overflowing->StatForSkill(FName(OverflowAttackSpeedSecondsStat), FGameplayTagContainer(), 0.0f)
			: 0.0f;
		if (Window > 0.0f)
		{
			Overflowing->NoteFervourOverflow(Overflow, Window);
		}
	}
	if (Added <= 0.0f)
	{
		return 0.0f;
	}
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Added);
	const float Rise = AbilitySystem->GetNumericAttribute(Pool) - Before;
	if (Rise > 0.0f)
	{
		AfterPoolRose(AbilitySystem, Rise);
	}
	return Rise;
}

float UCataclysmFervour::KillDecayHoldFullSeconds(const UAbilitySystemComponent* AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return 0.0f;
	}
	if (Cataclysm->LastKillGraceSeconds() >= 0.0f)
	{
		return Cataclysm->LastKillGraceSeconds();
	}
	// BEFORE ANY KILL, A COMMON KILL'S GRACE, as ruled: the grace rows are written against the slain creature's rung,
	// and a common creature is rung 0.
	return FMath::Max(0.0f, Cataclysm->StatAsIfAgainstRarity(FName(KillDecayGraceSecondsStat), 0, 0.0f));
}

void UCataclysmFervour::AfterPoolRose(UAbilitySystemComponent* AbilitySystem, float Rise)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || Rise <= 0.0f)
	{
		return;
	}

	// A KILL'S PAYMENT SETS THE HOLD ONLY THROUGH THE KILL'S OWN STAMP, which follows it: ruled by the coordinating
	// session on 2026-11-25. Kindred Fires' "holds off decay as a kill does" is about Fervour from other sources; a kill
	// already holds off decay by its own grace, and a restart here would give a common kill inside an Elite kill's hold
	// the Elite's 8 seconds.
	if (Cataclysm->IsPayingForAKill())
	{
		return;
	}

	// KINDRED FIRES: ANY OTHER RISE restarts the hold at its full length.
	if (Cataclysm->StatForSkill(FName(HoldRestartedByAnyGainStat), FGameplayTagContainer(), 0.0f) > 0.0f)
	{
		Cataclysm->RestartKillDecayHold(KillDecayHoldFullSeconds(AbilitySystem));
		return;
	}

	// BORROWED TIME: otherwise a rise lengthens a hold still running, capped at its full length.
	const float Extension =
		Cataclysm->StatForSkill(FName(HoldExtendedOnGainSecondsStat), FGameplayTagContainer(), 0.0f);
	if (Extension > 0.0f)
	{
		Cataclysm->ExtendKillDecayHold(Extension, KillDecayHoldFullSeconds(AbilitySystem));
	}
}

FGameplayTag UCataclysmFervour::LeechTag()
{
	// The same refusal `RegenerationTag` makes, for the same reason: a test may
	// run before the tag table is loaded, and an empty tag matches nothing.
	return FGameplayTag::RequestGameplayTag(TEXT("Keyword.Leech"),
											/*ErrorIfNotFound=*/false);
}

FGameplayTag UCataclysmFervour::RegenerationTag()
{
	// ErrorIfNotFound IS FALSE FOR THE REASON UCataclysmDamageCalculation GIVES
	// FOR ITS OWN TAGS: a test may run before the tag table is loaded, and an
	// empty tag matches nothing, which is the right answer there.
	return FGameplayTag::RequestGameplayTag(TEXT("Keyword.Regeneration"),
											/*ErrorIfNotFound=*/false);
}

float UCataclysmFervour::FervourFor(float HealthChanged, float MaxHealth,
									float RatePerPercent)
{
	if (HealthChanged <= 0.0f || MaxHealth <= 0.0f || RatePerPercent <= 0.0f)
	{
		return 0.0f;
	}

	// THE SHARE OF THE CHARACTER'S WHOLE HEALTH, IN PERCENT, TIMES THE RATE.
	// A character with 500 health and one with 5000 fill the bar at the same
	// speed relative to how much of themselves they have spent, which is what
	// "1 per 1% of maximum health" means and is why the rate is not per point.
	return HealthChanged / MaxHealth * 100.0f * RatePerPercent;
}

float UCataclysmFervour::RateFor(const UAbilitySystemComponent* AbilitySystem,
								 FName Stat,
								 const FGameplayTagContainer& Context)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	// NO CLASS RESOURCE ATTRIBUTE SET MEANS NO FERVOUR. Only the player state
	// carries one; every enemy and every minion in the game answers here. That
	// is a guard rather than a fault, and it is why nothing below has to ask
	// whether the thing it is looking at is a player.
	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	if (!Resource)
	{
		return 0.0f;
	}

	const TMap<FString, FGameplayAttribute>& ByName = RateAttributes();
	const FGameplayAttribute* Attribute = ByName.Find(Stat.ToString());
	if (!Attribute)
	{
		return 0.0f;
	}

	const float Plain = AbilitySystem->GetNumericAttribute(*Attribute);

	// THE SAME PIPELINE THE STAT WAS WORKED OUT BY, RUN AGAIN WITH THE HIT'S
	// TAGS. Without this the Masochist's Shared Agony node -- which increases
	// Fervour gained from damage over time specifically -- would be dropped
	// before it reached anything, because the attribute holds the value worked
	// out with no tags in hand. Issue #943 is the same gap on every other stat.
	if (const UCataclysmAbilitySystemComponent* Cataclysm =
			Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		return Cataclysm->StatForSkill(Stat, Context, Plain);
	}

	return Plain;
}

float UCataclysmFervour::GainFromDamage(UAbilitySystemComponent* AbilitySystem,
										float HealthLost,
										const FGameplayTagContainer& HitTags)
{
	return Move(AbilitySystem, FName(FromDamageStat), HealthLost, HitTags,
				/*Sign=*/1.0f);
}

float UCataclysmFervour::LoseFromDamage(UAbilitySystemComponent* AbilitySystem, float HealthLost)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || HealthLost <= 0.0f || !Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		return 0.0f;
	}
	const float PerFervour =
		Cataclysm->StatForSkill(FName(HealthPercentLostPerFervourLostStat), FGameplayTagContainer(), 0.0f);
	const float MaximumHealth = Cataclysm->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	if (PerFervour <= 0.0f || MaximumHealth <= 0.0f)
	{
		return 0.0f;
	}
	const int32 Whole =
		Cataclysm->AccumulateHealthLostForFervour(HealthLost / MaximumHealth * 100.0f, PerFervour);
	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = Cataclysm->GetNumericAttribute(Pool);
	// NEVER BELOW NOUGHT, AND NOT STOPPED BY MISER'S GRIP'S FLOOR, which is about decay, as ruled.
	const float Taken = FMath::Min(static_cast<float>(Whole), FMath::Max(0.0f, Before));
	if (Taken <= 0.0f)
	{
		return 0.0f;
	}
	Cataclysm->ApplyModToAttribute(Pool, EGameplayModOp::Additive, -Taken);
	return Before - Cataclysm->GetNumericAttribute(Pool);
}

float UCataclysmFervour::GainFromHealthCost(
	UAbilitySystemComponent* AbilitySystem, float HealthSpent)
{
	// NO TAGS. A health cost is paid by the character rather than delivered by
	// a hit, so there is nothing for it to carry and no node scopes to it.
	return Move(AbilitySystem, FName(FromCostStat), HealthSpent,
				FGameplayTagContainer(), /*Sign=*/1.0f);
}

float UCataclysmFervour::RemoveForHealing(
	UAbilitySystemComponent* AbilitySystem, float HealthRestored,
	const FGameplayTagContainer& HealingTags)
{
	return Move(AbilitySystem, FName(LostToHealingStat), HealthRestored,
				HealingTags, /*Sign=*/-1.0f);
}

bool UCataclysmFervour::HasAGenerator(
	const UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem
		|| !AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		return false;
	}

	// THE ATTRIBUTES AND NOT `RateFor`, because this asks whether the character
	// has a generator at all rather than what one is worth to a particular hit.
	// A rate that only exists on damage over time still counts.
	for (const FGameplayAttribute& Attribute :
			UCataclysmClassResourceAttributeSet::GetRateAttributes())
	{
		if (AbilitySystem->GetNumericAttribute(Attribute) > 0.0f)
		{
			return true;
		}
	}

	// AND ANY ROW ON A GAIN STAT, as `FCataclysmWarFervour::HasARate` asks its own: a stat with no attribute
	// (`fervour_per_kill`, the Soul Collector's starting node), one whose attribute is not a rate attribute
	// (`fervour_on_enemy_death_nearby`, the Necromancer's; `fervour_per_enemy_hit`, the Ravager's), and a row a condition
	// or a scale keeps out of its attribute. Any row at all, for the reason given above. 2026-11-26.
	if (const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem))
	{
		for (const TCHAR* Stat : GainStats())
		{
			if (const FCataclysmStatInputs* Inputs = Cataclysm->GetStatInputs(FName(Stat)))
			{
				for (const FCataclysmStatModifier& Modifier : Inputs->Modifiers)
				{
					if (Modifier.Value > 0.0f)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

const TArray<const TCHAR*>& UCataclysmFervour::GainStats()
{
	static const TArray<const TCHAR*> Stats = {FromDamageStat,	  FromCostStat,		 PerSecondStat,
											   PerCastStat,		  OnDroppingLowStat, FromMinionsStat,
											   OnMinionDeathStat, PerEnemyInReachStat, PerEnemyHitStat,
											   OnEnemyDeathNearbyStat, PerKillStat};
	return Stats;
}

const TMap<FString, FGameplayAttribute>& UCataclysmFervour::RateAttributes()
{
	// BUILT ONCE. A gameplay attribute holds a pointer to a reflected property,
	// so this cannot be a compile-time constant, and it is read on every hit.
	static const TMap<FString, FGameplayAttribute> Map = {
		{FromDamageStat,
		 UCataclysmClassResourceAttributeSet::GetFervourFromDamageAttribute()},
		{FromCostStat,
		 UCataclysmClassResourceAttributeSet::GetFervourFromCostAttribute()},
		{LostToHealingStat,
		 UCataclysmClassResourceAttributeSet::GetFervourLostToHealingAttribute()},
	};
	return Map;
}

bool UCataclysmFervour::LossIsSuppressed(
	const UAbilitySystemComponent* AbilitySystem,
	const FGameplayTagContainer& Healing)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm
		|| !Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		// NO CLASS RESOURCE SET MEANS NO FERVOUR TO PROTECT, which is every
		// enemy in the game, and a component that is not this project's subclass
		// cannot be asked anything with tags in hand.
		return false;
	}

	// ASKED WITH THE HEALING'S TAGS, WHICH IS THE WHOLE MECHANISM. Issue #1006.
	// Sanguine Ledger's row requires `Keyword.Regeneration` and Wounds That
	// Feed's requires `Keyword.Leech`, so the same stat answers differently for
	// the two kinds of healing on the same character.
	//
	// A FALLBACK OF ZERO RATHER THAN THE ATTRIBUTE'S OWN VALUE. The attribute
	// holds what the flag is worth with NO healing in hand, and every row that
	// sets it requires a tag, so the attribute is zero for everybody. Passing it
	// would be passing zero the long way round, and passing anything else would
	// suppress the loss for healing that matched no row.
	return Cataclysm->StatForSkill(FName(LossSuppressedStat), Healing, 0.0f)
		> 0.0f;
}

float UCataclysmFervour::FloorAgainstHealing(
	const UAbilitySystemComponent* AbilitySystem,
	const FGameplayTagContainer& Healing)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm
		|| !Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		// NO CLASS RESOURCE SET MEANS NO FERVOUR TO KEEP, for the reason
		// `LossIsSuppressed` above gives.
		return 0.0f;
	}

	// A FALLBACK OF ZERO, for the reason `LossIsSuppressed` gives: the stat has
	// no gameplay attribute, so nothing recorded for it means no floor.
	//
	// FLOORED AT ZERO ITSELF, because a negative floor would be a floor below
	// an empty pool, which is no floor; nothing states one.
	return FMath::Max(
		0.0f, Cataclysm->StatForSkill(FName(FloorAgainstHealingStat), Healing, 0.0f));
}

float UCataclysmFervour::Move(UAbilitySystemComponent* AbilitySystem,
							  FName Stat, float HealthChanged,
							  const FGameplayTagContainer& Context, float Sign)
{
	if (!AbilitySystem || HealthChanged <= 0.0f)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmVitalAttributeSet* Vitals =
		AbilitySystem->GetSet<UCataclysmVitalAttributeSet>();
	if (!Resource || !Vitals)
	{
		return 0.0f;
	}

	// SOME HEALING DOES NOT REMOVE FERVOUR AT ALL. Issue #1006. Two Masochist
	// keystones say so: Sanguine Ledger for health regeneration and Wounds That
	// Feed for life leech. They set one flag, and the `Required Tags` on the row
	// decide which healing it covers -- which is why the question is asked with
	// this healing's own tags in hand rather than read off the attribute.
	//
	// ONLY THE REMOVING DIRECTION. A negative sign is Fervour leaving the pool,
	// which is healing; a positive one is damage taken or a cost paid, and no
	// node suppresses those. Asking on the way in as well would let a node meant
	// to protect the bar quietly stop it filling.
	if (Sign < 0.0f && LossIsSuppressed(AbilitySystem, Context))
	{
		return 0.0f;
	}

	const float Amount = FervourFor(
		HealthChanged, Vitals->GetMaxHealth(),
		RateFor(AbilitySystem, Stat, Context));
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED HERE BECAUSE IT KEEPS THE STORED BASE AT OR UNDER THE MAXIMUM.
	// Issue #1036, read in the UE 5.8 GameplayAbilities source on 2026-09-30:
	// `ApplyModToAttribute` (GameplayEffect.cpp 4155-4161) adds the change to
	// the BASE and hands it to `SetAttributeBaseValue` (about 4001-4013), which
	// stores it as given and only then clamps the CURRENT value in
	// `PreAttributeChange`. Every current-value write reaches that clamp, with
	// or without an aggregator; the base is never clamped there. So without
	// this, a gain at a full pool raised the base above the maximum while the
	// bar read full, and the next spend came off the hidden excess.
	//
	// A GUARD PROOF ON 2026-08-27 REMOVED THIS AND NOTHING FAILED, because
	// every test then read only the current value after one write, and the
	// `return` below measures the current value.
	// `Cataclysm.Fervour.AGainAtAFullPoolIsNotBankedForTheNextSpend` reads a
	// spend after a gain.
	//
	// ONE OF THREE PLACES THAT CLAMP THE POOL, the other two being
	// `PreAttributeChange` and `PostGameplayEffectExecute` in
	// `UCataclysmClassResourceAttributeSet`. All three used to consult a flag
	// saying the pool had no maximum, granted by The Final Vow's second option
	// under issue #1029; issue #1031 rewrote all twelve Masochist capstone
	// options and that one no longer exists, so the maximum is simply the
	// maximum again.
	//
	// AND A GAIN IS SCALED BY HOW FAST THE CLASS RESOURCE GENERATES, BEFORE THAT
	// CLAMP. Ruled 2026-10-07, `class_resource_generation`. This one line carries
	// both directions: a positive sign is damage taken or a cost paid, which is
	// generation, and a negative one is healing, which is a loss and is handed
	// back by the helper exactly as it came.
	const float Moved = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(
		AbilitySystem, Sign * Amount);

	// AND HEALING STOPS AT A FLOOR A ROW STATES. Sanguine Ledger, reworked by
	// the project owner's approval of 2026-11-24: "Healing never takes your
	// Fervour below 50." Only the removing direction, for the reason the
	// suppression above gives. A pool already at or below the floor keeps what
	// it has -- the lowest healing may leave is the smaller of the floor and
	// what was there -- and one above it loses no further than the floor. A
	// character with no floor answers 0, which is the clamp this always had.
	const float Lowest = Sign < 0.0f
		? FMath::Min(Before, FloorAgainstHealing(AbilitySystem, Context))
		: 0.0f;
	const float Wanted = FMath::Clamp(Before + Moved, FMath::Max(0.0f, Lowest),
									  Resource->MaximumClassResourceAsked());
	// A GAIN: the whole GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's. Before the
	// full-bar return below, so a hit taken at a full bar still reaches it.
	if (Moved > 0.0f)
	{
		RaisePool(AbilitySystem, Moved);
		return AbilitySystem->GetNumericAttribute(Pool) - Before;
	}

	const float Change = Wanted - Before;
	if (FMath::IsNearlyZero(Change))
	{
		// ALREADY FULL, OR ALREADY EMPTY. Not a fault: a Masochist at maximum
		// Fervour taking another hit is the ordinary case, and it is what two of
		// the tree's keystones are about.
		return 0.0f;
	}

	// A LOSS, written as it was.
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);

	// MEASURED RATHER THAN ASSUMED, so a caller reporting what it did says what
	// really happened rather than what it asked for.
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainPerSecondStep(
	UAbilitySystemComponent* AbilitySystem, float SecondsInStep)
{
	if (!AbilitySystem || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill, which is every enemy.
		return 0.0f;
	}

	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE. Issue #1008. Low Life's row
	// carries a health condition, and a conditional bonus is never folded into
	// a gameplay attribute -- it would be stale the moment health moved -- so
	// the attribute is zero for a character holding the keystone and a plain
	// read would answer zero for ever with nothing reporting it.
	//
	// NO TAGS, because nothing is happening: this is Fervour arriving from the
	// passage of time rather than from a hit or a heal, so there is no event
	// whose tags could scope it.
	const float PerSecond = Cataclysm->StatForSkill(
		FName(PerSecondStat), FGameplayTagContainer(), 0.0f);

	// AND THE RITUALIST'S RATE, WHICH IS PER MINION RATHER THAN FLAT.
	// Issue #1518: "1 per second for each minion you have".
	//
	// A SECOND STAT RATHER THAN A SECOND VALUE OF THE ONE ABOVE, so the
	// Ritualist's `Binding Sigils` node -- "+2% increased Fervour gained from
	// your minions" -- cannot reach the Masochist's Low Life keystone, which
	// grants Fervour for being hurt. One character can reach all 24 class
	// trees, so a character holding both nodes is ordinary.
	//
	// THE MINION COUNT IS NOT MULTIPLIED IN HERE. The row carries the scale
	// `minions_held`, so `StatForSkill` has already multiplied the rate by how
	// many minions the character is commanding, counted from the world at the
	// moment of this call. A Ritualist commanding nothing gets zero out of this
	// line and no special case is needed to make that true.
	//
	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, and the fallback is zero
	// for the reason `GainForCast` gives for its own: a scaled bonus is never
	// folded into a gameplay attribute -- it would be stale the moment a minion
	// was summoned or died -- so the attribute is zero even for a Ritualist
	// holding the node, and passing it would be passing zero the long way round.
	const float PerMinionPerSecond = Cataclysm->StatForSkill(
		FName(FromMinionsStat), FGameplayTagContainer(), 0.0f);

	// AND THE RAVAGER'S RATE, WHICH IS PER ENEMY STANDING NEAR RATHER THAN PER
	// MINION. Issue #1515: "1 per second for every enemy within 4 metres of
	// you". `Ravager_basic_spine_000` is its only source.
	//
	// A THIRD STAT RATHER THAN A THIRD VALUE OF EITHER ABOVE, for the reason
	// the minion rate gives for being separate from the flat one. The
	// Ravager's `Held Ground` node reads "+2% increased Fervour gained from
	// enemies near you", and a shared stat would hand that increase to Low Life
	// and to the Ritualist's minions as well. One character can reach all 24
	// class trees, so holding all three nodes is ordinary rather than exotic.
	//
	// THE BODIES ARE NOT COUNTED IN HERE, exactly as the minions are not. The
	// row carries `Scale=enemies_in_reach` with `ReachMetres=4`, so
	// `StatForSkill` has already multiplied the rate by how many hostile actors
	// stand inside that radius, counted from the world at the moment of this
	// call. A Ravager standing alone gets zero out of this line with no special
	// case written for it.
	//
	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, and the fallback is zero
	// for the same reason again: a scaled bonus is never folded into a gameplay
	// attribute -- it would be stale the moment anything moved -- so the
	// attribute reads zero even for a Ravager holding the node.
	const float PerEnemyNearPerSecond = Cataclysm->StatForSkill(
		FName(PerEnemyInReachStat), FGameplayTagContainer(), 0.0f);

	// SUMMED RATHER THAN ONE OR THE OTHER, and clamped once below. A character
	// in several trees -- hurt, holding minions and standing in a crowd -- is
	// earning from every rule at once and should receive all of them.
	const float PerSecondAltogether =
		FMath::Max(0.0f, PerSecond) + FMath::Max(0.0f, PerMinionPerSecond)
		+ FMath::Max(0.0f, PerEnemyNearPerSecond);
	if (PerSecondAltogether <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME UNTIL A POINT IS SPENT IN LOW LIFE OR IN
		// THE RITUALIST'S OR THE RAVAGER'S STARTING NODE; every character
		// holding Low Life that is not hurt enough for the condition; every
		// Ritualist commanding nothing at all; and every Ravager standing
		// alone.
		return 0.0f;
	}

	// SCALED BY HOW FAST THE CLASS RESOURCE GENERATES, once for the whole sum
	// and before the clamp below. Ruled 2026-10-07, `class_resource_generation`.
	const float Wanted = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(
		AbilitySystem, PerSecondAltogether * SecondsInStep);

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the same rule `Move` above follows and for
	// the reason it gives: `ApplyModToAttribute` writes a base value and whether
	// that reaches `PreAttributeChange` depends on whether an aggregator happens
	// to exist for the attribute.
	// THE WHOLE GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's.
	const float Gain = Wanted;
	if (Gain <= 0.0f)
	{
		return 0.0f;
	}
	RaisePool(AbilitySystem, Gain);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::DecayStep(ACataclysmCharacterBase* Character,
								  float SecondsInStep)
{
	if (!Character || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	// NOR A CREATURE HOLDING A PLAYER BUILD, the Corrupted Stalker. It is raised with its Ultimate's Fervour, the
	// owner's answer of 2026-10-26, and on the first floor it hunts it is raised at the walkable cell farthest from the
	// player: with its build's starting node it would drain them at 5 a second on the way. Ruled by the coordinating
	// session the same day as the means of keeping that answer true. Issue #1308.
	if (const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Character);
		Enemy && Enemy->bHoldsAPlayerBuild)
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!Cataclysm)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>();
	if (!Resource)
	{
		// No class resource set means no pool to drain, which is every enemy.
		return 0.0f;
	}

	// THE RATE FIRST, BECAUSE IT IS THE CHEAPEST QUESTION AND REFUSES MOST
	// CHARACTERS. Zero for everyone without `Ravager_basic_spine_000`, which is
	// every character in the game until a point is spent there, so the walk for
	// nearby bodies below is never paid for by anyone the rule does not apply
	// to. That ordering is the whole reason this is not expensive.
	const float PerSecond = Cataclysm->StatForSkill(
		FName(DecayPerSecondStat), FGameplayTagContainer(), 0.0f);
	if (PerSecond <= 0.0f)
	{
		return 0.0f;
	}

	// AND NOTHING TO DRAIN IS ALSO A REFUSAL, checked before the walk for the
	// same reason. A Ravager standing alone with an empty pool is the ordinary
	// state between fights and must not cost a search of the world every step.
	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = Cataclysm->GetNumericAttribute(Pool);
	if (Before <= 0.0f)
	{
		return 0.0f;
	}

	// AND NOTHING DECAYS WHILE A BUFF THAT HOLDS FERVOUR RUNS. Issue #2885, the Death draft's N8, slice DS3: the Sword's
	// Cold Blood, "while it lasts your Fervour does not decay" (`HoldsFervour=1`). Asked after the two cheap refusals
	// above and before the search for bodies below: a walk over the character's own running abilities, which the
	// search costs more than. The Death classes' decay after a kill (`KillDecayStep`) and the War classes' out of combat
	// (`FCataclysmWarFervour::OutOfCombatDecayStep`) ask it too, since the Death skills merge.
	if (UCataclysmSkillTemplate::ARunningBuffHoldsFervour(Character))
	{
		return 0.0f;
	}

	// THE RADIUS IS A STAT AND TWO NODES ADD TO IT. The starting node grants 4
	// and `Ravager_keystone_d_kC` No Ground Given grants 4 more, so a character
	// holding both is asking about 8 -- which is what that keystone's sentence
	// names. See `DecayGraceMetresStat`.
	float Metres = Cataclysm->StatForSkill(
		FName(DecayGraceMetresStat), FGameplayTagContainer(), 0.0f);

	// AND NOWHERE TO RUN'S "Your Fervour does not decay while any enemy is held
	// this way". Issue #1515. Every hostile within its radius is held, so the
	// radius here is at least that wide: the larger of the two, not their sum,
	// ruled 2026-09-25, because a grace row would add to No Ground Given's and
	// the sentence states 8, not 12.
	const float HoldMetres = Cataclysm->StatForSkill(
		FName(UCataclysmDebuffs::NowhereToRunMetresStat), FGameplayTagContainer(), 0.0f);
	if (HoldMetres > 0.0f)
	{
		Metres = FMath::Max(Metres, HoldMetres);
	}

	// A RADIUS OF NOTHING IS NOT A RADIUS OF EVERYTHING. A character with the
	// decay rate and no radius -- which no authored data produces, because one
	// node grants both -- would otherwise be judged out of contact always. The
	// safe direction for an unknown reading is the one that does not strengthen
	// the rule against the character.
	if (Metres > 0.0f)
	{
		if (UCataclysmTargetCandidates* Candidates =
				UCataclysmTargetCandidates::In(Character->GetWorld()))
		{
			TArray<float> Distances;
			Candidates->HostileDistancesWithinMetres(
				Character, Character->GetActorLocation(), Metres, Distances);
			if (Distances.Num() > 0)
			{
				// CONTACT HOLDS, SO THE CLOCK RESTARTS AND NOTHING DRAINS.
				Cataclysm->NoteEnemyInReach();
				return 0.0f;
			}
		}
	}

	// THE THREE SECONDS AND WHAT A ROW ADDS TO THEM. The Ravager tree rework of
	// 2026-11-24, Stoked: "Your Fervour starts to decay 1 second later per
	// point." Asked here, after the cheaper refusals above, so a character with
	// no decay rate never asks; a fallback of nought, and a negative row is
	// floored so nothing can make the grace shorter than the starting node's.
	const float Grace = DecayGraceSeconds
		+ FMath::Max(0.0f, Cataclysm->StatForSkill(FName(DecayGraceSecondsStat),
												   FGameplayTagContainer(), 0.0f));
	if (!Cataclysm->OutOfContactFor(Grace))
	{
		// INSIDE THE GRACE. The node grants the grace so that stepping
		// behind a pillar does not empty a bar the character spent a fight
		// filling.
		return 0.0f;
	}

	// CLAMPED BEFORE IT IS WRITTEN, the same rule `GainPerSecondStep` follows
	// and for the reason it gives: `ApplyModToAttribute` writes a base value,
	// and whether that reaches `PreAttributeChange` depends on whether an
	// aggregator happens to exist for the attribute.
	const float Change =
		FMath::Clamp(Before - PerSecond * SecondsInStep,
					 0.0f, Resource->MaximumClassResourceAsked())
		- Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	Cataclysm->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	const float Moved = Cataclysm->GetNumericAttribute(Pool) - Before;
	// AND WHAT IT TOOK, KEPT FOR RECLAIMED (the Death trees' slice C4b2, as ruled: both decays count).
	Cataclysm->NoteFervourDecayed(-Moved);
	return Moved;
}

float UCataclysmFervour::GainForCast(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill, which is every enemy.
		return 0.0f;
	}

	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE. Issue #1051. The Last
	// Drop's row carries a health condition, so the attribute is zero even for
	// a character holding the option and a plain read would answer zero for
	// ever with nothing reporting it. The same move `GainPerSecondStep` above
	// makes for Low Life.
	//
	// NO TAGS. The grant is for casting a skill at all rather than for casting
	// a skill of a particular kind, so there is nothing for tags to scope.
	// The node says "every skill you cast".
	const float PerCast = Cataclysm->StatForSkill(
		FName(PerCastStat), FGameplayTagContainer(), 0.0f);
	if (PerCast <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME WITHOUT THAT CAPSTONE OPTION, and every
		// character holding it that is not hurt enough for the condition.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the same rule every other write to the
	// pool in this file follows and for the reason they give:
	// `ApplyModToAttribute` writes a base value and whether that reaches
	// `PreAttributeChange` depends on whether an aggregator happens to exist.
	//
	// SCALED FIRST BY HOW FAST THE CLASS RESOURCE GENERATES. Ruled 2026-10-07.
	// THE WHOLE GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's.
	const float Gain = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, PerCast);
	if (Gain <= 0.0f)
	{
		return 0.0f;
	}
	RaisePool(AbilitySystem, Gain);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainOnDroppingLow(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill, which is every enemy.
		return 0.0f;
	}

	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, the standing rule for
	// anything a later node might put a condition on. Issue #1069.
	//
	// AND THE FALLBACK IS THE ATTRIBUTE, WHICH IS NOT WHAT `GainForCast` ABOVE
	// PASSES. That one passes zero because The Last Drop's row carries a health
	// condition, and a conditional bonus is never folded into an attribute, so
	// the attribute is zero even for a character holding the option. Rock
	// Bottom's row carries no condition, so it IS folded in, and passing zero
	// would throw the answer away whenever no stat line has been recorded --
	// which is the ordinary case for an ability system before its first
	// refresh.
	//
	// NO TAGS. This arrives because health moved rather than because a skill of
	// a particular kind was used, so there is nothing for tags to scope.
	const float OnDropping = Cataclysm->StatForSkill(
		FName(OnDroppingLowStat), FGameplayTagContainer(),
		Resource->GetFervourOnDroppingLow());
	if (OnDropping <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the rule every other write to the pool in
	// this file follows and for the reason they give.
	//
	// SCALED FIRST BY HOW FAST THE CLASS RESOURCE GENERATES. Ruled 2026-10-07.
	// THE WHOLE GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's.
	const float Gain = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, OnDropping);
	if (Gain <= 0.0f)
	{
		return 0.0f;
	}
	RaisePool(AbilitySystem, Gain);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::RestoreHealthOnKill(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to spend, which is every enemy.
		return 0.0f;
	}

	// THE NODE FIRST, BECAUSE IT REFUSES ALMOST EVERYONE. Zero for every
	// character without `Ravager_basic_d_c1`, so nobody else touches the pool
	// or reads their health on a kill.
	const float Percent = Cataclysm->StatForSkill(
		FName(HealthRestoredOnKillStat), FGameplayTagContainer(), 0.0f);
	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	// AN UNPAYABLE COST BUYS NOTHING. See the header for why a character
	// holding less than the cost restores nothing rather than a share: the same
	// tree's `Bought With Ruin` states that rule outright.
	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Held = AbilitySystem->GetNumericAttribute(Pool);
	if (Held < KillRestoreCost)
	{
		return 0.0f;
	}

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Maximum = AbilitySystem->GetNumericAttribute(MaxHealth);
	const float Before = AbilitySystem->GetNumericAttribute(Health);

	// NOTHING TO RESTORE IS NOTHING TO BUY. A character already as high as
	// healing may bring it keeps its Fervour: paying for a restoration that
	// restores nothing is a cost with no effect, and no sentence describes one.
	//
	// "AS HIGH AS HEALING MAY BRING IT", NOT "AT FULL HEALTH". Issue #1607,
	// ruled 2026-09-30: until then this compared with maximum health, so a player
	// at the healing ceiling or with health reserved paid five Fervour and
	// `TopUp` below restored nothing.
	if (Maximum <= 0.0f
		|| Before >= UCataclysmRegeneration::HealthHealingCeiling(*AbilitySystem))
	{
		return 0.0f;
	}

	// PAID BEFORE THE HEALING, so a character whose healing is refused
	// elsewhere still pays for the attempt the node describes. Clamped the way
	// every write to the pool in this file is clamped, for the reason `Move`
	// gives.
	const float Spend =
		FMath::Clamp(Held - KillRestoreCost, 0.0f, Resource->MaximumClassResourceAsked())
		- Held;
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Spend);

	// `TopUp` IS WHAT THE LIFE LEECH HEALS WITH, so this healing obeys the same
	// ceiling and the same received-healing reductions every other restoration
	// of health does, rather than a second copy of those rules.
	UCataclysmRegeneration::TopUp(*AbilitySystem, Health, MaxHealth,
								  Maximum * Percent / 100.0f);

	return AbilitySystem->GetNumericAttribute(Health) - Before;
}

const TCHAR* UCataclysmFervour::HealthRestoredOnCriticalKillStat = TEXT("health_restored_on_critical_kill");

float UCataclysmFervour::RestoreHealthOnCriticalKill(UAbilitySystemComponent* AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return 0.0f;
	}
	const float Percent =
		Cataclysm->StatForSkill(FName(HealthRestoredOnCriticalKillStat), FGameplayTagContainer(), 0.0f);
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (Percent <= 0.0f || !AbilitySystem->HasAttributeSetForAttribute(Health))
	{
		return 0.0f;
	}
	const float Maximum = AbilitySystem->GetNumericAttribute(MaxHealth);
	const float Before = AbilitySystem->GetNumericAttribute(Health);
	if (Maximum <= 0.0f || Before >= Maximum)
	{
		return 0.0f;
	}
	UCataclysmRegeneration::TopUp(*AbilitySystem, Health, MaxHealth, Maximum * Percent / 100.0f);
	return AbilitySystem->GetNumericAttribute(Health) - Before;
}

float UCataclysmFervour::RestoreHealthOnKillAtNoCost(
	UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no attribute to hold the option, which is
		// every enemy.
		return 0.0f;
	}

	// THE OPTION FIRST, BECAUSE IT REFUSES ALMOST EVERYONE. Zero for every
	// character without Long Hold, so nobody else reads their health on a kill.
	const float Percent = Cataclysm->StatForSkill(
		FName(HealthRestoredOnKillAtNoCostStat), FGameplayTagContainer(),
		Resource->GetHealthRestoredOnKillAtNoCost());
	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Maximum = AbilitySystem->GetNumericAttribute(MaxHealth);
	const float Before = AbilitySystem->GetNumericAttribute(Health);
	if (Maximum <= 0.0f || Before >= Maximum)
	{
		return 0.0f;
	}

	// `TopUp`, FOR THE REASON `RestoreHealthOnKill` GIVES: the same ceiling and
	// the same received-healing reductions as every other restoration.
	UCataclysmRegeneration::TopUp(*AbilitySystem, Health, MaxHealth,
								  Maximum * Percent / 100.0f);

	return AbilitySystem->GetNumericAttribute(Health) - Before;
}

float UCataclysmFervour::RestoreHealthOnFervourFull(UAbilitySystemComponent* AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || !AbilitySystem->GetSet<UCataclysmVitalAttributeSet>())
	{
		// No vital set means no health to restore.
		return 0.0f;
	}

	// THE NODE FIRST, BECAUSE IT REFUSES ALMOST EVERYONE. Nought for every
	// character without Second Wind, so nobody else reads their health when
	// their pool fills.
	const float Percent = Cataclysm->StatForSkill(
		FName(HealthRestoredOnFullStat), FGameplayTagContainer(), 0.0f);
	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Maximum = AbilitySystem->GetNumericAttribute(MaxHealth);
	const float Before = AbilitySystem->GetNumericAttribute(Health);
	if (Maximum <= 0.0f || Before <= 0.0f || Before >= Maximum)
	{
		// FULL ALREADY, OR DEAD: a corpse whose pool fills is not restored.
		return 0.0f;
	}

	// `TopUp`, FOR THE REASON `RestoreHealthOnKill` GIVES: the same ceiling and
	// the same received-healing reductions as every other restoration.
	UCataclysmRegeneration::TopUp(*AbilitySystem, Health, MaxHealth,
								  Maximum * Percent / 100.0f);

	return AbilitySystem->GetNumericAttribute(Health) - Before;
}

float UCataclysmFervour::BuyDamageForEnemiesStruckTogether(
	UAbilitySystemComponent* AbilitySystem,
	const FGameplayTagContainer& SkillTags, int32 EnemiesStruckTogether)
{
	// ONE ENEMY OR NONE BUYS NOTHING AND COSTS NOTHING: the sentence counts the
	// enemies "beyond the first". An unknown count of -1 lands here too.
	const int32 Beyond = EnemiesStruckTogether - 1;
	if (!AbilitySystem || Beyond <= 0)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		return 0.0f;
	}

	// THROUGH THE PIPELINE WITH THE SKILL'S TAGS, fallback zero. The row requires
	// `Type.Melee`, so an attack without that tag reads zero here and neither
	// pays nor buys.
	const float PercentPerEnemy = Cataclysm->StatForSkill(
		FName(IncreasedDamageBoughtPerExtraEnemyHitStat), SkillTags, 0.0f);
	if (PercentPerEnemy <= 0.0f)
	{
		return 0.0f;
	}

	// ALL OR NOTHING FOR THE WHOLE ATTACK: "If you cannot pay, the attack still
	// hits but gains nothing." Holding less than the whole cost spends nothing.
	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Held = AbilitySystem->GetNumericAttribute(Pool);
	const float Cost = ExtraEnemyHitCost * static_cast<float>(Beyond);
	if (Held < Cost)
	{
		return 0.0f;
	}

	// PAID BEFORE ANY BLOW, and clamped the way every write to the pool in this
	// file is clamped, for the reason `Move` gives.
	const float Spend =
		FMath::Clamp(Held - Cost, 0.0f, Resource->MaximumClassResourceAsked()) - Held;
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Spend);

	return PercentPerEnemy * static_cast<float>(Beyond);
}

float UCataclysmFervour::GainForEnemiesHit(UAbilitySystemComponent* AbilitySystem,
										   const FGameplayTagContainer& SkillTags,
										   int32 EnemiesHit)
{
	if (!AbilitySystem || EnemiesHit <= 0)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill, which is every enemy.
		return 0.0f;
	}

	// THROUGH THE PIPELINE WITH THE SKILL'S TAGS, fallback zero, for the reason
	// every rate in this file gives: a row that later gains a condition or a
	// scope is never folded into an attribute, and a plain read would answer
	// zero for ever.
	const float PerEnemy = Cataclysm->StatForSkill(
		FName(PerEnemyHitStat), SkillTags, 0.0f);
	if (PerEnemy <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME WITHOUT THE RAVAGER'S STARTING NODE.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the rule every write to the pool in this file
	// follows; `GainForCast` gives the reason.
	//
	// SCALED FIRST BY HOW FAST THE CLASS RESOURCE GENERATES. Ruled 2026-10-07.
	// THE WHOLE GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's.
	const float Gain = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, PerEnemy * static_cast<float>(EnemiesHit));
	if (Gain <= 0.0f)
	{
		return 0.0f;
	}
	RaisePool(AbilitySystem, Gain);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainOnMinionDeath(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill. That is every enemy, and
		// it is also every minion -- which matters here, because the caller has
		// a dying minion in hand and must pass its COMMANDER'S ability system.
		// Passing the minion's own lands on this line.
		return 0.0f;
	}

	// ASKED FOR RATHER THAN READ OFF THE ATTRIBUTE, the standing rule for
	// anything a later node might put a condition on. Issue #1518.
	//
	// AND THE FALLBACK IS THE ATTRIBUTE, WHICH IS WHAT `GainOnDroppingLow`
	// PASSES AND NOT WHAT THE PER-SECOND RATE ABOVE DOES. This node's row
	// carries no condition and no scale, so it IS folded into the attribute,
	// and passing zero would throw the answer away whenever no stat line has
	// been recorded -- the ordinary case for an ability system before its first
	// refresh.
	//
	// NO TAGS. A minion dying is not a skill of a particular kind, so there is
	// nothing for tags to scope.
	const float OnDeath = Cataclysm->StatForSkill(
		FName(OnMinionDeathStat), FGameplayTagContainer(),
		Resource->GetFervourOnMinionDeath());
	if (OnDeath <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME UNTIL A POINT IS SPENT IN THE RITUALIST'S
		// STARTING NODE.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the rule every other write to the pool in
	// this file follows and for the reason they give.
	//
	// SCALED FIRST BY HOW FAST THE CLASS RESOURCE GENERATES. Ruled 2026-10-07.
	// THE WHOLE GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's.
	const float Gain = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, OnDeath);
	if (Gain <= 0.0f)
	{
		return 0.0f;
	}
	RaisePool(AbilitySystem, Gain);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::GainStated(UAbilitySystemComponent* AbilitySystem, float Stated, bool bForAKill)
{
	if (!AbilitySystem || Stated <= 0.0f)
	{
		return 0.0f;
	}

	if (!AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		// No class resource set means no pool to fill, which is every enemy and every minion.
		return 0.0f;
	}

	// A KILL'S PAYMENT, MARKED FOR ITS WHOLE LENGTH, as `GainOnOwnKill` marks its own: opened before the rate is asked,
	// so Any Fuel's condition refuses on it, and held through `RaisePool`, so Kindred Fires and Borrowed Time leave the
	// hold to the kill. The Death skills merge, obligation 4 (the coordinating session's ruling on slice C4b1).
	UCataclysmAbilitySystemComponent* Paying = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	const bool bMarked = bForAKill && Paying;
	if (bMarked)
	{
		Paying->BeginKillPayment();
	}
	ON_SCOPE_EXIT
	{
		if (bMarked)
		{
			Paying->EndKillPayment();
		}
	};

	// SCALED FIRST BY HOW FAST THE CLASS RESOURCE GENERATES (ruled 2026-10-07), then THE WHOLE GAIN TO `RaisePool`, which
	// clamps it at the full bar after reserves and gives what does not fit to Overflow, as every gain in this file does
	// (the Death trees' slice C6c). Issue #2885, N6; through `RaisePool` since the Death skills merge, obligation 4.
	return RaisePool(AbilitySystem, UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, Stated));
}

float UCataclysmFervour::SpendAll(UAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem || !AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>())
	{
		// No class resource set means no pool to spend, which is every enemy and every minion.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float HeldBefore = AbilitySystem->GetNumericAttribute(Pool);
	if (HeldBefore <= 0.0f)
	{
		return 0.0f;
	}

	// TAKEN AS THE COST IS, through `ApplyModToAttribute`, so the pool's own clamp at nought is what it meets. Issue
	// #2885, N13.
	AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, -HeldBefore);
	return FMath::Max(0.0f, HeldBefore - AbilitySystem->GetNumericAttribute(Pool));
}

float UCataclysmFervour::GainOnEnemyDeathNearby(UAbilitySystemComponent* AbilitySystem,
												float MetresAway)
{
	// TOO FAR, OR NOT KNOWN, GRANTS NOTHING. At the radius or inside it counts.
	if (!AbilitySystem || MetresAway < 0.0f
		|| MetresAway > EnemyDeathNearbyRadiusMetres)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill, which is every enemy.
		return 0.0f;
	}

	// WITH THE ATTRIBUTE AS THE FALLBACK, for the reason `GainOnMinionDeath`
	// gives. NO TAGS: a death nearby is not a skill of any kind.
	const float OnDeath = Cataclysm->StatForSkill(
		FName(OnEnemyDeathNearbyStat), FGameplayTagContainer(),
		Resource->GetFervourOnEnemyDeathNearby());
	if (OnDeath <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME WITHOUT FED BY THE FALLEN.
		return 0.0f;
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// CLAMPED BEFORE IT IS WRITTEN, the rule every write to the pool in this
	// file follows.
	//
	// SCALED FIRST BY HOW FAST THE CLASS RESOURCE GENERATES. Ruled 2026-10-07.
	// THE WHOLE GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's.
	const float Gain = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, OnDeath);
	if (Gain <= 0.0f)
	{
		return 0.0f;
	}
	RaisePool(AbilitySystem, Gain);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

void UCataclysmFervour::NoteOwnKill(UAbilitySystemComponent* AbilitySystem, const AActor* Slain)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		return;
	}

	// THE GRACE IS ASKED AGAINST THE SLAIN CREATURE, so a row under `target_rarity_at_least` can lengthen it for an
	// Elite. Nought for a character with no grace row, which holds the decay until now: nothing.
	const float Grace = Cataclysm->StatForSkill(
		FName(KillDecayGraceSecondsStat), FGameplayTagContainer(), 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f, -1.0f,
		false, Slain);
	Cataclysm->NoteOwnKill(Grace);
}

const TCHAR* UCataclysmFervour::PerKillExtraChanceStat = TEXT("fervour_per_kill_extra_chance");

/**
 * Pins the roll `GainOnOwnKill` makes against Gleaning's chance, 0 to 100, for tests. Negative, the default, rolls. The
 * same shape as `Cataclysm.CooldownResetRoll`. The Death trees' slice C4a (2026-11-25).
 */
static TAutoConsoleVariable<float> CVarKillFervourExtraRoll(
	TEXT("Cataclysm.KillFervourExtraRoll"), -1.0f,
	TEXT("Pins the roll a kill makes against its chance of 1 extra Fervour (0-100); negative rolls."));

float UCataclysmFervour::GainOnOwnKill(UAbilitySystemComponent* AbilitySystem, const AActor* Slain)
{
	if (!AbilitySystem)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		// No class resource set means no pool to fill, which is every enemy.
		return 0.0f;
	}

	// A KILL'S PAYMENT, MARKED FOR ITS WHOLE LENGTH (the Death trees' slice C4b1, 2026-11-25), so Any Fuel's condition
	// refuses on the rate asked below and Borrowed Time leaves the hold to this kill's own stamp.
	UCataclysmAbilitySystemComponent* Paying = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	Paying->BeginKillPayment();
	ON_SCOPE_EXIT
	{
		Paying->EndKillPayment();
	};

	// ASKED AGAINST THE SLAIN CREATURE, so its rarity rung can pick the row. NO TAGS: the generator pays for the kill,
	// not for the skill that made it. NO FALLBACK BUT NOUGHT: the stat has no gameplay attribute.
	float PerKill = Cataclysm->StatForSkill(
		FName(PerKillStat), FGameplayTagContainer(), 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f, -1.0f, false, Slain);
	if (PerKill <= 0.0f)
	{
		// EVERY CHARACTER IN THE GAME WITHOUT A DEATH STARTING NODE OR THE BULWARK'S (which pays 1 a kill since the final
		// bundle, 2026-11-26).
		return 0.0f;
	}

	// AND GLEANING'S CHANCE OF ONE MORE, rolled on each kill (the Death trees' slice C4a). A chance of 100 or more is
	// always, compared rather than rolled, as a cooldown reset's is.
	const float ExtraChance = Cataclysm->StatForSkill(
		FName(PerKillExtraChanceStat), FGameplayTagContainer(), 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f, -1.0f,
		false, Slain);
	if (ExtraChance > 0.0f)
	{
		const float Pinned = CVarKillFervourExtraRoll.GetValueOnAnyThread();
		const float Roll = Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
		if (ExtraChance >= 100.0f || Roll < ExtraChance)
		{
			PerKill += 1.0f;
		}
	}

	const FGameplayAttribute Pool =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);

	// SCALED BY HOW FAST THE CLASS RESOURCE GENERATES, AND CLAMPED BEFORE IT IS WRITTEN, the shape of
	// `GainOnEnemyDeathNearby` above.
	// THE WHOLE GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's.
	const float Gain = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, PerKill);
	if (Gain <= 0.0f)
	{
		return 0.0f;
	}
	RaisePool(AbilitySystem, Gain);

	// AND SPIRIT VOLLEY'S SPIRITS, one for each whole Fervour the kill paid (the Death finish slice A, 2026-11-26, #2908).
	LooseSpiritVolley(AbilitySystem, Slain, Gain);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

const TCHAR* UCataclysmFervour::SpiritVolleyWeaponPercentStat = TEXT("spirit_volley_weapon_percent");
const TCHAR* const UCataclysmFervour::SpiritVolleyLogPrefix = TEXT("SPIRIT VOLLEY: ");

int32 UCataclysmFervour::LooseSpiritVolley(UAbilitySystemComponent* AbilitySystem, const AActor* Slain, float Paid)
{
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	AActor* Character = Cataclysm ? Cataclysm->GetAvatarActor() : nullptr;
	if (!Character || !Slain)
	{
		return 0;
	}
	const float Percent =
		Cataclysm->StatForSkill(FName(SpiritVolleyWeaponPercentStat), FGameplayTagContainer(), 0.0f);
	const int32 Spirits =
		FMath::Min(SpiritVolleyMostAKill, FMath::FloorToInt32(FMath::Max(0.0f, Paid) + KINDA_SMALL_NUMBER));
	if (Percent <= 0.0f || Spirits <= 0)
	{
		return 0;
	}

	// FROM WHERE THE BODY LIES NOW, and handed to the announcer, as `BurstTheSlain` hands its burst.
	const TWeakObjectPtr<AActor> Caller(Character);
	const TWeakObjectPtr<const AActor> Body(Slain);
	const FVector From = Slain->GetActorLocation();
	TFunction<void()> Fly = [Caller, Body, From, Spirits, Percent]()
	{
		if (Caller.IsValid())
		{
			FlySpirits(Caller.Get(), Body.Get(), From, Spirits, Percent);
		}
	};
	if (UCataclysmCombatEvents* Events = UCataclysmCombatEvents::In(Character->GetWorld()))
	{
		Events->AfterThisDeathIsHeard(MoveTemp(Fly));
	}
	else
	{
		Fly();
	}
	return Spirits;
}

int32 UCataclysmFervour::FlySpirits(AActor* Character, const AActor* Slain, const FVector& From, int32 Spirits,
									float Percent)
{
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World || Spirits <= 0 || Percent <= 0.0f)
	{
		return 0;
	}
	const FVector To = Character->GetActorLocation();
	const auto Living = [](const AActor* Each)
	{
		if (!Each || UCataclysmSkillEffects::IsDead(Each))
		{
			return false;
		}
		const UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Each);
		const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
		return !Its || !Its->HasAttributeSetForAttribute(Health) || Its->GetNumericAttribute(Health) > 0.0f;
	};

	int32 Struck = 0;
	for (int32 Index = 0; Index < Spirits; ++Index)
	{
		// "THE NEAREST ENEMY ON ITS WAY": within the corridor round the line from the body to the character, the one
		// nearest the body, which is the first the spirit meets. Searched within a sphere round the line's middle that
		// holds the whole corridor, then held to the corridor.
		const FVector Middle = 0.5f * (From + To);
		const float Reach = 0.5f * FVector::Dist(From, To) + SpiritPathHalfWidthCm;
		AActor* Nearest = nullptr;
		float NearestCm = TNumericLimits<float>::Max();
		for (AActor* Each : UCataclysmTargeting::FindEnemiesInSphere(World, Character, Middle, Reach))
		{
			if (Each == Slain || !Living(Each))
			{
				continue;
			}
			const FVector At = Each->GetActorLocation();
			if (FMath::PointDistToSegment(At, From, To) > SpiritPathHalfWidthCm)
			{
				continue;
			}
			const float Cm = FVector::Dist(At, From);
			if (Cm < NearestCm)
			{
				NearestCm = Cm;
				Nearest = Each;
			}
		}
		if (!Nearest)
		{
			// NOTHING ON ITS WAY: this spirit and every one after it reach the character and strike nothing.
			break;
		}

		// THE CHARACTER'S OWN HIT, at the vow's percent of weapon damage, as Death damage (the Siphon's Drain is typed the
		// same way), with no skill's tags: it is no skill's blow. Its kill is the character's, so it pays and looses
		// spirits in turn.
		FCataclysmHitDelivery Delivery;
		Delivery.DamageType = FName(TEXT("Death"));
		UCataclysmSkillEffects::ApplyHit(Character, Nearest, Percent, FGameplayTagContainer(), Delivery);
		++Struck;
	}
	UE_LOG(LogCataclysm, Log, TEXT("%s%d of %d spirits from %s struck an enemy on their way to %s, each for %.0f%% of "
								   "weapon damage."),
		   SpiritVolleyLogPrefix, Struck, Spirits, *GetNameSafe(Slain), *GetNameSafe(Character), Percent);
	return Struck;
}

float UCataclysmFervour::GainOnCriticalStrike(UAbilitySystemComponent* AbilitySystem, const AActor* Struck,
											 const FGameplayTagContainer& SkillTags)
{
	const UCataclysmClassResourceAttributeSet* Resource =
		AbilitySystem ? AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>() : nullptr;
	const UCataclysmAbilitySystemComponent* Cataclysm = Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Resource || !Cataclysm)
	{
		return 0.0f;
	}

	// ASKED AGAINST THE CHARACTER STRUCK, so Telling Blows' rarity row is judged on it, AND WITH THE BLOW'S SKILL TAGS,
	// so Battle Lust's melee row adds to a melee critical strike only (the bundle 3 merge, obligation 5): a row scoped to
	// no tag is paid whatever the skill. NO FALLBACK BUT NOUGHT: the stat has no gameplay attribute.
	const float PerCritical = Cataclysm->StatForSkill(
		FName(PerCriticalStrikeStat), SkillTags, 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f, -1.0f, false, Struck);
	if (PerCritical <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = AbilitySystem->GetNumericAttribute(Pool);
	// SCALED AND CLAMPED BEFORE IT IS WRITTEN, the shape of `GainOnOwnKill` above.
	// THE WHOLE GAIN TO `RaisePool`, NOT ONLY WHAT FITS, since window death3-w1's second pass (2026-11-25, as ruled):
	// `RaisePool` holds the pool to the full bar after reserves, and what did not fit is Overflow's.
	const float Gain = UCataclysmAbilitySystemComponent::ClassResourceGainScaled(AbilitySystem, PerCritical);
	if (Gain <= 0.0f)
	{
		return 0.0f;
	}
	RaisePool(AbilitySystem, Gain);
	return AbilitySystem->GetNumericAttribute(Pool) - Before;
}

float UCataclysmFervour::PayForABossTick(UAbilitySystemComponent* AbilitySystem, const AActor* Ticked)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || !Ticked)
	{
		return 0.0f;
	}
	// ASKED AGAINST THE CHARACTER TICKED, so the row's `target_is_boss` is judged on it. No tags and no blow: a tick is
	// not a blow being priced.
	const float Every = Cataclysm->StatForSkill(
		FName(BossTicksPayAKillEverySecondsStat), FGameplayTagContainer(), 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f,
		-1.0f, false, Ticked);
	if (Every <= 0.0f)
	{
		return 0.0f;
	}
	// ONE CLOCK FOR THE CHARACTER, as ruled, inclusive of its seconds less a thousandth, so a tick landing on the second
	// a frame's rounding puts a hair early still pays.
	const float Since = Cataclysm->SecondsSinceBossTickPaid();
	if (Since >= 0.0f && Since + 1.0e-3f < Every)
	{
		return 0.0f;
	}
	Cataclysm->NoteBossTickPaid();
	return GainOnOwnKill(AbilitySystem, Ticked);
}

bool UCataclysmFervour::CountAHitOnABoss(UAbilitySystemComponent* AbilitySystem, AActor* Struck)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || !Struck || UCataclysmSkillEffects::IsDead(Struck))
	{
		return false;
	}
	const float Every = Cataclysm->StatForSkill(
		FName(BossHitsCountedAsAKillEveryStat), FGameplayTagContainer(), 0.0f, -1.0f, FCataclysmBlowContext(), -1.0f,
		-1.0f, false, Struck);
	const int32 Nth = FMath::RoundToInt(Every);
	if (Nth < 1)
	{
		return false;
	}
	if (Cataclysm->CountBossHit(Struck) % Nth != 0)
	{
		return false;
	}
	CreditAKillWithoutADeath(AbilitySystem, Struck);
	return true;
}

void UCataclysmFervour::CreditAKillWithoutADeath(UAbilitySystemComponent* AbilitySystem, AActor* Slain)
{
	UCataclysmAbilitySystemComponent* Acting = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Acting || !Slain)
	{
		return;
	}
	// IN THE KILL HANDLER'S ORDER: what decay took given back, the payment, then the stamp, then the marks.
	GiveBackDecayOnKill(AbilitySystem);
	GainOnOwnKill(AbilitySystem, Slain);
	NoteOwnKill(AbilitySystem, Slain);
	MarkNearAKill(AbilitySystem, Slain, Slain->GetActorLocation());
	// AND WEARER OF THE DEAD'S MODIFIERS (the Death trees' slice C4d3): the creature is alive and carries them still.
	WearTheModifiersOf(AbilitySystem, Slain);

	const UAbilitySystemComponent* Theirs = UCataclysmTargeting::AbilitySystemOf(Slain);
	const float SlainMaximum =
		Theirs ? Theirs->GetNumericAttribute(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()) : 0.0f;
	Acting->ActOnEvent(FName(TEXT("kill")), nullptr, SlainMaximum, /*bLanded=*/true, Slain);

	// WHAT NEEDS A BODY IS DROPPED: the explosions and the burst a real kill's handler makes from these shares.
	Acting->TakePendingOverkillExplosionSharePercent();
	Acting->TakePendingOverkillExplosionRadiusCm();
	Acting->TakePendingSlainBurstSharePercent();
	Acting->TakePendingKillingBlowExplosionSharePercent();
}

const TArray<FName>& UCataclysmFervour::WornEnemyModifierRows()
{
	// IN PLACE ORDER, FROM 1. Appended to, never reordered: a row's place is written in the data.
	static const TArray<FName> Rows = {
		FName(TEXT("Generic_Titanic_Resolve")),
		FName(TEXT("Generic_Overpowered")),
		FName(TEXT("Generic_Bloodthirsty")),
		FName(TEXT("Generic_Unyielding")),
		FName(TEXT("Generic_Relentless")),
	};
	return Rows;
}

int32 UCataclysmFervour::WornEnemyModifierPlace(FName Row)
{
	const int32 Index = WornEnemyModifierRows().IndexOfByKey(Row);
	return Index == INDEX_NONE ? 0 : Index + 1;
}

int32 UCataclysmFervour::WearTheModifiersOf(UAbilitySystemComponent* AbilitySystem, const AActor* Slain)
{
	UCataclysmAbilitySystemComponent* Wearer = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	const ACataclysmEnemyCharacter* Creature = Cast<const ACataclysmEnemyCharacter>(Slain);
	if (!Wearer || !Creature || Creature->RarityStep < 1)
	{
		return 0;
	}
	int32 Recorded = 0;
	for (const FName& Row : Creature->ModifierRows)
	{
		const int32 Place = WornEnemyModifierPlace(Row);
		if (Place > 0 && Wearer->HoldsARowWearing(Place))
		{
			Wearer->NoteWornEnemyModifier(Place, WornEnemyModifierSeconds);
			++Recorded;
		}
	}
	if (Recorded > 0)
	{
		Wearer->RefreshLiveMaximumHealth();
	}
	return Recorded;
}

FString UCataclysmFervour::WornModifiersLine(const UCataclysmAbilitySystemComponent* Holder)
{
	if (!Holder)
	{
		return FString();
	}
	TArray<FString> Parts;
	const TArray<FName>& Rows = WornEnemyModifierRows();
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const float Left = Holder->WornEnemyModifierSecondsLeft(Index + 1);
		if (Left > 0.0f)
		{
			// THE ROW'S NAME LESS ITS CATACLYSM, its underscores as spaces: "Generic_Titanic_Resolve" is "Titanic Resolve".
			FString Name = Rows[Index].ToString();
			int32 Underscore = INDEX_NONE;
			if (Name.FindChar(TEXT('_'), Underscore))
			{
				Name.RightChopInline(Underscore + 1);
			}
			Name.ReplaceCharInline(TEXT('_'), TEXT(' '));
			Parts.Add(FString::Printf(TEXT("%s %ds"), *Name, FMath::CeilToInt(Left)));
		}
	}
	return Parts.IsEmpty() ? FString() : TEXT("Worn: ") + FString::Join(Parts, TEXT(", "));
}

float UCataclysmFervour::KillDecayStep(UAbilitySystemComponent* AbilitySystem, float SecondsInStep)
{
	UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm || SecondsInStep <= 0.0f)
	{
		return 0.0f;
	}

	// NOR A CREATURE HOLDING A PLAYER BUILD, for the reason `DecayStep` gives: the Corrupted Stalker is raised with its
	// Ultimate's Fervour and must not lose it on the walk to the player. Issue #1308.
	if (const ACataclysmEnemyCharacter* Enemy = Cast<ACataclysmEnemyCharacter>(Cataclysm->GetAvatarActor());
		Enemy && Enemy->bHoldsAPlayerBuild)
	{
		return 0.0f;
	}

	const UCataclysmClassResourceAttributeSet* Resource = Cataclysm->GetSet<UCataclysmClassResourceAttributeSet>();
	if (!Resource)
	{
		// No class resource set means no pool to drain, which is every enemy.
		return 0.0f;
	}

	// THE RATE FIRST, THE CHEAPEST QUESTION AND THE ONE THAT REFUSES EVERY CHARACTER WITHOUT A DEATH STARTING NODE.
	// Asked with the character's own state, so a row under `seconds_after_striking_a_boss` (Siege of One) is judged.
	const float PerSecond = Cataclysm->StatForSkill(FName(KillDecayPerSecondStat), FGameplayTagContainer(), 0.0f);
	if (PerSecond <= 0.0f)
	{
		return 0.0f;
	}

	const FGameplayAttribute Pool = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	const float Before = Cataclysm->GetNumericAttribute(Pool);
	if (Before <= 0.0f || Cataclysm->KillDecayIsHeld())
	{
		// AN EMPTY POOL, OR THE GRACE AFTER THE LAST KILL NOT YET RUN OUT.
		return 0.0f;
	}

	// AND NOTHING DECAYS WHILE A BUFF THAT HOLDS FERVOUR RUNS: the Sword's Cold Blood, "while it lasts your Fervour does
	// not decay", as `DecayStep` asks it. The Death skills merge, obligation 3, ruled by the coordinating session on
	// 2026-11-24. Each decay asks for itself and stops itself only, so a character under two decays is not held twice.
	if (UCataclysmSkillTemplate::ARunningBuffHoldsFervour(Cataclysm->GetAvatarActor()))
	{
		return 0.0f;
	}

	// AND NEVER BELOW MISER'S GRIP'S FLOOR, a share of the full bar after reserves (the Death trees' slice C6c,
	// 2026-11-25, as ruled): a pool at or under it loses nothing to decay.
	const float FloorPercent = FMath::Clamp(
		Cataclysm->StatForSkill(FName(DecayFloorPercentStat), FGameplayTagContainer(), 0.0f), 0.0f, 100.0f);
	const float Floor = Resource->MaximumClassResourceAsked() * FloorPercent / 100.0f;
	if (Before <= Floor)
	{
		return 0.0f;
	}

	// CLAMPED BEFORE IT IS WRITTEN, as `DecayStep` is and for its reason.
	const float Change =
		FMath::Clamp(Before - PerSecond * SecondsInStep, Floor, Resource->MaximumClassResourceAsked()) - Before;
	if (FMath::IsNearlyZero(Change))
	{
		return 0.0f;
	}

	Cataclysm->ApplyModToAttribute(Pool, EGameplayModOp::Additive, Change);
	const float Moved = Cataclysm->GetNumericAttribute(Pool) - Before;
	// AND WHAT IT TOOK, KEPT FOR RECLAIMED (the Death trees' slice C4b2, as ruled: both decays count).
	Cataclysm->NoteFervourDecayed(-Moved);
	return Moved;
}
