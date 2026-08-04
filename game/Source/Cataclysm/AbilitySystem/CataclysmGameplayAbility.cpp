// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectTypes.h"
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

// --------------------------------------------------------------------------
// What an ability waits and what it costs. Issue #155.
//
// Both come from the slot rather than from the ability, because no designed
// skill states either one. An ability may still override, which is what a skill
// that differs from its slot would do.
// --------------------------------------------------------------------------

void UCataclysmGameplayAbility::EnsureSlotNumbersLoaded() const
{
	if (bSlotNumbersLoaded)
	{
		return;
	}
	bSlotNumbersLoaded = true;

	const UDataTable* Table = UCataclysmSkillSlots::LoadGeneratedTable();
	const FCataclysmSkillSlotNumbers Numbers =
		UCataclysmSkillSlots::NumbersFor(Table, Slot);

	if (!Numbers.bFound)
	{
		// Not fatal, and deliberately not silent. An ability whose slot has no
		// row costs nothing and waits for nothing, which is exactly the state
		// issue #155 was about, so it has to be visible.
		UE_LOG(LogCataclysm, Warning,
			TEXT("%s is in slot %d, which has no row in the skill slot table. "
				 "It will cost nothing and have no cooldown."),
			*GetName(), static_cast<int32>(Slot));
		return;
	}

	SlotCooldown = Numbers.Cooldown;
	SlotManaCostAtLevel100 = Numbers.ManaCostAtLevel100;
}

float UCataclysmGameplayAbility::GetBaseCooldown() const
{
	if (CooldownOverride >= 0.0f)
	{
		return CooldownOverride;
	}
	EnsureSlotNumbersLoaded();
	return SlotCooldown;
}

float UCataclysmGameplayAbility::GetManaCost() const
{
	const float AtLevel100 = [this]
	{
		if (ManaCostOverride >= 0.0f)
		{
			return ManaCostOverride;
		}
		EnsureSlotNumbersLoaded();
		return SlotManaCostAtLevel100;
	}();

	// GAS's ability level is this project's character level: the weapon slots
	// component grants each skill at the character's level.
	return UCataclysmSkillSlots::ManaCostAtLevel(AtLevel100, GetAbilityLevel());
}

bool UCataclysmGameplayAbility::CheckCost(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}

	const float Cost = GetManaCost();
	if (Cost <= 0.0f)
	{
		return true;
	}

	const UAbilitySystemComponent* AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		return false;
	}

	return AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetManaAttribute()) >= Cost;
}

void UCataclysmGameplayAbility::ApplyCost(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	const float Cost = GetManaCost();
	if (Cost <= 0.0f)
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	// Applied directly rather than through a Gameplay Effect asset. There is no
	// authored asset per slot to carry a magnitude that comes from a generated
	// table, and an effect built at runtime for every activation would allocate
	// on every button press. When enchantments that change a skill's mana cost
	// are built -- four of them exist in the data -- this is where they hook in.
	AbilitySystem->ApplyModToAttribute(
		UCataclysmVitalAttributeSet::GetManaAttribute(),
		EGameplayModOp::Additive, -Cost);
}

bool UCataclysmGameplayAbility::CheckCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
	if (!Tag.IsValid() || GetBaseCooldown() <= 0.0f)
	{
		// No cooldown at all. True for the Basic Attack, which is automatic, and
		// the Aura, which is a toggle.
		return true;
	}

	const UAbilitySystemComponent* AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		return false;
	}

	if (AbilitySystem->HasMatchingGameplayTag(Tag))
	{
		if (OptionalRelevantTags)
		{
			OptionalRelevantTags->AddTag(Tag);
		}
		return false;
	}
	return true;
}

void UCataclysmGameplayAbility::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	const float Seconds = GetBaseCooldown();
	const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
	if (Seconds <= 0.0f || !Tag.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	// A duration effect that grants the slot's cooldown tag and nothing else.
	// Built here rather than authored as an asset for the same reason as the
	// cost: the duration comes from a generated table, and there is no asset per
	// slot to put it in.
	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		GetTransientPackage(), FName(*FString::Printf(TEXT("Cooldown_%s"), *Tag.ToString())));
	Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	Effect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Seconds));

	UTargetTagsGameplayEffectComponent& TagsComponent =
		Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
	FInheritedTagContainer Granted;
	Granted.Added.AddTag(Tag);
	TagsComponent.SetAndApplyTargetTagChanges(Granted);

	AbilitySystem->ApplyGameplayEffectToSelf(
		Effect, /*Level=*/1.0f, AbilitySystem->MakeEffectContext());
}
