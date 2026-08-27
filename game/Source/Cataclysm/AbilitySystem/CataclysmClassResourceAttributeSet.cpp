// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UCataclysmClassResourceAttributeSet::UCataclysmClassResourceAttributeSet()
{
	// Most class resources build up from nothing during a fight rather than
	// starting full, so the current value starts at zero. The maximum matches
	// the 0-100 range the one designed resource uses.
	InitClassResource(0.0f);
	InitMaxClassResource(100.0f);

	// ZERO FOR ALL THREE RATES, AND THAT IS THE DESIGN RATHER THAN A GAP. A
	// character gains and loses no Fervour until it spends a point on a tree's
	// generator node, which is what makes that node worth a point. Issue #954.
	InitFervourFromDamage(0.0f);
	InitFervourFromCost(0.0f);
	InitFervourLostToHealing(0.0f);

	// AND ZERO FOR THE ADDED HEALTH COST, for the same reason. Issue #970.
	// A character with no point in the Masochist's Deeper Cuts node pays only
	// whatever cost a skill states for itself, which for every skill but Blood
	// Pyre is nothing at all.
	InitAddedHealthCost(0.0f);
	InitAddedHealthCostOfCurrent(0.0f);
	InitDeferredHealthCostShare(0.0f);
	InitHealthOwed(0.0f);

	// AND ZERO FOR BOTH DEBT RULES. Issues #995 and #997. A character with no
	// point in Rolling Debt pushes nothing out, and one without The Reckoning
	// has an ordinary debt that falls due on a timer.
	InitHealthDebtDelayExtension(0.0f);
	InitHealthDebtClearedOnlyByAKill(0.0f);

	// AND ZERO FOR BOTH FERVOUR RULES. Issues #1006 and #1008. A character
	// with no point in Sanguine Ledger or Wounds That Feed has healing that
	// removes Fervour as usual, and one without Low Life gains none on a timer.
	InitFervourLossSuppressed(0.0f);
	InitFervourPerSecond(0.0f);
	InitDamageToBleedingOnLowHealth(0.0f);
	InitDamageToBleedingWindow(0.0f);

	// AND THE POOL HAS A MAXIMUM UNLESS THE FINAL VOW'S SECOND OPTION SAYS NOT.
	// Issue #1029. Zero is the ordinary case for every character.
	InitClassResourceUncapped(0.0f);
}

void UCataclysmClassResourceAttributeSet::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, ClassResource);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, MaxClassResource);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, FervourFromDamage);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, FervourFromCost);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, FervourLostToHealing);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, AddedHealthCost);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, AddedHealthCostOfCurrent);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, DeferredHealthCostShare);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, HealthOwed);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, HealthDebtDelayExtension);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, HealthDebtClearedOnlyByAKill);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, FervourLossSuppressed);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, FervourPerSecond);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, DamageToBleedingOnLowHealth);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, DamageToBleedingWindow);
	CATACLYSM_REPLICATE(UCataclysmClassResourceAttributeSet, ClassResourceUncapped);
}

void UCataclysmClassResourceAttributeSet::PreAttributeChange(
	const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetClassResourceAttribute())
	{
		// THE FLOOR ALWAYS APPLIES AND THE CEILING MAY NOT. Issue #1029. The
		// Final Vow's second option says "Your Fervour has no maximum", and
		// nothing anywhere says a pool may go negative, so the two ends are not
		// treated alike.
		NewValue = GetClassResourceUncapped() > 0.0f
			? FMath::Max(0.0f, NewValue)
			: FMath::Clamp(NewValue, 0.0f, GetMaxClassResource());
	}
	else if (Attribute == GetDeferredHealthCostShareAttribute())
	{
		// HELD BETWEEN 0 AND 100, BECAUSE IT IS A SHARE OF A COST. Issue #991.
		// Below zero it would take more than the cost now and leave a negative
		// debt; above a hundred it would defer more than was charged and hand
		// the character health back at the moment of the cast.
		//
		// A HUNDRED IS LEGITIMATE and is what the Masochist's Deferred Payment
		// node reaches at its full ten points: the whole cost is taken later.
		NewValue = FMath::Clamp(NewValue, 0.0f, 100.0f);
	}
	else if (Attribute == GetMaxClassResourceAttribute())
	{
		// Zero is legitimate: a character with no class tree invested has no
		// resource, and should not be given a phantom one.
		NewValue = FMath::Max(NewValue, 0.0f);
	}
	else if (Attribute == GetFervourFromDamageAttribute()
		|| Attribute == GetFervourFromCostAttribute()
		|| Attribute == GetFervourLostToHealingAttribute()
		|| Attribute == GetAddedHealthCostAttribute()
		|| Attribute == GetAddedHealthCostOfCurrentAttribute()
		|| Attribute == GetHealthOwedAttribute()
		|| Attribute == GetHealthDebtDelayExtensionAttribute()
		|| Attribute == GetHealthDebtClearedOnlyByAKillAttribute()
		|| Attribute == GetFervourLossSuppressedAttribute()
		|| Attribute == GetFervourPerSecondAttribute()
		|| Attribute == GetDamageToBleedingOnLowHealthAttribute()
		|| Attribute == GetDamageToBleedingWindowAttribute())
	{
		// FLOORED AT ZERO, WHICH FOR A RATE MEANS "THIS DOES NOT MOVE THE BAR".
		// A negative rate would invert the rule the node states: taking damage
		// would empty Fervour and healing would fill it. Nothing in the design
		// asks for that, and the one node that reduces a rate -- the Masochist's
		// Staunch, at 5% per point over at most six points -- reaches 70% of the
		// rate and not past zero. Issue #954.
		//
		// AND FOR THE ADDED HEALTH COST IT MEANS "THIS COSTS NOTHING EXTRA".
		// Issue #970. A negative one would not be a smaller cost, it would be
		// health handed back on every cast, which is the same class of inversion
		// and is not what any node states.
		//
		// AND FOR THE DELAY EXTENSION IT MEANS "THIS PUSHES NOTHING OUT". Issue
		// #995. A negative extension would pull a debt FORWARD, which is the
		// opposite of what Rolling Debt says and would make a character worse
		// off for spending points on it.
		//
		// AND FOR THE RECKONING FLAG IT MEANS "OFF". Issue #997. It is read as
		// a yes or no rather than as a quantity, so anything above zero is yes;
		// what has to be refused is a negative, which would read as off while
		// looking like a real value in the debugger.
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UCataclysmClassResourceAttributeSet::PostGameplayEffectExecute(
	const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetClassResourceAttribute())
	{
		// THE SECOND OF THE THREE PLACES THAT CLAMP THE POOL, and it honours the
		// uncapped flag for the same reason the first does. Issue #1029. A build
		// that honoured one and not the other would let the pool pass its maximum
		// by a direct write and not by a gameplay effect, which is worse than not
		// building the option at all.
		SetClassResource(GetClassResourceUncapped() > 0.0f
			? FMath::Max(0.0f, GetClassResource())
			: FMath::Clamp(GetClassResource(), 0.0f, GetMaxClassResource()));
	}
}

