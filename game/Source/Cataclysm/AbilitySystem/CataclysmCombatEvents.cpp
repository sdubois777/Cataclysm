// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCombatEvents.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmCharacterBase.h"
#include "Engine/World.h"
#include "GameplayEffectExtension.h"

namespace
{
	/**
	 * The actor a blow was dealt by.
	 *
	 * THE CONTEXT'S SOURCE OBJECT WHEN IT IS AN ACTOR, which is how a minion is
	 * recorded -- `UCataclysmSkillEffects` puts `FCataclysmHitDelivery::DealtBy`
	 * there -- and otherwise the causer, which for every other blow in the game
	 * is the instigator itself. The causer is not changed for a minion because
	 * eight places in `UCataclysmVitalAttributeSet` read it to decide how a blow
	 * resolves, and slice 4 announces blows without changing how any resolves.
	 */
	AActor* CombatEventsDealtBy(const FGameplayEffectContextHandle& Context)
	{
		if (AActor* Source = Cast<AActor>(Context.GetSourceObject()))
		{
			return Source;
		}
		return Context.GetEffectCauser();
	}

	/**
	 * The skill a blow came from, read from the effect context's INSTANCE.
	 *
	 * NOT `GetAbility`, which returns the class default object the context
	 * also keeps. Every player skill is an instance of one of the eight
	 * template classes with its name and tags set on the instance, so the
	 * class default names no skill. See `FCataclysmHitDelivery::Skill`.
	 */
	const UCataclysmSkillTemplate* CombatEventsSkillOf(
		const FGameplayEffectContextHandle& Context)
	{
		return Cast<UCataclysmSkillTemplate>(Context.GetAbilityInstance_NotReplicated());
	}

	/** Metres between two actors, or -1 when either is missing. */
	float CombatEventsMetresBetween(const AActor* From, const AActor* To)
	{
		if (!From || !To)
		{
			return -1.0f;
		}
		return FVector::Dist(From->GetActorLocation(), To->GetActorLocation())
			/ 100.0f;
	}
}

bool FCataclysmHitNotice::HasTag(const FGameplayTag& Tag) const
{
	return (EffectTags && EffectTags->HasTag(Tag))
		|| (GrantedTags && GrantedTags->HasTag(Tag));
}

UCataclysmCombatEvents* UCataclysmCombatEvents::In(const UWorld* World)
{
	return World ? World->GetSubsystem<UCataclysmCombatEvents>() : nullptr;
}

void UCataclysmCombatEvents::NoteBlow(const FGameplayEffectModCallbackData& Data,
									  const FCataclysmIncomingHit& Hit,
									  const FCataclysmDamageResult& Outcome,
									  const FGameplayTagContainer& EffectTags,
									  bool bLethal)
{
	UAbilitySystemComponent* TargetSystem = &Data.Target;

	// THE AVATAR, NOT THE OWNER, for the reason the attribute set gives at every
	// such lookup: the player's ability system is owned by the player state,
	// which is not a character and has no location.
	AActor* Target = TargetSystem->GetAvatarActor();
	if (!Target)
	{
		return;
	}

	UWorld* World = Target->GetWorld();
	UCataclysmCombatEvents* Events = In(World);
	const bool bListening = Events && Events->OnHit.IsBound();
	const bool bReachedHealth = Outcome.DealtToHealth > 0.0f;

	// NOTHING TO DO FOR MOST BLOWS IN A WORLD NOBODY LISTENS TO. A blow that did
	// not reach health cannot be the one that killed, so it leaves no record,
	// and with no listener it is announced to nobody.
	if (!bReachedHealth && !bListening)
	{
		return;
	}

	const FGameplayEffectContextHandle& Context = Data.EffectSpec.GetContext();
	AActor* Attacker = Context.GetInstigator();
	AActor* DealtBy = CombatEventsDealtBy(Context);

	// WHAT A DAMAGE-OVER-TIME TICK GRANTS IS WHERE ITS AILMENT IS, and gathering
	// it costs a container, so it is gathered only for a tick that is either
	// listened for or lethal. See `FCataclysmHitNotice::GrantedTags`.
	FGameplayTagContainer Granted;
	if (Hit.bIsDamageOverTime && (bListening || bLethal))
	{
		Data.EffectSpec.GetAllGrantedTags(Granted);
	}

	if (bReachedHealth)
	{
		if (UCataclysmAbilitySystemComponent* Cataclysm =
				Cast<UCataclysmAbilitySystemComponent>(TargetSystem))
		{
			FCataclysmLastBlow Blow;
			Blow.Attacker = Attacker;
			Blow.DealtBy = DealtBy;
			Blow.Skill = Context.GetAbilityInstance_NotReplicated();
			Blow.bDamageOverTime = Hit.bIsDamageOverTime;
			Blow.WorldSeconds = World ? World->GetTimeSeconds() : 0.0;

			// THE TAGS ONLY FOR THE BLOW THAT KILLS. See `FCataclysmLastBlow`.
			if (bLethal)
			{
				Blow.KillingTags = EffectTags;
				Blow.KillingTags.AppendTags(Granted);
			}

			Cataclysm->RecordLastBlow(MoveTemp(Blow));
		}
	}

	if (!bListening)
	{
		return;
	}

	FCataclysmHitNotice Notice;
	Notice.Attacker = Attacker;
	Notice.DealtBy = DealtBy;
	Notice.Target = Target;
	if (const UCataclysmSkillTemplate* Skill = CombatEventsSkillOf(Context))
	{
		Notice.SkillName = FName(*Skill->SkillName);
		Notice.SkillTags = &Skill->SkillTags;
	}
	Notice.Landed = Outcome.AbsorbedByMana + Outcome.AbsorbedByShield
		+ Outcome.DealtToHealth;
	Notice.DealtToHealth = Outcome.DealtToHealth;
	Notice.bCritical = Outcome.bWasCritical;
	Notice.bBlocked = Outcome.bBlocked;
	Notice.bEvaded = Outcome.bEvaded;
	Notice.bDamageOverTime = Hit.bIsDamageOverTime;
	Notice.bArea = Hit.bIsArea;
	Notice.DistanceMetres = CombatEventsMetresBetween(DealtBy, Target);
	Notice.Location = Target->GetActorLocation();
	Notice.EffectTags = &EffectTags;
	Notice.GrantedTags = Hit.bIsDamageOverTime ? &Granted : nullptr;

	++Events->Hits;
	Events->OnHit.Broadcast(Notice);
}

