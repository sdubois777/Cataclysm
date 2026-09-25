// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGameplayAbility.h"
// For asking what a stat is worth with the character's own state in hand,
// rather than reading a gameplay attribute that is zero by design. Issue #973.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
// For the flag saying this character pays with health where others pay with
// mana. Issue #1067.
#include "AbilitySystem/CataclysmSkillTemplate.h"
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

const FGameplayTagContainer& UCataclysmGameplayAbility::SkillTagsForStats() const
{
	// NONE HERE, AND THE ONE SHARED EMPTY CONTAINER rather than a local, because
	// this hands back a reference. `UCataclysmSkillTemplate` overrides it with the
	// skill's own tags; an enemy's C++ ability has none and keeps this.
	return FGameplayTagContainer::EmptyContainer;
}

float UCataclysmGameplayAbility::ManaCostFor(
	const UAbilitySystemComponent* AbilitySystem) const
{
	const float Base = GetManaCost();
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);

	// THE Nth SPELL OF A WORN "EVERY Nth SPELL" ROW ADDS A SHARE OF THE MANA
	// HELD, on top of whatever the cost comes to below. Issue #1833, phase 2,
	// ruled 2026-09-24: PLUS rather than instead, because "instead" would make
	// the Nth cast cheaper whenever that share is below the normal cost. Asked
	// here, before the cast is paid for, because the count advances after. A
	// free spell pays the share too.
	const float Extra = Cataclysm && UCataclysmSkillEffects::IsSpell(SkillTagsForStats())
		? Cataclysm->NthSpellExtraManaPercent() / 100.0f
			* Cataclysm->GetNumericAttribute(UCataclysmVitalAttributeSet::GetManaAttribute())
		: 0.0f;

	if (Base <= 0.0f)
	{
		// NOTHING TO SCALE. The Basic Attack and the Aura's activation are free,
		// and a stat cannot make a free skill cost something: every row in the
		// data reduces a cost or takes it away.
		return Base + Extra;
	}

	if (!Cataclysm)
	{
		// An ability system this project did not make carries no stat line, so it
		// pays the skill's own cost. An enemy's abilities come through here too.
		return Base;
	}

	// THE SKILL'S OWN COST AS THE BASE, so the three buckets do the work: a More
	// multiplier below zero is "less mana", one above is "more mana", and the
	// removal kind of issue #1791 is "costs no mana".
	//
	// `StatAppliedTo` AND NOT `StatForSkill`, AND THE DIFFERENCE IS THE WHOLE
	// READING. `StatForSkill` takes its third argument as a FALLBACK for a
	// character with no row and otherwise runs the pipeline on the recorded
	// line's base, which for a stat with no attribute is zero -- so it answered
	// zero for every character that had a row at all. Four tests and a probe
	// caught it on 2026-09-17.
	//
	// WITH THE SKILL'S TAGS, which is what scopes "Your spells cost 10%-20% less
	// mana" to spells, and with the caster's current conditions, which is what
	// lets "while standing still" be judged as the cost is asked.
	const float Asked = Cataclysm->StatAppliedTo(
		FName(UCataclysmSkillSlots::ManaCostStat), SkillTagsForStats(), Base);

	// NEVER BELOW ZERO. A Less multiplier stops at -99%, so it cannot get here,
	// but a negative flat row could, and a cost below zero would pay a character
	// for casting.
	return FMath::Max(0.0f, Asked) + Extra;
}

const TCHAR* UCataclysmGameplayAbility::ManaCostAsCurrentHealthPercentStat =
	TEXT("mana_cost_as_current_health_percent");