bool UCataclysmClassResourceAttributeSet::PoolIsUncapped(
	const UAbilitySystemComponent* AbilitySystem)
{
	// ASKED OF THE COMPONENT RATHER THAN OF A SET THE CALLER FOUND, so the third
	// clamp site -- `UCataclysmFervour::Move`, which is not in this class -- asks
	// the same question in the same words. Issue #1029.
	//
	// NO CLASS RESOURCE SET MEANS CAPPED, which is every enemy in the game and
	// every test that builds a component without one. That is the safe direction:
	// a character with no pool cannot have an uncapped one.
	if (!AbilitySystem)
	{
		return false;
	}
	const UCataclysmClassResourceAttributeSet* Set =
		AbilitySystem->GetSet<UCataclysmClassResourceAttributeSet>();
	return Set && Set->GetClassResourceUncapped() > 0.0f;
}

TArray<FGameplayAttribute> UCataclysmClassResourceAttributeSet::GetAllAttributes()
{
	TArray<FGameplayAttribute> All = {
		GetClassResourceAttribute(), GetMaxClassResourceAttribute()
	};
	All.Append(GetRateAttributes());

	// THE ADDED HEALTH COST IS NOT A RATE, so it is listed here rather than in
	// GetRateAttributes below. That function answers "can this character move
	// Fervour at all", which is what decides whether the bar is drawn, and a
	// character that pays health for its skills but converts none of it to
	// Fervour should not be given a bar it can only read zero from. Issue #970.
	All.Add(GetAddedHealthCostAttribute());
	All.Add(GetAddedHealthCostOfCurrentAttribute());
	All.Add(GetDeferredHealthCostShareAttribute());
	All.Add(GetHealthOwedAttribute());
	All.Add(GetHealthDebtDelayExtensionAttribute());
	All.Add(GetHealthDebtClearedOnlyByAKillAttribute());
	All.Add(GetFervourLossSuppressedAttribute());
	All.Add(GetFervourPerSecondAttribute());
	All.Add(GetDamageToBleedingOnLowHealthAttribute());
	All.Add(GetDamageToBleedingWindowAttribute());
	All.Add(GetClassResourceUncappedAttribute());
	return All;
}

TArray<FGameplayAttribute> UCataclysmClassResourceAttributeSet::GetRateAttributes()
{
	return { GetFervourFromDamageAttribute(), GetFervourFromCostAttribute(),
			 GetFervourLostToHealingAttribute() };
}

CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, ClassResource)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, MaxClassResource)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, FervourFromDamage)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, FervourFromCost)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, FervourLostToHealing)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, AddedHealthCost)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, AddedHealthCostOfCurrent)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, DeferredHealthCostShare)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, HealthOwed)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, HealthDebtDelayExtension)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, HealthDebtClearedOnlyByAKill)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, FervourLossSuppressed)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, FervourPerSecond)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, DamageToBleedingOnLowHealth)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, DamageToBleedingWindow)
CATACLYSM_ON_REP(UCataclysmClassResourceAttributeSet, ClassResourceUncapped)
