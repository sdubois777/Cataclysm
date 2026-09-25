// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSecondSelf.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmCharacterBase.h"
#include "Items/CataclysmItem.h"

const TCHAR* UCataclysmSecondSelf::Stat = TEXT("minion_held_longest_becomes_your_equal");

namespace
{
	bool Holds(const AActor* Commander)
	{
		const UCataclysmAbilitySystemComponent* Theirs =
			Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Commander));
		const FName Held(UCataclysmSecondSelf::Stat);
		return Theirs && Theirs->StatForSkill(Held, FGameplayTagContainer(), 0.0f) > 0.0f;
	}

	/** A minion or a thrall, and not a deployable machine. */
	bool MayBeChosen(const AActor* Follower)
	{
		const ACataclysmMinion* Minion = Cast<ACataclysmMinion>(Follower);
		return Cast<ACataclysmCharacterBase>(Follower) && !(Minion && Minion->bIsMachine);
	}
}

AActor* UCataclysmSecondSelf::Choose(const AActor* Commander)
{
	const TArray<AActor*> Followers = UCataclysmCommand::ThingsCommandedBy(Commander);

	// ONE ALREADY CHOSEN STAYS CHOSEN while it stands: "when it is gone, the next
	// longest-held takes its place", and only then.
	for (AActor* Follower : Followers)
	{
		const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower);
		if (Body && Body->bIsSecondSelf && MayBeChosen(Follower))
		{
			return Follower;
		}
	}

	// THE EARLIEST STAMP. `ThingsCommandedBy` answers nearest first, so keeping
	// only a strictly earlier one gives a tie to the nearer. One never stamped
	// came under command by a route that does not stamp, and counts as newest.
	AActor* Longest = nullptr;
	float Earliest = TNumericLimits<float>::Max();
	for (AActor* Follower : Followers)
	{
		if (!MayBeChosen(Follower))
		{
			continue;
		}
		const float Since = Cast<ACataclysmCharacterBase>(Follower)->CommandedSinceSeconds;
		const float Held = Since >= 0.0f ? Since : TNumericLimits<float>::Max() * 0.5f;
		if (!Longest || Held < Earliest)
		{
			Longest = Follower;
			Earliest = Held;
		}
	}
	return Longest;
}

AActor* UCataclysmSecondSelf::Step(AActor* Commander)
{
	if (!Holds(Commander))
	{
		return nullptr;
	}
	AActor* Chosen = Choose(Commander);
	ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Chosen);
	UAbilitySystemComponent* Its = UCataclysmTargeting::AbilitySystemOf(Chosen);
	const UAbilitySystemComponent* Mine = UCataclysmTargeting::AbilitySystemOf(Commander);
	if (!Body || !Its || !Mine)
	{
		return nullptr;
	}
	Body->bIsSecondSelf = true;

	// YOUR MAXIMUM HEALTH, LIVE, AND THE SAME SHARE OF IT. Written every step,
	// so a creature's own setters rewriting its maximum are overruled within one.
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float Yours = Mine->GetNumericAttribute(MaxHealth);
	const float Was = Its->GetNumericAttribute(MaxHealth);
	if (Yours > 0.0f && !FMath::IsNearlyEqual(Yours, Was, 0.01f))
	{
		const float Share = Was > 0.0f
			? FMath::Clamp(Its->GetNumericAttribute(Health) / Was, 0.0f, 1.0f)
			: 1.0f;
		Its->SetNumericAttributeBase(MaxHealth, Yours);
		Its->SetNumericAttributeBase(Health, Yours * Share);
	}

	// YOUR SPELL DAMAGE, on its own attribute where it has one. An imp has no
	// combat set, so for an imp this does nothing, as the header says.
	if (Its->GetSet<UCataclysmCombatAttributeSet>() && Mine->GetSet<UCataclysmCombatAttributeSet>())
	{
		const FGameplayAttribute Spell = UCataclysmCombatAttributeSet::GetSpellDamageAttribute();
		Its->SetNumericAttributeBase(Spell, Mine->GetNumericAttribute(Spell));
	}
	return Chosen;
}

bool UCataclysmSecondSelf::IsSecondSelf(const AActor* Follower)
{
	const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower);
	return Body && Body->bIsSecondSelf && !UCataclysmSkillEffects::IsDead(Body)
		&& Holds(UCataclysmCommand::CommanderOf(Follower));
}

float UCataclysmSecondSelf::AreaMultiplierFor(const AActor* Follower)
{
	// NOT `IsSecondSelf`, WHICH ASKS FOR ONE STILL STANDING. A death blast goes
	// off after the minion has died, and its area is still its summoner's.
	const ACataclysmCharacterBase* Body = Cast<ACataclysmCharacterBase>(Follower);
	if (!Body || !Body->bIsSecondSelf || !Holds(UCataclysmCommand::CommanderOf(Follower)))
	{
		return 1.0f;
	}
	// THE SAME MULTIPLIER A PLAYER'S AREA SKILL USES, asked with no skill's tags.
	return UCataclysmSkillEffects::AsMultiplierForSkill(
		UCataclysmTargeting::AbilitySystemOf(UCataclysmCommand::CommanderOf(Follower)),
		UCataclysmCombatAttributeSet::GetAreaOfEffectAttribute(),
		FName(UCataclysmItemModifiers::AreaOfEffectStat), FGameplayTagContainer());
}

int32 UCataclysmSecondSelf::ExtraThrallShares(const AActor* Commander)
{
	if (!Holds(Commander))
	{
		return 0;
	}
	// NONE YET MEANS THE NEXT ONE TAKEN IS CHOSEN, and claims its second share
	// the moment it is taken.
	const AActor* Chosen = Choose(Commander);
	return !Chosen || !Cast<ACataclysmMinion>(Chosen) ? 1 : 0;
}
