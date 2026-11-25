// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmStigmata.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
#include "GameFramework/Actor.h"

const TCHAR* UCataclysmStigmata::SelfBurnStat = TEXT("burn_self_on_burning_an_enemy");

float UCataclysmStigmata::TickFor(float MaxHealth)
{
	return MaxHealth > 0.0f ? MaxHealth * PercentOfMaximumHealthPerSecond / 100.0f : 0.0f;
}

bool UCataclysmStigmata::AfterSettingAlight(AActor* Instigator, AActor* Target,
											const FGameplayTag& EffectTag)
{
	// ONLY ANOTHER CHARACTER SET ALIGHT, which is what keeps this from calling
	// itself: the self-burn below is laid with its holder as its own target.
	if (!Instigator || !Target || Instigator == Target)
	{
		return false;
	}

	// ONLY A BURN. Every damage over time laid on another character comes
	// through here, and a bleed or a poison is not setting anything alight.
	const FGameplayTag Burn = UCataclysmSkillEffects::BurnTag();
	if (!Burn.IsValid() || !EffectTag.MatchesTag(Burn))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* Own = Cast<UCataclysmAbilitySystemComponent>(
		UCataclysmTargeting::AbilitySystemOf(Instigator));
	if (!Own)
	{
		return false;
	}

	// ASKED WITH THE BURN'S OWN TAG, the way the other rows read on an
	// application are. A fallback of nought: the stat has no gameplay attribute,
	// so nothing recorded is no Stigmata, which is every character but one.
	if (Own->StatForSkill(FName(SelfBurnStat), FGameplayTagContainer(EffectTag), 0.0f) <= 0.0f)
	{
		return false;
	}

	// NOTHING FOR A CORPSE, for the reason `UCataclysmNova::Step` gives: a
	// creature that has just died is still standing with an ability system.
	if (UCataclysmSkillEffects::IsDead(Instigator))
	{
		return false;
	}

	const UCataclysmVitalAttributeSet* Vitals = Own->GetSet<UCataclysmVitalAttributeSet>();
	const float PerTick = Vitals ? TickFor(Vitals->GetMaxHealth()) : 0.0f;
	if (PerTick <= 0.0f)
	{
		return false;
	}

	// ITS HOLDER AS BOTH ENDS, AND NOT SCALED BY ITS HOLDER'S STATS. See the
	// header: the sentence states the figure, so the three damage over time
	// stats are not asked, and `ApplyDamageOverTime` raises no `dot_applied`
	// event and lays no rider for an effect a character puts on itself.
	const bool bLaid = UCataclysmSkillEffects::ApplyDamageOverTime(
		Instigator, Instigator, PerTick, Seconds, Burn, /*bScalesWithInstigator=*/false);

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s set %s alight and Stigmata set it alight too, %.1f a second: %s."),
		   *GetNameSafe(Instigator), *GetNameSafe(Target), PerTick,
		   bLaid ? TEXT("laid") : TEXT("refused"));
	return bLaid;
}