float UCataclysmGameplayAbility::ManaCostPaidAsHealthPercent(
	const UAbilitySystemComponent* AbilitySystem) const
{
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Cataclysm)
	{
		// AN ENEMY, OR ANY ABILITY SYSTEM WITH NO STAT LINE, pays as it always has.
		return 0.0f;
	}

	// ONLY A CAST THAT WOULD HAVE TAKEN MANA. See the declaration: the pool must
	// be mana, and the cost after this character's reductions must be above
	// nothing. The basic attack has no mana cost, so it never reaches the stat.
	if (CostPool(AbilitySystem) != UCataclysmVitalAttributeSet::GetManaAttribute()
		|| ManaCostFor(AbilitySystem) <= 0.0f)
	{
		return 0.0f;
	}

	// WITH THE SKILL'S TAGS AND THE CHARACTER'S CURRENT CONDITIONS, which is what
	// judges `mana_below` against the mana in hand now. A fallback of nothing, so
	// a character carrying no such row pays exactly as before.
	return FMath::Max(0.0f,
					  Cataclysm->StatForSkill(FName(ManaCostAsCurrentHealthPercentStat),
											  SkillTagsForStats(), 0.0f));
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

FGameplayAttribute UCataclysmGameplayAbility::CostPool(
	const UAbilitySystemComponent* AbilitySystem)
{
	// OUT OF HEALTH FOR A CHARACTER THAT TRADED ITS MANA POOL FOR ONE. Issue
	// #1067. The Masochist's Water to Blood: "every ability costs health instead
	// of mana."
	//
	// THE SAME NUMBER OUT OF A DIFFERENT POOL. The option converts the pool, not
	// the price, so a skill that cost 40 mana costs 40 health.
	return UCataclysmSkillTemplate::ManaPoolBecomesHealth(AbilitySystem)
		? UCataclysmVitalAttributeSet::GetHealthAttribute()
		: UCataclysmVitalAttributeSet::GetManaAttribute();
}

bool UCataclysmGameplayAbility::PoolCovers(
	const UAbilitySystemComponent* AbilitySystem, const FGameplayAttribute& Pool,
	float Cost)
{
	if (!AbilitySystem)
	{
		return false;
	}

	// STRICTLY MORE THAN FOR HEALTH, WHERE MANA ASKS FOR AT LEAST. A cost that
	// took a character to exactly zero health would kill it, and no skill should
	// be able to do that by being paid for.
	//
	// ROCK BOTTOM DOES NOT REACH THIS COST. That option turns an unpayable health
	// cost into debt, and `UCataclysmSkillTemplate::PayHealthCost` applies it to
	// the added health costs it charges; this cost is the mana cost moved onto
	// health and is taken whole by `ApplyCost`. So a cast this cannot cover is
	// refused, and an aura's upkeep it cannot cover switches the aura off.
	const float Held = AbilitySystem->GetNumericAttribute(Pool);
	return Pool == UCataclysmVitalAttributeSet::GetHealthAttribute()
		? Held > Cost
		: Held >= Cost;
}

const TCHAR* UCataclysmGameplayAbility::CostPaidFromEnergyShieldStat =
	TEXT("skill_cost_paid_from_energy_shield");

FGameplayAttribute UCataclysmGameplayAbility::PoolPaying(
	const UAbilitySystemComponent* AbilitySystem, float Cost)
{
	const FGameplayAttribute Pool = CostPool(AbilitySystem);
	if (PoolCovers(AbilitySystem, Pool, Cost))
	{
		return Pool;
	}

	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	const FGameplayAttribute Shield = UCataclysmVitalAttributeSet::GetEnergyShieldAttribute();
	if (Cataclysm && Pool == UCataclysmVitalAttributeSet::GetManaAttribute()
		&& Cataclysm->StatForSkill(FName(CostPaidFromEnergyShieldStat),
								   FGameplayTagContainer(), 0.0f) > 0.0f
		&& PoolCovers(AbilitySystem, Shield, Cost))
	{
		return Shield;
	}
	return FGameplayAttribute();
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

	const UAbilitySystemComponent* AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;

	// WHAT IT COSTS THIS CHARACTER, NOT WHAT THE SLOT STATES. Issue #1815. The
	// same function answers the payment below, an aura's upkeep and the skill
	// bar, so a cast this refuses is one the character really cannot pay for.
	const float Cost = ManaCostFor(AbilitySystem);
	if (Cost <= 0.0f)
	{
		// FREE IS ALLOWED WITH NO POOL AT ALL, which is what "your skills cost no
		// mana" has to mean: a character at nothing left still casts.
		return true;
	}

	if (!AbilitySystem)
	{
		return false;
	}

	// AND A CAST PAID IN HEALTH INSTEAD IS ALWAYS AFFORDABLE. Issues #1820 and #41.
	// It takes a share of CURRENT health, and `UCataclysmSkillTemplate::
	// PayHealthCost` stops that share at the last point of health, so there is no
	// amount of health too small to pay it from.
	if (ManaCostPaidAsHealthPercent(AbilitySystem) > 0.0f)
	{
		return true;
	}

	// OUT OF WHICHEVER POOL THIS CHARACTER PAYS FROM, asked of the function above
	// so that an aura's per-pulse upkeep asks exactly the same question. Issues
	// #1067 and #1901, and #1515 for the energy shield Cast from Ward adds.
	return PoolPaying(AbilitySystem, Cost).IsValid();
}