void UCataclysmCombatEvents::NoteDeath(AActor* Victim)
{
	UWorld* World = Victim ? Victim->GetWorld() : nullptr;
	UCataclysmCombatEvents* Events = In(World);
	if (!Events || !Events->OnDeath.IsBound())
	{
		return;
	}

	// NEVER NULL, SO NO LISTENER HAS TO ASK. A death with no blow on record points
	// here, at nothing.
	static const FGameplayTagContainer NoTags;

	FCataclysmDeathNotice Notice;
	Notice.Victim = Victim;
	Notice.Location = Victim->GetActorLocation();
	Notice.KillingTags = &NoTags;

	const UCataclysmAbilitySystemComponent* System =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Victim));
	if (System && System->GetLastBlow().IsOnRecord())
	{
		const FCataclysmLastBlow& Blow = System->GetLastBlow();
		Notice.Killer = Blow.Attacker.Get();
		Notice.KillingCauser = Blow.DealtBy.Get();
		if (const UCataclysmSkillTemplate* Skill =
				Cast<UCataclysmSkillTemplate>(Blow.Skill.Get()))
		{
			Notice.KillingSkillName = FName(*Skill->SkillName);
			Notice.KillingSkillTags = &Skill->SkillTags;
		}
		Notice.bByDamageOverTime = Blow.bDamageOverTime;
		Notice.SecondsSinceLastBlow =
			static_cast<float>(World->GetTimeSeconds() - Blow.WorldSeconds);
		Notice.KillingTags = &Blow.KillingTags;
	}

	++Events->Deaths;
	Events->OnDeath.Broadcast(Notice);
}

void UCataclysmCombatEvents::NoteSkillUsed(AActor* User, const FString& SkillName,
										   const FGameplayTagContainer& SkillTags,
										   ECataclysmAbilitySlot Slot)
{
	UCataclysmCombatEvents* Events = User ? In(User->GetWorld()) : nullptr;
	if (!Events || !Events->OnSkillUsed.IsBound())
	{
		return;
	}

	FCataclysmSkillUsedNotice Notice;
	Notice.User = User;
	Notice.SkillName = FName(*SkillName);
	Notice.SkillTags = &SkillTags;
	Notice.Slot = Slot;
	Notice.Location = User->GetActorLocation();

	++Events->SkillUses;
	Events->OnSkillUsed.Broadcast(Notice);
}

void UCataclysmCombatEvents::NoteCreatureAbility(ACataclysmCharacterBase* Creature,
												 int32 AbilityIndex)
{
	UCataclysmCombatEvents* Events = Creature ? In(Creature->GetWorld()) : nullptr;
	if (!Events || !Events->OnSkillUsed.IsBound())
	{
		return;
	}

	FCataclysmSkillUsedNotice Notice;
	Notice.User = Creature;
	Notice.Location = Creature->GetActorLocation();

	// ASKED FOR ONLY WHEN SOMETHING LISTENS, because `EnemyAbilities` answers
	// with a fresh array. A creature's ability has a name and no tags, so the
	// name is all there is to carry.
	const TArray<FCataclysmEnemyAbility> Abilities = Creature->EnemyAbilities();
	if (Abilities.IsValidIndex(AbilityIndex))
	{
		Notice.SkillName = Abilities[AbilityIndex].Name;
	}

	++Events->SkillUses;
	Events->OnSkillUsed.Broadcast(Notice);
}
