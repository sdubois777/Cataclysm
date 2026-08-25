// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGameplayAbility.h"
// For asking what a stat is worth with the character's own state in hand,
// rather than reading a gameplay attribute that is zero by design. Issue #973.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"

/**
 * Pins the roll that decides whether a skill skips its cooldown. Issue #973.
 *
 * THE SAME SHAPE AS `Cataclysm.CritRoll` AND FOR THE SAME REASON. A test that
 * asserted a skill did or did not go on cooldown would otherwise pass most of
 * the time and fail the rest, which is worse than failing outright.
 *
 * -1, the default, rolls normally. 0 always skips when the character has any
 * chance at all, because every chance above zero beats it. 100 never skips,
 * because the comparison is strictly less than.
 *
 * IT IS ALSO USEFUL AT THE KEYBOARD, for watching what a character with the
 * Masochist's The Catalyst node feels like without waiting on the dice.
 */
static TAutoConsoleVariable<float> CVarCooldownSkipRoll(
	TEXT("Cataclysm.CooldownSkipRoll"),
	-1.0f,
	TEXT("Pins the roll deciding whether a skill skips its cooldown, 0-100. "
		 "-1 rolls normally. 0 always skips for a character with any chance; "
		 "100 never skips."),
	ECVF_Default);

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
	SlotManaOnHitAtLevel100 = Numbers.ManaOnHitAtLevel100;
}

FString UCataclysmGameplayAbility::DisplayedName() const
{
	// NOTHING, RATHER THAN THE CLASS NAME. A box on the skill bar reading
	// "CataclysmUndesignedSkill_C" would be worse than one reading "Special",
	// and the caller is the one that knows which slot it is asking about.
	return FString();
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

float UCataclysmGameplayAbility::GetManaOnHit() const
{
	// NO OVERRIDE PROPERTY, UNLIKE THE COST AND THE COOLDOWN. Those two exist
	// because a designed skill can differ from its slot; mana on hit belongs to
	// the basic attack alone, and the basic attack has no row of its own to
	// differ in -- it comes from the weapon, not from the skill matrix. One
	// would be a knob nothing could turn.
	EnsureSlotNumbersLoaded();
	return UCataclysmSkillSlots::ManaOnHitAtLevel(SlotManaOnHitAtLevel100,
												  GetAbilityLevel());
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

float UCataclysmGameplayAbility::CooldownAfterReduction(
	const UAbilitySystemComponent* AbilitySystem, float BaseCooldown)
{
	const FGameplayAttribute Reduction =
		UCataclysmCombatAttributeSet::GetCooldownReductionAttribute();
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Reduction))
	{
		return BaseCooldown;
	}

	// A PERCENTAGE BECOMES A FRACTION HERE. The attribute holds 12 for a 12%
	// affix and FinalCooldown wants 0.12, and this is the only place the two
	// meet.
	//
	// NO "MORE" MULTIPLIER YET. Gems, passive nodes and enchantments are the
	// only sources the design allows one from and none of them reaches an
	// ability today, so 1.0 is the honest answer rather than a placeholder.
	return UCataclysmCombatAttributeSet::FinalCooldown(
		BaseCooldown, AbilitySystem->GetNumericAttribute(Reduction) / 100.0f);
}

bool UCataclysmGameplayAbility::CooldownIsSkipped(
	const UAbilitySystemComponent* AbilitySystem) const
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		// An ability system this project did not make carries no stat line, so
		// it has no chance to skip anything. An enemy's abilities come through
		// here too.
		return false;
	}

	// ASKED FOR, NOT READ. Issue #973. The only source of this stat is a passive
	// node carrying a health condition, so the gameplay attribute holds zero at
	// all times by design and reading it would find the node doing nothing.
	// `StatForSkill` runs the pipeline again with the character's health in hand.
	//
	// AN EMPTY TAG CONTAINER, AND THAT IS HONEST RATHER THAN LAZY. A skill's
	// tags live on `UCataclysmSkillTemplate::SkillTags`, which is a subclass of
	// this one, and an enemy's C++ ability has none at all -- so this function
	// has no tags it can truthfully supply. Empty means every unscoped modifier
	// applies, which is every source this stat has. A node that scoped it to
	// some skills would need the tags threaded here first, and would not work
	// silently in the meantime: it would simply not apply.
	const float Chance = Cataclysm->StatForSkill(
		FName(TEXT("cooldown_skip_chance")), FGameplayTagContainer(),
		/*Fallback=*/0.0f);
	if (Chance <= 0.0f)
	{
		// NO ROLL AT ALL FOR A CHARACTER WITH NO CHANCE, which is every
		// character in the game that has not spent a point on that node. This is
		// on the path of every skill use, and a roll nobody can win is waste.
		return false;
	}

	// PINNED BY A CONSOLE VARIABLE WHEN ONE IS SET, the same way the critical
	// strike roll is and for the same reason: a test asserting that a skill did
	// or did not go on cooldown would otherwise pass most of the time and fail
	// the rest, which is worse than failing.
	const float Pinned = CVarCooldownSkipRoll.GetValueOnAnyThread();
	const float Roll = Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);

	// STRICTLY LESS THAN, so a roll of 100 never skips and a chance of 0 never
	// does either. The critical strike roll reads the same way.
	return Roll < Chance;
}

void UCataclysmGameplayAbility::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	const FGameplayTag Tag = UCataclysmSkillSlots::CooldownTag(Slot);
	if (GetBaseCooldown() <= 0.0f || !Tag.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AbilitySystem)
	{
		return;
	}

	// AND THE CHANCE THE SKILL DOES NOT GO ON COOLDOWN AT ALL. Issue #973. The
	// Masochist's The Catalyst node: "While at or below 5% health, your skills
	// have a 5% chance per point not to go on cooldown."
	//
	// BEFORE THE LENGTH IS WORKED OUT, because a cooldown that does not happen
	// has no length. Everything below this line builds and applies the effect.
	if (CooldownIsSkipped(AbilitySystem))
	{
		return;
	}

	// THE CHARACTER'S COOLDOWN REDUCTION, AND UNTIL ISSUE #895 THIS WAS THE BASE
	// LENGTH. UCataclysmCombatAttributeSet::FinalCooldown was written,
	// documented and tested, and nothing called it, so every cooldown in the
	// game waited its full time however much reduction the player was wearing.
	const float Seconds =
		CooldownAfterReduction(AbilitySystem, GetBaseCooldown());
	if (Seconds <= 0.0f)
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
