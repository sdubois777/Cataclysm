// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"

UCataclysmAbilitySystemComponent::UCataclysmAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
}

int32 UCataclysmAbilitySystemComponent::DisplacementsInWindow() const
{
	const UWorld* World = GetWorld();
	if (!World || LastDisplacedAtSeconds < 0.0f)
	{
		return 0;
	}

	// REPORTED RATHER THAN STORED, so asking does not reset anything. The count
	// held in the field is only meaningful inside the window; outside it the
	// answer is zero, and the field is corrected on the next displacement.
	const float Since = World->GetTimeSeconds() - LastDisplacedAtSeconds;
	return Since > UCataclysmSkillEffects::StunImmunityWindowSeconds
		? 0
		: DisplacementCount;
}

float UCataclysmAbilitySystemComponent::TakeNextDisplacementShare()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		// No world means no clock to measure a window against. The full distance
		// is the safe answer: it is what the skill asked for, and halving it
		// would silently shorten a shove in a context that cannot have had a
		// previous one.
		return 1.0f;
	}

	const float Now = World->GetTimeSeconds();

	// THE SAME 5 SECONDS THE STUN IMMUNITY WINDOW USES, read from that constant
	// rather than written again. The design says so in as many words: "It is the
	// stun immunity window, reused rather than a second number to remember." Two
	// copies of a number that measure different things which happen to be equal
	// are exactly the kind that drift with nothing noticing.
	const float Window = UCataclysmSkillEffects::StunImmunityWindowSeconds;

	if (LastDisplacedAtSeconds < 0.0f || Now - LastDisplacedAtSeconds > Window)
	{
		DisplacementCount = 0;
	}

	LastDisplacedAtSeconds = Now;

	// Full, then half, then a quarter. Capped so a target shoved a great many
	// times inside one window cannot shift the exponent past what a float holds;
	// by the thirtieth the distance is far below anything visible anyway.
	const int32 Halvings = FMath::Min(DisplacementCount, 30);
	++DisplacementCount;

	return 1.0f / static_cast<float>(1 << Halvings);
}

void UCataclysmAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		// HasTagExact, not HasTag. Slot.Heavy must not be matched by a press of
		// Slot, and a parent tag press must not fire every child. The slot names
		// are flat today, but the tag vocabulary is generated from the workbook
		// and a designer adding Slot.Heavy.Charged later would otherwise make one
		// key press activate two abilities.
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			InputPressedSpecHandles.AddUnique(Spec.Handle);
		}
	}
}

void UCataclysmAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			InputReleasedSpecHandles.AddUnique(Spec.Handle);
		}
	}
}

void UCataclysmAbilitySystemComponent::ProcessAbilityInput()
{
	TArray<FGameplayAbilitySpecHandle> ToActivate;
	ToActivate.Reserve(InputPressedSpecHandles.Num());

	for (const FGameplayAbilitySpecHandle& Handle : InputPressedSpecHandles)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		if (!Spec || !Spec->Ability)
		{
			continue;
		}

		Spec->InputPressed = true;

		if (Spec->IsActive())
		{
			// Already running. Tell it the key went down again rather than
			// starting a second copy, which is what an ability that reacts to a
			// second press while active -- a charge, a stance -- needs.
			AbilitySpecInputPressed(*Spec);
		}
		else
		{
			ToActivate.AddUnique(Handle);
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : ToActivate)
	{
		// Remote activation is left on, which is what makes this work off the
		// server. Abilities in this project default to ServerInitiated, and the
		// engine turns a client's TryActivateAbility on such an ability into a
		// server remote call rather than refusing it. With it off, every ability
		// press on a client would be silently dropped.
		TryActivateAbility(Handle, /*bAllowRemoteActivation=*/true);
	}

	for (const FGameplayAbilitySpecHandle& Handle : InputReleasedSpecHandles)
	{
		FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		if (!Spec || !Spec->Ability)
		{
			continue;
		}

		Spec->InputPressed = false;

		if (Spec->IsActive())
		{
			AbilitySpecInputReleased(*Spec);
		}
	}

	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

void UCataclysmAbilitySystemComponent::ClearAbilityInput()
{
	// Release anything currently held before dropping the record, so an ability
	// waiting on a key release is not left waiting forever.
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.InputPressed)
		{
			Spec.InputPressed = false;

			if (Spec.IsActive())
			{
				AbilitySpecInputReleased(Spec);
			}
		}
	}

	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

