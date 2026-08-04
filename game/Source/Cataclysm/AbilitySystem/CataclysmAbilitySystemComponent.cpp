// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "GameplayTagContainer.h"

UCataclysmAbilitySystemComponent::UCataclysmAbilitySystemComponent()
{
	SetIsReplicatedByDefault(true);
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
