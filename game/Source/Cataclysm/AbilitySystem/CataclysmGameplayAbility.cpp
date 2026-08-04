// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"

namespace CataclysmAbilitySlots
{
	namespace
	{
		/**
		 * The seven slots and the tag name each one carries, in the order the
		 * design document's control table lists them.
		 *
		 * The names must match the Slot.* rows of the generated tag list exactly.
		 * They are requested by name rather than declared as native tags on
		 * purpose: a native tag declaration would create a second definition of
		 * the tag that exists whether or not the workbook still lists it, which
		 * would hide precisely the disagreement this table is meant to expose.
		 */
		struct FSlotTagName
		{
			ECataclysmAbilitySlot Slot;
			const TCHAR* TagName;
		};

		constexpr FSlotTagName SlotTagNames[] = {
			{ ECataclysmAbilitySlot::BasicAttack, TEXT("Slot.Basic")     },
			{ ECataclysmAbilitySlot::Heavy,       TEXT("Slot.Heavy")     },
			{ ECataclysmAbilitySlot::Special,     TEXT("Slot.Special")   },
			{ ECataclysmAbilitySlot::Support,     TEXT("Slot.Support")   },
			{ ECataclysmAbilitySlot::Aura,        TEXT("Slot.Aura")      },
			{ ECataclysmAbilitySlot::Ultimate,    TEXT("Slot.Ultimate")  },
			{ ECataclysmAbilitySlot::Movement,    TEXT("Slot.Movement")  },
		};
	}

	TArrayView<const ECataclysmAbilitySlot> All()
	{
		// Built once from the table above so the two cannot disagree about which
		// slots exist, which they could if this were a second hand-written list.
		static const TArray<ECataclysmAbilitySlot> Slots = []
		{
			TArray<ECataclysmAbilitySlot> Result;
			Result.Reserve(UE_ARRAY_COUNT(SlotTagNames));
			for (const FSlotTagName& Entry : SlotTagNames)
			{
				Result.Add(Entry.Slot);
			}
			return Result;
		}();

		return Slots;
	}

	FGameplayTag Tag(ECataclysmAbilitySlot Slot)
	{
		if (Slot == ECataclysmAbilitySlot::None)
		{
			return FGameplayTag();
		}

		for (const FSlotTagName& Entry : SlotTagNames)
		{
			if (Entry.Slot == Slot)
			{
				// ErrorIfNotFound is false because a missing tag is a condition
				// the test reports clearly; the engine's own error would fire
				// during startup with no indication of which slot caused it.
				return UGameplayTagsManager::Get().RequestGameplayTag(
					FName(Entry.TagName), /*ErrorIfNotFound=*/false);
			}
		}

		return FGameplayTag();
	}
}

UCataclysmGameplayAbility::UCataclysmGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// Abilities run on the server and the result replicates. The alternative,
	// LocalPredicted, is worth adopting per-ability later for responsiveness,
	// but it requires prediction keys to be handled correctly and is not a
	// sensible default to start from.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
}

void UCataclysmGameplayAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo,
											const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);

	if (bActivateOnGranted && ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle, /*bAllowRemoteActivation=*/false);
	}
}