FGameplayAbilitySpecHandle UCataclysmAbilitySystemComponent::GiveAbilityInSlot(
	TSubclassOf<UGameplayAbility> AbilityClass,
	ECataclysmAbilitySlot Slot,
	int32 Level,
	UObject* SourceObject)
{
	if (!IsValid(AbilityClass))
	{
		return FGameplayAbilitySpecHandle();
	}

	// Granting is server-only, exactly as UCataclysmAbilitySet requires. On a
	// client this would appear to work and then have no effect, so it returns an
	// invalid handle the caller can notice instead.
	if (!IsOwnerActorAuthoritative())
	{
		return FGameplayAbilitySpecHandle();
	}

	const FGameplayTag SlotTag = CataclysmAbilitySlots::Tag(Slot);
	if (!SlotTag.IsValid())
	{
		// A slot of None has no key and no tag, so a granted ability would sit
		// there unreachable. Refusing is better than granting something no input
		// can ever reach.
		return FGameplayAbilitySpecHandle();
	}

	FGameplayAbilitySpec Spec(AbilityClass, Level);
	Spec.SourceObject = SourceObject;
	Spec.GetDynamicSpecSourceTags().AddTag(SlotTag);

	const FGameplayAbilitySpecHandle Handle = GiveAbility(Spec);

	// STAMPED ON THE INSTANCE, AND NOTHING DID THIS BEFORE. Adding the slot TAG
	// is what lets a key press find the ability; setting the slot PROPERTY is
	// what lets the ability find its own numbers. Issue #155 put the cooldown,
	// the mana cost and the damage multiplier in a table keyed by slot, and
	// UCataclysmGameplayAbility reads them from `Slot` -- which stayed at None
	// on every granted ability, so all three read as zero.
	//
	// Nothing reported it because a slot with no row logs a warning at Verbose
	// and returns zeros, and the only ability that existed was the placeholder,
	// which spends nothing and waits for nothing anyway.
	// Cataclysm.Skills.UsingASkillSpendsManaAndStartsItsCooldown fails without
	// this line.
	if (FGameplayAbilitySpec* Granted = FindAbilitySpecFromHandle(Handle))
	{
		if (UCataclysmGameplayAbility* Instance =
				Cast<UCataclysmGameplayAbility>(Granted->GetPrimaryInstance()))
		{
			Instance->Slot = Slot;
		}
		else if (UCataclysmGameplayAbility* Shared =
					Cast<UCataclysmGameplayAbility>(Granted->Ability))
		{
			// A non-instanced ability has no per-grant object to write to, so
			// this writes the class default and two grants into different slots
			// would fight. Every ability in this project is InstancedPerActor,
			// which is why that is a warning rather than a supported path.
			UE_LOG(LogCataclysm, Warning,
				TEXT("%s is not instanced, so its slot is being written on the "
					 "class default. Two weapons granting it into different "
					 "slots will disagree."), *Shared->GetName());
			Shared->Slot = Slot;
		}
	}

	return Handle;
}

// ==========================================================================
// The three-bucket stat pipeline's modifiers
// ==========================================================================

int32 UCataclysmAbilitySystemComponent::AddStatModifier(
	const FCataclysmStatModifier& Modifier)
{
	// REFUSED HERE RATHER THAN IGNORED AT EVALUATION TIME. Accumulate skips a
	// More multiplier from a source that may not grant one and counts it in
	// RejectedMoreCount, which is right for gear the player is wearing: the
	// character sheet can then say a modifier is doing nothing. A skill asking
	// for one it is not allowed is a mistake in the skill, and returning an
	// invalid handle is what makes it visible at the point it is made.
	const FString Refusal = UCataclysmStatPipeline::ValidateModifier(Modifier);
	if (!Refusal.IsEmpty())
	{
		UE_LOG(LogCataclysm, Warning,
			TEXT("%s refused a stat modifier: %s"),
			*GetNameSafe(GetOwner()), *Refusal);
		return 0;
	}

	const int32 Handle = NextStatModifierHandle++;
	StatModifiers.Add(Modifier);
	StatModifierHandles.Add(Handle);
	return Handle;
}

float UCataclysmAbilitySystemComponent::StatForSkill(
	FName Stat, const FGameplayTagContainer& SkillTags, float Fallback) const
{
	const FCataclysmStatInputs* Inputs = StatInputs.Find(Stat);
	if (!Inputs)
	{
		// NOTHING WAS RECORDED FOR THIS STAT, which is ordinary rather than a
		// fault: an enemy's ability system is never given a character stat line,
		// and a player's has none until the first refresh. The caller's own
		// attribute read is the right answer in both cases.
		return Fallback;
	}

	// THE WHOLE LIST THROUGH ONE PIPELINE PASS, rather than the scoped part
	// applied on top of a finished attribute. Increases have to sum into one
	// bracket: a base of 100 carrying an unscoped +50% and a scoped +50% is 200
	// through one pass and 225 through two. FCataclysmStatInputs quotes the
	// design's own words on it.
	return UCataclysmStatPipeline::Evaluate(Inputs->Base, Inputs->Modifiers,
											SkillTags).Final;
}

bool UCataclysmAbilitySystemComponent::RemoveStatModifier(int32 Handle)
{
	const int32 Index = StatModifierHandles.IndexOfByKey(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	// RemoveAt rather than RemoveAtSwap, so the two arrays stay aligned and the
	// order a character's modifiers were added in is the order they apply in.
	// Order does not change the arithmetic -- increases sum and More multipliers
	// commute -- but it does change what a breakdown reads like.
	StatModifiers.RemoveAt(Index);
	StatModifierHandles.RemoveAt(Index);
	return true;
}

bool UCataclysmAbilitySystemComponent::SetStatModifierValue(int32 Handle,
															float NewValue)
{
	const int32 Index = StatModifierHandles.IndexOfByKey(Handle);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	StatModifiers[Index].Value = NewValue;
	return true;
}

float UCataclysmAbilitySystemComponent::GetStatModifierValue(int32 Handle) const
{
	const int32 Index = StatModifierHandles.IndexOfByKey(Handle);
	return Index == INDEX_NONE ? 0.0f : StatModifiers[Index].Value;
}