void UCataclysmGameplayAbility::ApplyCost(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	UAbilitySystemComponent* AbilitySystem =
		ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;

	// THE SAME FIGURE `CheckCost` ALLOWED THE CAST ON. Issue #1815. Asking
	// `GetManaCost` here instead would charge the slot's number while the check
	// above used the character's, so a free cast would still empty a pool.
	// WHAT THIS CAST PAYS IN HEALTH INSTEAD, DECIDED HERE AND ONLY HERE. Issues
	// #1820 and #41. See `ManaCostPaidAsHealthPercentThisCast`: asked after the
	// mana below were taken, the answer could change with the mana it took.
	// Cleared first, so a cast that pays in mana never carries the last one's.
	LastManaCostPaidAsHealthPercent = ManaCostPaidAsHealthPercent(AbilitySystem);

	const float Cost = ManaCostFor(AbilitySystem);
	if (Cost <= 0.0f)
	{
		return;
	}

	if (!AbilitySystem)
	{
		return;
	}

	// INSTEAD OF MANA MEANS NO MANA IS TAKEN. The health is taken by
	// `UCataclysmSkillTemplate::PayHealthCost`, beside every other health cost.
	if (LastManaCostPaidAsHealthPercent > 0.0f)
	{
		return;
	}

	// Applied directly rather than through a Gameplay Effect asset. There is no
	// authored asset per slot to carry a magnitude that comes from a generated
	// table, and an effect built at runtime for every activation would allocate
	// on every button press. When enchantments that change a skill's mana cost
	// are built -- four of them exist in the data -- this is where they hook in.
	//
	// OUT OF WHICHEVER POOL THIS CHARACTER PAYS FROM, and `CheckCost` above has
	// already refused the cast if no pool could cover it. Issue #1067.
	//
	// A COST PAID FROM THE SHIELD IS NOT DAMAGE, ruled on 2026-09-24 under the
	// owner's delegation, issue #1515. Written straight onto the attribute, it
	// passes none of the damage path: the shield's refill wait, which restarts
	// only on damage taken, does not restart, and emptying the shield this way
	// is not "breaking" it, so Sacrificial Ward and anything else that answers a
	// break never hears of it.
	const FGameplayAttribute Pool = PoolPaying(AbilitySystem, Cost);
	if (Pool.IsValid())
	{
		AbilitySystem->ApplyModToAttribute(Pool, EGameplayModOp::Additive, -Cost);
	}
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
	const UAbilitySystemComponent* AbilitySystem, float BaseCooldown,
	const FGameplayTagContainer& SkillTags)
{
	const FGameplayAttribute Reduction =
		UCataclysmCombatAttributeSet::GetCooldownReductionAttribute();
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Reduction))
	{
		return BaseCooldown;
	}

	// THE REDUCTION IS ASKED FOR, AND THEN IT DIVIDES -- THE SAME TWO STEPS THE
	// ATTRIBUTE ROUTE ALWAYS TOOK. Issue #2000. The first attempt at this, in
	// issue #1981, worked the whole interval out in one go through
	// `UCataclysmStatPipeline::EvaluateRate` (deleted by issue #2004), whose
	// divisor was built from the INCREASES bucket. The game's data puts cooldown reduction in the FLAT
	// bucket -- the `Haste` affix is `ValueKind` flat -- so the gear was read as
	// nothing and the Efficacy attribute, which exists only to SCALE a base
	// something else supplied, was read as the reduction itself.
	//
	// `StatForSkill` AND NOT `StatAppliedTo`, and the difference matters here in
	// the opposite direction from usual. The third argument is a FALLBACK used
	// only when nothing was recorded, so a character with rows is evaluated on
	// its own recorded base -- which for this stat is nought, because no class
	// line names it, and the flat rows ARE the value. A figure handed in as the
	// base would be added to them.
	//
	// THE ATTRIBUTE IS THE FALLBACK, so an ability system with no recorded stat
	// line -- every enemy, and a player before its first refresh -- reads exactly
	// what it read before any of this.
	//
	// AND THE FLOOR COMES WITH `FinalCooldown`. `CooldownDivisor` clamps the
	// increases at nought, so a negative reduction lengthens nothing. Issue
	// #1995 wanted that guaranteed on both routes; routing through here is what
	// guarantees it, rather than a second floor written somewhere else.
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	const float Percent = Cataclysm
		? Cataclysm->StatForSkill(FName(TEXT("cooldown_reduction")), SkillTags,
								  AbilitySystem->GetNumericAttribute(Reduction))
		: AbilitySystem->GetNumericAttribute(Reduction);

	// A PERCENTAGE BECOMES A FRACTION HERE. The stat holds 12 for a 12% affix
	// and FinalCooldown wants 0.12, and this is the only place the two meet.
	const float Reduced =
		UCataclysmCombatAttributeSet::FinalCooldown(BaseCooldown, Percent / 100.0f);

	// AND THEN IT IS LENGTHENED, BY A SEPARATE STAT. Issue #1994, ruled
	// 2026-09-23: Base x (1 + lengthening) / divisor, two factors. Five drawback
	// sentences need it, "Ultimate cooldowns increased by 100%-500%" among
	// them. Asked with the skill's tags for the reason the reduction is, so a
	// row scoped to a slot lengthens only that slot. The fallback is nought:
	// the stat has no attribute, so a character with nothing recorded -- every
	// enemy, and a player before its first refresh -- is lengthened by nothing.
	const float Lengthening = Cataclysm
		? Cataclysm->StatForSkill(
			  FName(UCataclysmSkillSlots::CooldownLengtheningStat), SkillTags, 0.0f)
		: 0.0f;
	return Reduced
		* UCataclysmCombatAttributeSet::CooldownLengthFactor(Lengthening / 100.0f);
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
	const float Seconds = CooldownAfterReduction(
		AbilitySystem, GetBaseCooldown(), SkillTagsForStats());
	if (Seconds <= 0.0f)
	{
		return;
	}

	// A duration effect that grants the slot's cooldown tag and nothing else.
	// Built here rather than authored as an asset for the same reason as the
	// cost: the duration comes from a generated table, and there is no asset per
	// slot to put it in.
	//
	// ONE NAME PER COOLDOWN APPLIED, RATHER THAN ONE PER SLOT. Issue #1501.
	// This built every cooldown under `Cooldown_<tag>`, and asking Unreal for an
	// object whose name is already taken does not give a second object: it
	// destroys the existing one in place and constructs the new one at its
	// address. Two characters using the same skill, or one character whose
	// cooldown was reapplied, each destroyed an effect that was still running on
	// somebody.
	//
	// THIS ONE COULD NOT CRASH THE WAY THE STATUS EFFECTS IN
	// `UCataclysmSkillEffects` DID, because it carries no attribute modifiers
	// and so leaves no raw pointers in anyone's attribute aggregator. The object
	// lifetime was wrong for the same reason and is corrected the same way.
	UObject* EffectOuter = GetTransientPackage();
	UGameplayEffect* Effect = NewObject<UGameplayEffect>(
		EffectOuter,
		MakeUniqueObjectName(
			EffectOuter, UGameplayEffect::StaticClass(),
			FName(*FString::Printf(TEXT("Cooldown_%s"), *Tag.ToString()))));
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
