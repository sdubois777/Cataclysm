// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
// For the class resource a scaling bonus counts points of. Issue #980.
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
// For the two ailment chances a condition asks whether this character has at
// all, which is what Spreading Hurt widens. Issue #1718.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For the minions a scaling bonus counts. Issue #1518.
#include "AbilitySystem/CataclysmCommand.h"
// For the damage reduction cap, which a reading counted by a scaling bonus
// stops at, the same cap a hit stops at. Issue #1515.
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For the debuffs a conditional or scaling bonus asks about. Issue #962.
#include "AbilitySystem/CataclysmDebuffs.h"
// For the cooldown tags and the self buffs a respawn tells apart. Issue #1535.
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplates.h"
#include "Player/CataclysmPlayerState.h"
// For AbilitySystemOf, which `WithTargetState` uses to read the health of the
// character being hit. Issue #1515. An actor with no ability system is the
// "cannot be read" case the condition refuses on.
#include "AbilitySystem/CataclysmTargeting.h"
// For TopUp, which is how a worn row restores a pool: it clamps to the
// maximum, and for health it is the healing path, so the nodes that boost
// healing reach it and the Masochist's rule that healing removes Fervour
// applies to it. Issue #1815.
#include "AbilitySystem/CataclysmRegeneration.h"
// For the health a conditional bonus is judged against. Issue #959.
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
// For the character the nearby enemies are measured from. Issue #1597.
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
// For the nearby enemies a conditional or scaling bonus counts. Issue
// #1597. The lists it keeps are the same ones a creature's target search
// reads, so counting enemies near a character costs no second walk of the
// level.
#include "Character/CataclysmTargetCandidates.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
// For the one spelling of "attack_damage" that ApplyTo records the stat under.
// Issue #958.
#include "Items/CataclysmItem.h"

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
	FName Stat, const FGameplayTagContainer& SkillTags, float Fallback,
	float SkillHealthCostPercent, const FCataclysmBlowContext& Blow,
	float MetresMovedBeforeBlow, float TargetDistanceMetres,
	bool bTargetIsStaggered, const AActor* Target,
	int32 EnemiesStruckTogether) const
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
	return UCataclysmStatPipeline::Evaluate(
			   Inputs->Base, Inputs->Modifiers, SkillTags,
			   WithEnemiesInReach(
				   Inputs->Modifiers,
				   WithTargetState(
					   Inputs->Modifiers, Target,
					   CurrentConditions(SkillHealthCostPercent, Blow,
										 MetresMovedBeforeBlow,
										 TargetDistanceMetres,
										 bTargetIsStaggered,
										 EnemiesStruckTogether)))).Final;
}

float UCataclysmAbilitySystemComponent::StatAppliedTo(
	FName Stat, const FGameplayTagContainer& SkillTags, float Figure) const
{
	const FCataclysmStatInputs* Inputs = StatInputs.Find(Stat);
	if (!Inputs)
	{
		// NOTHING RECORDED FOR THIS STAT, so the figure stands as it came in.
		// That is every enemy, and a player before its first refresh.
		return Figure;
	}

	// THE CALLER'S FIGURE IS THE BASE, and `Inputs->Base` is deliberately not
	// added to it: the recorded base belongs to a stat the character holds, and
	// this asks about a figure the caller holds. See the header.
	return UCataclysmStatPipeline::Evaluate(Figure, Inputs->Modifiers, SkillTags,
											CurrentConditions())
		.Final;
}

float UCataclysmAbilitySystemComponent::MaximumEnergyShield() const
{
	const FGameplayAttribute Maximum =
		UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute();

	// THE ATTRIBUTE-SET CHECK IS NOT OPTIONAL. Reading an attribute whose set
	// the component does not hold raises an engine ensure rather than answering
	// zero, and plenty of ability systems in this game have no vital set.
	if (!HasAttributeSetForAttribute(Maximum))
	{
		return 0.0f;
	}

	// THE ATTRIBUTE PLUS WHAT THE REFRESH COULD NOT FOLD INTO IT, so a scaled
	// row reaches play, nothing written straight to the attribute is lost, and
	// no unconditioned row is counted twice. Until 2026-09-23 this ran every row
	// over the attribute through `StatAppliedTo`, which applied the flat rows and
	// increases already inside it a second time. The helper's header says how.
	return AttributePlusWhatWasNotFolded(FName(TEXT("max_energy_shield")),
										 GetNumericAttribute(Maximum));
}

float UCataclysmAbilitySystemComponent::MaximumClassResource() const
{
	const FGameplayAttribute Maximum =
		UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute();

	// THE ATTRIBUTE-SET CHECK IS NOT OPTIONAL, for the reason
	// `MaximumEnergyShield` gives: reading an attribute whose set the component
	// does not hold raises an engine ensure, and every enemy in the game has no
	// class resource set.
	if (!HasAttributeSetForAttribute(Maximum))
	{
		return 0.0f;
	}

	// THE SAME HELPER AS THE SHIELD'S LOOKUP ABOVE, for its reasons. Room for
	// One More's flat +30 and the increased-maximum-Fervour points are already
	// inside the attribute; Vessel's scaled row is not, and is all this adds.
	return AttributePlusWhatWasNotFolded(FName(TEXT("class_resource")),
										 GetNumericAttribute(Maximum));
}

float UCataclysmAbilitySystemComponent::AttributePlusWhatWasNotFolded(
	FName Stat, float Attribute) const
{
	const FCataclysmStatInputs* Inputs = StatInputs.Find(Stat);
	if (!Inputs)
	{
		// NOTHING RECORDED, SO THE ATTRIBUTE IS THE WHOLE ANSWER.
		return Attribute;
	}

	// WHAT THE REFRESH WROTE, WORKED OUT AGAIN RATHER THAN REMEMBERED: the same
	// line, no skill tags and a default reading, which is exactly the call
	// `UCataclysmPlayerClassStats::ApplyTo` makes. Taken from the same list as
	// the figure below, so a character whose rows are all unconditioned gets a
	// difference of exactly nothing.
	const float Folded = UCataclysmStatPipeline::Evaluate(
		Inputs->Base, Inputs->Modifiers, FGameplayTagContainer(),
		FCataclysmStatConditions()).Final;

	// AND WHAT THE LINE COMES TO WITH THE CHARACTER'S READINGS NOW.
	const float Now = UCataclysmStatPipeline::Evaluate(
		Inputs->Base, Inputs->Modifiers, FGameplayTagContainer(),
		CurrentConditions()).Final;

	return Attribute + (Now - Folded);
}

float UCataclysmAbilitySystemComponent::AttackDamageIncreasesForSkill(
	const FGameplayTagContainer& SkillTags,
	float SkillHealthCostPercent, float MetresMovedBeforeBlow,
	float TargetDistanceMetres, bool bTargetIsStaggered,
	const AActor* Target, int32 EnemiesStruckTogether) const
{
	// THE SAME KEY `UCataclysmPlayerClassStats::ApplyTo` RECORDED IT UNDER, and
	// the shared constant rather than a second spelling of the name, because a
	// name that does not match falls back silently and reads as a character with
	// no increases rather than as a fault.
	const FCataclysmStatInputs* Inputs =
		StatInputs.Find(FName(UCataclysmItemModifiers::AttackDamageStat));
	if (!Inputs)
	{
		// NOTHING RECORDED, so the stored figure is the whole answer. Ordinary
		// for an enemy, whose attack damage is written straight onto the
		// attribute, and for a player before its first stat refresh.
		return AttackDamageIncreases;
	}

	// PERCENTAGE POINTS OUT OF THE PIPELINE AND A FRACTION OUT OF HERE, which is
	// the conversion issue #963 was about. The two figures a hit uses have to be
	// in the same units or one cannot be undone and the other applied.
	return UCataclysmStatPipeline::Evaluate(
			   Inputs->Base, Inputs->Modifiers, SkillTags,
			   WithEnemiesInReach(
				   Inputs->Modifiers,
				   WithTargetState(
					   Inputs->Modifiers, Target,
					   CurrentConditions(SkillHealthCostPercent,
										 FCataclysmBlowContext(),
										 MetresMovedBeforeBlow,
										 TargetDistanceMetres,
										 bTargetIsStaggered,
										 EnemiesStruckTogether))))
			   .SumOfIncreases / 100.0f;
}

float UCataclysmAbilitySystemComponent::IncreasesForStat(
	FName Stat, const FGameplayTagContainer& Tags) const
{
	const FCataclysmStatInputs* Inputs = StatInputs.Find(Stat);
	if (!Inputs)
	{
		// NOTHING RECORDED FOR THIS STAT. Ordinary rather than a fault: an
		// enemy's ability system is never given a character stat line, and a
		// player's has none until the first refresh. Nothing recorded means
		// nothing to add.
		return 0.0f;
	}

	// PERCENTAGE POINTS OUT OF THE PIPELINE AND A FRACTION OUT OF HERE, the same
	// conversion `AttackDamageIncreasesForSkill` makes and for the same reason:
	// `SumOfIncreases` is 25 for +25% and every caller wants 0.25.
	//
	// THE BASE IS NOT READ AND MUST NOT BE. A stat reaching this function has no
	// base by definition, so `Evaluate(...).Final` would be zero. Only the
	// increases are asked for.
	return UCataclysmStatPipeline::Evaluate(
			   Inputs->Base, Inputs->Modifiers, Tags,
			   WithEnemiesInReach(Inputs->Modifiers, CurrentConditions()))
			   .SumOfIncreases / 100.0f;
}

bool UCataclysmAbilitySystemComponent::IsStatRemoved(
	FName Stat, const FGameplayTagContainer& Tags) const
{
	const FCataclysmStatInputs* Inputs = StatInputs.Find(Stat);
	if (!Inputs)
	{
		// NOTHING RECORDED, SO NOTHING REMOVED. Ordinary rather than a fault,
		// for the reason `IncreasesForStat` gives above.
		return false;
	}

	// THE SAME PASS AND THE SAME CONDITIONS AS `IncreasesForStat`, reading the
	// count the pipeline keeps rather than the figure. Issue #1791.
	return UCataclysmStatPipeline::Evaluate(
			   Inputs->Base, Inputs->Modifiers, Tags,
			   WithEnemiesInReach(Inputs->Modifiers, CurrentConditions()))
			   .RemovedCount > 0;
}

float UCataclysmAbilitySystemComponent::MultiplierForStatAgainst(
	FName Stat, const FGameplayTagContainer& Tags, const AActor* Target) const
{
	const FCataclysmStatInputs* Inputs = StatInputs.Find(Stat);
	if (!Inputs)
	{
		// NOTHING RECORDED, SO NOTHING CHANGES, for the reason
		// `IncreasesForStat` gives.
		return 1.0f;
	}

	// THE STATE `IncreasesForStat` BUILDS, WITH THE TARGET ADDED, nested the
	// way `AttackDamageMoreForSkill` nests it. `WithTargetState` only adds the
	// target's readings, so every other condition is judged as it is there.
	const FCataclysmStatBreakdown Result = UCataclysmStatPipeline::Evaluate(
		Inputs->Base, Inputs->Modifiers, Tags,
		WithEnemiesInReach(
			Inputs->Modifiers,
			WithTargetState(Inputs->Modifiers, Target, CurrentConditions())));

	// NOTHING IS FOLDED INTO AN ATTRIBUTE FOR A STAT THAT HAS NONE, so the
	// "more" product is the whole of it and is divided by nothing, unlike
	// `AttackDamageMoreForSkill`.
	return FMath::Max(0.0f, 1.0f + Result.SumOfIncreases / 100.0f)
		* Result.MoreMultiplier;
}

float UCataclysmAbilitySystemComponent::AttackDamageMoreForSkill(
	const FGameplayTagContainer& SkillTags,
	float SkillHealthCostPercent, float MetresMovedBeforeBlow,
	float TargetDistanceMetres, bool bTargetIsStaggered,
	const AActor* Target, int32 EnemiesStruckTogether) const
{
	// THE SAME KEY `AttackDamageIncreasesForSkill` READS, for the reason it
	// gives: a name that did not match would fall back in silence and read as a
	// character with no "more" multipliers rather than as a fault.
	const FCataclysmStatInputs* Inputs =
		StatInputs.Find(FName(UCataclysmItemModifiers::AttackDamageStat));
	if (!Inputs)
	{
		// NOTHING RECORDED, SO THE ATTRIBUTE IS THE WHOLE ANSWER. Ordinary for an
		// enemy, whose attack damage is written straight onto the attribute, and
		// for a player before its first stat refresh.
		return 1.0f;
	}

	// WHAT THE ATTRIBUTE WAS BUILT WITH, WORKED OUT AGAIN RATHER THAN
	// REMEMBERED. `UCataclysmPlayerClassStats::ApplyTo` folds in the "more"
	// multipliers it can judge with no skill in hand and nothing known about the
	// character, which is exactly this call. Taking it from the same list as the
	// product below means the two cannot disagree: a character whose "more"
	// modifiers are all unconditional divides a product by itself.
	const float Folded = UCataclysmStatPipeline::Evaluate(
		Inputs->Base, Inputs->Modifiers, FGameplayTagContainer(),
		FCataclysmStatConditions()).MoreMultiplier;

	// AND WHAT THIS SKILL, AT THIS INSTANT, SHOULD CARRY. The distance the
	// character had walked before this use travels here too, for the reason
	// the skill's cost does: it belongs to the blow. Issue #41, slice 2. The
	// call above is deliberately left without it, because that one is what
	// went into the attribute with nothing known about the character.
	//
	// AND SO DOES HOW FAR AWAY THE TARGET STOOD. Issue #1596. THE "MORE" BUCKET
	// IS THE WHOLE REASON THAT READING IS A CONDITION ON A ROW RATHER THAN A
	// VALUE ADDED IN CODE: Demon King's Regalia's 2-piece bonus is "You deal 25%
	// MORE damage to enemies that are within 5 meters of you", and a value added
	// into the increases sum could not express it. So this call must carry it,
	// and a version of this function that took the reading without passing it
	// would leave that row silently worth nothing.
	const float Applying = UCataclysmStatPipeline::Evaluate(
		Inputs->Base, Inputs->Modifiers, SkillTags,
		WithEnemiesInReach(
			Inputs->Modifiers,
			WithTargetState(
				Inputs->Modifiers, Target,
				CurrentConditions(SkillHealthCostPercent,
								  FCataclysmBlowContext(),
								  MetresMovedBeforeBlow,
								  TargetDistanceMetres,
								  bTargetIsStaggered,
								  EnemiesStruckTogether)))).MoreMultiplier;

	// THE FLOOR ONLY GUARDS A LIST BUILT BY HAND. The pipeline clamps every
	// "less" at -99 per cent, so a product of them cannot reach zero.
	return Applying / FMath::Max(Folded, UE_SMALL_NUMBER);
}

FCataclysmStatConditions UCataclysmAbilitySystemComponent::CurrentConditions(
	float SkillHealthCostPercent, const FCataclysmBlowContext& Blow,
	float MetresMovedBeforeBlow, float TargetDistanceMetres,
	bool bTargetIsStaggered, int32 EnemiesStruckTogether) const
{
	// BUILT HERE SO NO CALLER HAS TO KNOW A STAT HAS A CONDITION ON IT.
	// Issue #959. A skill asking what its critical strike chance is should not
	// have to fetch the character's health first, and every caller doing that
	// separately is a place the answer can drift.
	//
	// ASKED FRESH EVERY TIME, not cached. That is the point of a condition: it
	// is true at this instant and may be false at the next, which is why a
	// conditional bonus cannot be folded into a gameplay attribute.
	//
	// NO VITAL ATTRIBUTE SET MEANS THE HEALTH READING IS UNKNOWN, and an unknown
	// reading refuses the conditions that depend on it. That is the ordinary
	// answer for an ability system built without one, not a fault.
	//
	// EACH READING IS INDEPENDENT OF THE OTHERS, which is why this no longer
	// returns early. Issue #962. A component with no vital attribute set can
	// still have paid a health cost, and answering "nothing is known" for every
	// condition because one of them cannot be read would shut a window that is
	// genuinely open.
	FCataclysmStatConditions State;

	if (const UCataclysmVitalAttributeSet* Vitals =
			GetSet<UCataclysmVitalAttributeSet>())
	{
		State = FCataclysmStatConditions::FromHealth(Vitals->GetHealth(),
													 Vitals->GetMaxHealth());

		// AND THE MAXIMUM ITSELF, WHICH `FromHealth` DOES NOT KEEP. Issue
		// #1515. It divides the health in hand by the maximum and stores the
		// share, so a row asking how BIG the bar is -- Weight Bearing's "1 Armor
		// for every 10 maximum health" -- has nothing to read without this.
		//
		// AFTER THE ASSIGNMENT ABOVE AND NOT BEFORE IT, for the reason the
		// energy shield readings give: `FromHealth` returns a whole state and
		// replaces every field, so a reading taken first would be discarded.
		State.MaximumHealth = Vitals->GetMaxHealth();

		// AND HOW MUCH ENERGY SHIELD IS IN HAND, WITH THE TOP OF THAT BAR.
		// Issue #1515. Cold Reading asks for it: "+2% increased Spell Damage per
		// point while your Energy Shield is full."
		//
		// AFTER THE ASSIGNMENT ABOVE AND NOT BEFORE IT. `FromHealth` returns a
		// whole state and this line replaces every field of `State`, so a
		// reading taken first would be silently discarded and the condition
		// would refuse for every character in the game.
		//
		// NO VITAL ATTRIBUTE SET MEANS UNKNOWN, which is why both live inside
		// this block rather than beside it. Skipping the write is what leaves
		// the negative defaults standing, and those defaults are what
		// `EnergyShieldAtMaximum` reads as "there is no shield to ask about".
		// Writing zero here instead would erase the difference between an
		// ability system that has no shield attributes and a character whose
		// shield is simply empty.
		//
		// FLOORED AT ZERO AND NOT CAPPED AT THE MAXIMUM, the same shape as the
		// class resource pair below. The attribute set clamps both ends
		// already, so the floor guards only a value written before that ran;
		// capping the held value here would hide a pool pushed above its top
		// rather than answer "full" for it.
		State.EnergyShieldHeld = FMath::Max(0.0f, Vitals->GetEnergyShield());

		// THE ATTRIBUTE, AND THIS IS THE ONE READER THAT MAY NOT ASK FOR IT.
		// Issue #1973 gave the maximum a lookup, `MaximumEnergyShield` above,
		// so a row that scales it reaches play. This line cannot call it: that
		// lookup asks `AttributePlusWhatWasNotFolded`, which asks this very
		// function for the readings a conditional row needs, so the call would
		// not be slow, it would not return.
		//
		// WHAT IT COSTS, STATED RATHER THAN LEFT TO BE FOUND: a row conditioned
		// on the shield being full compares against the unscaled maximum. One
		// shipped row does -- `Ritualist_basic_c_a2` Cold Reading, "+2%
		// increased Spell Damage per point while your Energy Shield is full" --
		// so a Ritualist holding Hollow Crown with minions out reads full a
		// little before its bar is. The alternative is a stat evaluated while
		// gathering the readings it depends on, which has no answer at all.
		State.EnergyShieldMaximum =
			FMath::Max(0.0f, Vitals->GetMaxEnergyShield());
	}

	State.SecondsSinceHealthCost = SecondsSinceHealthCostPaid();
	State.SecondsSinceForeignDamage = SecondsSinceForeignDamageTaken();
	State.SecondsSinceChargeSkill = SecondsSinceChargeSkillUsed();
	State.SecondsSinceBasicAttack = SecondsSinceBasicAttackUsed();
	State.SecondsSinceBlock = SecondsSinceBlocked();
	State.SecondsSinceSummon = SecondsSinceSummonUsed();
	State.SecondsSinceEvade = SecondsSinceEvaded();
	State.SecondsSinceHitTaken = SecondsSinceHitTaken();
	State.SecondsSinceClassResourceFull = SecondsSinceClassResourceFull();
	State.SecondsSinceClassResourceEmpty = SecondsSinceClassResourceEmptied();
	State.SecondsSinceStruckABoss = SecondsSinceStruckABoss();
	State.SecondsSinceSupportSkill = SecondsSinceSupportSkillUsed();
	State.SecondsSinceMovementSkill = SecondsSinceMovementSkillUsed();
	State.SecondsSinceSpell = SecondsSinceSpellCast();
	State.SecondsSinceMeleeHitTaken = SecondsSinceMeleeHitTaken();
	State.SecondsSinceCrowdControl = SecondsSinceCrowdControlApplied();
	State.SecondsInCombat = SecondsInCombat();
	State.SecondsOutOfCombat = SecondsOutOfCombat();

	// AND HOW MUCH OF THE CLASS RESOURCE IS IN HAND. Issue #980. The Masochist's
	// Reciprocity keystone grows with it: "Your Retaliation damage is increased
	// by 1% for each point of Fervour you currently hold."
	//
	// NO CLASS RESOURCE ATTRIBUTE SET MEANS UNKNOWN, which is every enemy in the
	// game, and a bonus that counts points of a pool the character does not have
	// is correctly worth nothing to it. The reading is left at its negative
	// default rather than set to zero, so that "there is no bar" stays
	// distinguishable from "the bar is empty".
	if (const UCataclysmClassResourceAttributeSet* Resource =
			GetSet<UCataclysmClassResourceAttributeSet>())
	{
		// FLOORED AT ZERO. The attribute set already clamps the pool, so this
		// guards only a value written before that ran, the same way
		// `AddedHealthCostPercent` guards its own.
		State.ClassResourceHeld = FMath::Max(0.0f, Resource->GetClassResource());

		// AND THE TOP OF THAT BAR, for a condition that asks whether the pool is
		// full. Issue #1026. Communion of Pain is the node: "While your Fervour is
		// at maximum you deal 20% more damage and take 20% more damage."
		//
		// READ BESIDE THE POOL AND NOT DERIVED FROM IT, so the two cannot be a
		// frame apart. It is floored at zero for the same reason the pool above
		// is; `ConditionHolds` then refuses a maximum of zero outright, because a
		// bar that cannot hold anything is not full.
		//
		// NOTHING LEAVES IT UNKNOWN ANY MORE, though `ConditionHolds` still
		// accepts a negative as "unknown" and refuses on it. Issue #1029 gave
		// The Final Vow a second option, Apotheosis, that removed the pool's
		// maximum, and this read reported the maximum as unknown for such a
		// character so that "at maximum" refused rather than holding for ever.
		// Issue #1031 rewrote all twelve Masochist capstone options and that one
		// is gone, so every character's pool has a real top again.
		State.ClassResourceMaximum =
			FMath::Max(0.0f, Resource->GetMaxClassResource());

		// THE ATTRIBUTE, AND THIS IS THE ONE READER THAT MAY NOT ASK FOR IT.
		// Issue #1515 gave the maximum a lookup, `MaximumClassResource` above,
		// so a row that scales it reaches play, and every other reader in the
		// game now goes through it. This line cannot: that lookup asks
		// `AttributePlusWhatWasNotFolded`, which asks this very function for
		// the readings a conditional row needs, so the call would not be slow,
		// it would not return. It is the same exception the energy shield's
		// maximum makes twenty lines up, for the same reason.
		//
		// WHAT IT COSTS, STATED RATHER THAN LEFT TO BE FOUND: a row conditioned
		// on the class resource -- `class_resource_at_maximum`, or the share
		// `class_resource_above` reads -- compares against the UNSCALED
		// maximum. So a Ritualist holding Vessel with a large mana pool reads
		// "at maximum" a little before its bar really is. The alternative is a
		// stat worked out while working out that same stat, which is not an
		// alternative.

		// AND HOW MUCH HEALTH THE CHARACTER OWES, AS A SHARE OF ITS MAXIMUM.
		// Issue #994. Compound Interest grows with it: "+1% increased damage per
		// point for every 5% of your maximum health you currently owe."
		//
		// BOTH SETS ARE NEEDED, WHICH IS WHY IT IS NESTED HERE. The amount owed
		// is on the class resource set and the maximum it is measured against is
		// on the vital set, and a component missing either cannot answer at all.
		// An absent vital set and a maximum health of nothing both leave the
		// reading at its negative default rather than dividing by nothing.
		if (const UCataclysmVitalAttributeSet* ForOwed =
				GetSet<UCataclysmVitalAttributeSet>())
		{
			if (ForOwed->GetMaxHealth() > 0.0f)
			{
				// FLOORED AT ZERO AND NOT CAPPED. The attribute set already
				// floors what is owed; there is no ceiling to apply, because a
				// debt larger than the character's whole pool is exactly what
				// The Reckoning is about.
				State.HealthOwedPercent =
					FMath::Max(0.0f, Resource->GetHealthOwed())
					/ ForOwed->GetMaxHealth() * 100.0f;
			}
		}
	}

	// AND HOW MUCH LIFE LEECH THE CHARACTER HAS. Issue #1045. The Masochist's
	// Glutton capstone option grows with it: "Your retaliation damage is
	// increased by 1% for every 1% of life leech you have."
	//
	// A STAT AND NOT A STATE, which is what makes it unlike every reading above.
	// Those change from moment to moment as a character is hurt or spends its
	// pool; this changes only when its gear or its passive points change. It is
	// still read here rather than folded into the retaliation attribute, because
	// a scaled bonus never is -- and reading it here is what lets one stat's
	// value decide another stat's size at all.
	//
	// LEFT UNKNOWN WITHOUT A VITAL ATTRIBUTE SET, which is where life leech
	// lives. Ordinary rather than a fault, and the same shape as the readings
	// above.
	if (const UCataclysmVitalAttributeSet* ForLeech =
			GetSet<UCataclysmVitalAttributeSet>())
	{
		// FLOORED AT ZERO. Nothing states negative life leech, and a negative
		// reading would make a bonus counting it worth a negative amount.
		//
		// AND THIS ONE STAYS A PLAIN ATTRIBUTE READ, DELIBERATELY, WHILE THE
		// THREE IN `UCataclysmLeech` BECAME ASKS UNDER ISSUE #947. Asking the
		// pipeline for `life_leech` here would be circular: this value is an
		// INPUT to the pipeline -- it is what `PerPercentOfLifeLeech` scales a
		// modifier by -- so resolving it through the pipeline would mean
		// evaluating the stat in order to build the state the evaluation needs.
		//
		// THE CONSEQUENCE IS REAL AND IS THE RIGHT ONE: a conditioned life leech
		// row changes what the character leeches and does NOT change what a
		// bonus scaled per point of life leech is worth. Glutton reads the
		// investment a character has made, not the figure it happens to have
		// while standing in the right place.
		State.LifeLeechPercent = FMath::Max(0.0f, ForLeech->GetLifeLeech());
	}

	// AND HOW MUCH DAMAGE REDUCTION THE CHARACTER HAS. Issue #1515. Weight
	// Against Them grows with it: "+1% increased Attack Damage for every 2% of
	// Damage Reduction you have."
	//
	// A PLAIN ATTRIBUTE READ, FOR THE REASON THE LIFE LEECH READING ABOVE GIVES.
	// This value is an input to the pipeline, so asking the pipeline for it
	// would evaluate a stat in order to build the state the evaluation needs.
	// A damage reduction row that holds only in a situation changes what a hit
	// takes and does NOT change what this bonus is worth.
	//
	// THROUGH THE FUNCTION A HIT USES, so the reading stops at the cap the
	// protection stops at. A judgement ruled on 2026-09-17: "the Damage
	// Reduction you have" is the figure that reduces damage.
	//
	// LEFT UNKNOWN WITHOUT A COMBAT ATTRIBUTE SET, where damage reduction lives.
	if (const UCataclysmCombatAttributeSet* ForReduction =
			GetSet<UCataclysmCombatAttributeSet>())
	{
		State.DamageReductionPercent =
			UCataclysmDamageCalculation::EffectiveDamageReduction(
				ForReduction->GetDamageReduction());
	}

	// AND HOW MUCH MAXIMUM MANA THE CHARACTER HAS. Issue #1515. Drawn Deep grows
	// with it: "+1% increased Spell Damage per point for every full 200 maximum
	// mana you have."
	//
	// THE MAXIMUM AND NOT THE MANA IN HAND, which is what the sentence says. A
	// spell that spends mana does not shrink the bonus of the spell after it.
	//
	// A PLAIN ATTRIBUTE READ, FLOORED AT ZERO, for the reasons the readings above
	// give. Left unknown without a vital attribute set.
	if (const UCataclysmVitalAttributeSet* ForMana =
			GetSet<UCataclysmVitalAttributeSet>())
	{
		State.MaximumMana = FMath::Max(0.0f, ForMana->GetMaxMana());

		// AND HOW FULL IT IS, as a share of that maximum. Issues #1820 and #41.
		// Read by `mana_below`. A maximum of zero leaves it unknown rather than
		// at zero percent: a character with no mana pool has nothing to run low
		// on, and every cost it pays already comes from health.
		const float MaxMana = ForMana->GetMaxMana();
		State.ManaPercent = MaxMana > 0.0f
			? FMath::Clamp(ForMana->GetMana() / MaxMana * 100.0f, 0.0f, 100.0f)
			: -1.0f;

		// AND THE MANA IN HAND. Issue #1815, "10%-30% of your current mana".
		State.ManaHeld = FMath::Max(0.0f, ForMana->GetMana());
	}

	// AND HOW MANY STACKS OF EACH KIND ARE STANDING. Issues #1002, #1003 and
	// #1004. Three Masochist nodes grow with one of these counts.
	//
	// OUTSIDE THE CLASS RESOURCE BRANCH ABOVE, because a stack count is not on
	// any attribute set. It is plain state on this component, so every character
	// has one whether or not it has ever earned a stack, and the answer for one
	// that has not is zero.
	//
	// THE WINDOW IS APPLIED HERE, BY ASKING. A stack that expired two seconds
	// ago answers zero without anything having run in the meantime, which is
	// what makes the whole mechanic need no timer.
	State.SanguineMomentumStacks =
		UCataclysmStacks::Held(this, ECataclysmStackKind::SanguineMomentum);
	State.BloodlustStacks =
		UCataclysmStacks::Held(this, ECataclysmStackKind::Bloodlust);
	State.CarnageStacks =
		UCataclysmStacks::Held(this, ECataclysmStackKind::Carnage);

	// AND WHAT HARMFUL EFFECTS THE CHARACTER IS UNDER. Issue #962. Five
	// Masochist nodes ask one of these two questions: four count the debuffs and
	// one asks whether the character is Bleeding.
	//
	// READ FROM THE ABILITY SYSTEM'S OWN TAG LIST, so there is no state here to
	// keep and nothing to cancel when a character dies. A lasting effect grants
	// its target a tag for exactly as long as it runs, so an effect that expired
	// a moment ago has already taken its tag off and both readings are right
	// with nothing having run in the meantime. That is the same argument the
	// stack counts above make for themselves.
	//
	// BOTH ASKED, THOUGH ONE LOOKS DERIVABLE FROM THE OTHER. A character with
	// one debuff may or may not be Bleeding, and a bleeding character may carry
	// three debuffs; neither reading can be worked out from the other.
	State.bIsBleeding = UCataclysmDebuffs::IsBleeding(this);
	State.DebuffsCarried = UCataclysmDebuffs::CountOn(this);

	// AND WHETHER THIS CHARACTER'S ATTACKS CAN CRIPPLE OR WEAKEN AT ALL. Issue
	// #1718. Spreading Hurt asks it: "+4% increased Area of Effect per point for
	// attacks that Cripple or Weaken."
	//
	// A FACT ABOUT THE ATTACKER, WHICH THE NODE'S OWN WORDING FORCES. An area of
	// effect shapes an attack before it lands, so whether a blow applied a
	// Cripple cannot be known when the bonus is worked out. Whether this
	// character's attacks are ones that cripple is knowable, and the two chance
	// stats are what say so.
	//
	// READ OFF THE ATTRIBUTES AND NOT THROUGH `StatForSkill`, because this runs
	// while the pipeline is being set up and asking the pipeline here would
	// re-enter it. The cost is that a chance carried only by a row scoped to a
	// required tag is not in the attribute; every authored chance row today is
	// unscoped, so the reading is complete for the rows that exist.
	//
	// NO COMBAT ATTRIBUTE SET LEAVES IT FALSE, which is the same answer as two
	// zeroes and is the right one: neither character can apply either ailment.
	if (const UCataclysmCombatAttributeSet* Combat =
			GetSet<UCataclysmCombatAttributeSet>())
	{
		State.bCanCrippleOrWeaken = Combat->GetCrippleChance() > 0.0f
			|| Combat->GetWeakenChance() > 0.0f;
	}

	// AND HOW MANY MINIONS THE CHARACTER IS COMMANDING. Issue #1518. The
	// Ritualist's generator grows with it: "1 per second for each minion you
	// have".
	//
	// ASKED OF THE WORLD RATHER THAN TALLIED, the same shape the thrall cap
	// already uses. `ThingsCommandedBy` walks what is alive and owned right
	// now, so a minion that died or expired a moment ago is already gone from
	// the answer with nothing having run in the meantime. That is the argument
	// the stack counts above make for themselves, and it is why neither half of
	// this generator needs a timer or a tally to keep in step.
	//
	// IMPS AND THRALLS TOGETHER, WHICH IS WHY IT IS NOT `ThrallCountOf`. That
	// function deliberately counts only the taken ones, because a reservation
	// is per thrall. The generator says "minion", which the decision of
	// 2026-09-08 settled as meaning both.
	//
	// THE AVATAR AND NOT THE OWNER. A player's ability system is owned by the
	// player state, which survives death, while the pawn is what commands
	// anything. `UCataclysmVitalAttributeSet` makes the same distinction for
	// the same reason, and asking the owner here would count nothing for every
	// player in the game.
	State.MinionsHeld =
		UCataclysmCommand::ThingsCommandedBy(GetAvatarActor()).Num();

	// AND THE SELF-BUFF SKILLS RUNNING. Issue #1815. The same test
	// `ClearWhatDeathEnds` uses to find the buffs a death ends, so "a buff"
	// means one thing in both places.
	//
	// AND THE AURAS RUNNING, IN THE SAME WALK. Issue #1686. An aura is not a
	// self buff, so each class is counted by its own name.
	State.BuffsHeld = 0;
	State.AurasHeld = 0;
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.IsActive())
		{
			continue;
		}
		if (Cast<UCataclysmSelfBuffSkill>(Spec.GetPrimaryInstance()))
		{
			++State.BuffsHeld;
		}
		else if (Cast<UCataclysmAuraSkill>(Spec.GetPrimaryInstance()))
		{
			++State.AurasHeld;
		}
	}

	// AND THE PASSIVE POINTS SPENT, WHICH ONLY A PLAYER HAS. Issue #1686. A
	// player's ability system is owned by its player state, which holds the
	// allocation; anything else keeps the -1 that gives a class point row
	// nothing.
	if (const ACataclysmPlayerState* Player =
			Cast<ACataclysmPlayerState>(GetOwnerActor()))
	{
		State.ClassPointsSpent = Player->GetPassiveAllocation().Total();
	}

	// AND WHAT THE SKILL IN HAND COST, WHICH IS THE ONE READING HERE THAT IS NOT
	// A PROPERTY OF THE CHARACTER. Issue #983. The Masochist's Grand Tithe node
	// asks about "a skill whose health cost is above 10% of your maximum
	// health", so two blows an instant apart from the same character can answer
	// it differently and nothing built from the character alone could tell them
	// apart. Whoever has the blow in hand passes it in.
	//
	// PASSED THROUGH UNCHANGED, INCLUDING ITS NEGATIVE DEFAULT. A caller with no
	// skill in hand -- the character sheet, an enemy's plain attack, a burning
	// patch of ground -- leaves it at -1 and the condition refuses, which is the
	// same rule the readings above follow.
	State.SkillHealthCostPercent = SkillHealthCostPercent;

	// AND WHAT THE BLOW BEING TAKEN IS, the second reading that is not a
	// property of the character. Issue #666. Passed through unchanged: only the
	// damage taken lookup has a hit in hand, and every other caller's facts are
	// all false, which the four conditions reading them refuse.
	State.Blow = Blow;

	// AND WHAT THIS CHARACTER'S MOVEMENT IS DOING. Issue #41, slice 2. Three
	// readings from this component's own clocks, which the 0.25-second step on
	// `ACataclysmCharacterBase` keeps. A component that has never been sampled
	// answers -1 and the movement conditions refuse, the same rule the readings
	// above follow.
	State.bIsMoving = IsMoving();
	State.SecondsSinceMoved = SecondsSinceMoved();
	State.SecondsSinceOwnAttack = SecondsSinceOwnAttack();

	// AND HOW MANY HANDS THE EQUIPPED WEAPON TAKES. Issue #1515, for
	// `wielding_two_handed_weapon` and the Two Hands node. Off the avatar's
	// weapon slots, which read the item base table; an avatar with none -- every
	// enemy -- leaves it at -1 and the condition refuses.
	if (const AActor* Avatar = GetAvatarActor())
	{
		if (const UCataclysmWeaponSlotsComponent* Slots =
				Avatar->FindComponentByClass<UCataclysmWeaponSlotsComponent>())
		{
			State.WeaponHands = Slots->GetEquippedWeaponHands();
		}
	}

	// AND HOW FAR IT MOVED BEFORE THE BLOW IN HAND, which is the third reading
	// here that is not a state of the character. Passed through unchanged,
	// including its negative default: only a skill that has just been paid for
	// has a distance to hand, and "your first melee attack after moving 5 metres"
	// is a question about that blow rather than about this instant.
	State.MetresMovedBeforeBlow = MetresMovedBeforeBlow;

	// AND HOW MANY ENEMIES THE ATTACK IN HAND STRUCK TOGETHER, by the same
	// argument: a fact about the blow being dealt, passed in by the one caller
	// that has the blow and -1 from every caller that does not. Issue #1515.
	State.EnemiesStruckTogether = EnemiesStruckTogether;

	// AND HOW FAR AWAY THE CHARACTER BEING HIT STOOD, passed straight through for
	// the same reason, including its negative default. Issue #1596. Only a lookup
	// with a target in hand has one, which is the attacker's own attack damage and
	// spell damage; every other caller leaves it unknown and the condition reading
	// it refuses.
	State.TargetDistanceMetres = TargetDistanceMetres;

	// AND WHETHER THAT CHARACTER IS STAGGERED, the second reading of the target
	// and passed through for the same reasons. Issue #45. Only a lookup with a
	// target in hand has one; every other caller leaves it false, and false is
	// what the condition reading it refuses on.
	//
	// NO THIRD VALUE FOR "NOT KNOWN", unlike the distance above, and none is
	// needed. The distance needs -1 because zero is a real distance, so "no
	// target" and "touching" would otherwise be the same reading. A bool has no
	// such collision: "not staggered" and "no target" both mean no bonus.
	State.bTargetIsStaggered = bTargetIsStaggered;

	// WHO IS ASKING, SET LAST. Issue #1815. `FromHealth` near the top of this
	// function replaces the whole struct, so a field set before it is lost:
	// that is how the first A2 build left this null for every character with a
	// vital set, and both first-hit conditions refused for everyone. Only the
	// strike-history fill in `WithTargetState` reads it.
	State.AskingAbilitySystem = this;

	return State;
}

FCataclysmStatConditions UCataclysmAbilitySystemComponent::WithEnemiesInReach(
	const TArray<FCataclysmStatModifier>& Modifiers,
	FCataclysmStatConditions State) const
{
	// THE WIDEST REACH ANY ROW ASKS ABOUT, and nothing at all if none does.
	// Issue #1597. This loop is the whole cost to a lookup that counts no
	// enemies, which is almost every lookup in the game.
	float Widest = -1.0f;
	// AND SEPARATELY THE WIDEST REACH A CRIPPLED-ENEMY ROW ASKS ABOUT. Issue
	// #1515. Kept apart from `Widest` because it is filled by a different walk
	// -- one that returns the characters, so each can be asked whether it is
	// crippled -- and a lookup asking only about crippled enemies must not pay
	// for the distances-only walk, nor the reverse.
	float WidestCrippled = -1.0f;
	for (const FCataclysmStatModifier& Modifier : Modifiers)
	{
		if (Modifier.Condition == ECataclysmStatCondition::EnemiesInReachAtLeast
			|| Modifier.Scale == ECataclysmStatScale::PerEnemyInReach)
		{
			Widest = FMath::Max(Widest, Modifier.ReachMetres);
		}
		if (Modifier.Scale == ECataclysmStatScale::PerCrippledEnemyInReach)
		{
			WidestCrippled = FMath::Max(WidestCrippled, Modifier.ReachMetres);
		}
	}

	// A ROW THAT ASKS WITHOUT A REACH IS STILL NOTHING TO WALK. Its own
	// reach is -1, so it counts nobody either way, and a list of such rows
	// leaves both reaches negative and walks no characters.
	//
	// BOTH, NOT EITHER. This returned early on `Widest` alone before issue
	// #1515, which was right while it was the only list. Left that way, a
	// character whose only nearby-enemy row is a crippled one would return
	// here with the crippled list empty, and Grinding Halt would grant nothing
	// with nothing reporting why.
	if (Widest <= 0.0f && WidestCrippled <= 0.0f)
	{
		return State;
	}

	// MEASURED FROM THE AVATAR AND NOT FROM THIS COMPONENT'S OWNER. A
	// player's ability system lives on its player state, which stands
	// nowhere; the avatar is the body the rows are about. An ability system
	// with no avatar has no position, so there is nothing near it.
	const ACataclysmCharacterBase* Character =
		Cast<ACataclysmCharacterBase>(GetAvatarActor());
	if (!Character)
	{
		return State;
	}

	// NO SUBSYSTEM MEANS NO LISTS, which is what a world built without
	// subsystems gives. The conditions keep their empty list and every row
	// that counts enemies grants nothing, which is the same answer an empty
	// room gives.
	if (UCataclysmTargetCandidates* Candidates =
			UCataclysmTargetCandidates::In(GetWorld()))
	{
		if (Widest > 0.0f)
		{
			Candidates->HostileDistancesWithinMetres(
				Character, Character->GetActorLocation(), Widest,
				State.HostileDistancesMetres);
		}

		// AND THE CRIPPLED ONES, ASKED WHILE EACH BODY IS IN HAND. Issue #1515.
		// A distance cannot be tested for a gameplay tag, which is the whole
		// reason `HostileActorsWithinMetres` exists beside the walk above.
		if (WidestCrippled > 0.0f)
		{
			TArray<float> Distances;
			TArray<ACataclysmCharacterBase*> Bodies;
			Candidates->HostileActorsWithinMetres(
				Character, Character->GetActorLocation(), WidestCrippled,
				Distances, Bodies);

			// A TAG THE TABLE DOES NOT HOLD MATCHES NOTHING, so a renamed or
			// missing Cripple effect counts nobody rather than everybody. The
			// same refusal `UCataclysmDebuffs::CrippleTag` documents.
			const FGameplayTag Cripple = UCataclysmDebuffs::CrippleTag();
			if (Cripple.IsValid())
			{
				for (int32 Index = 0; Index < Bodies.Num(); ++Index)
				{
					// `HasTag` AND NOT `HasTagExact`, so an effect granting a
					// child of the Cripple tag still counts. `TagsOnActor`
					// returns explicit tags, and `HasTag` reads their implied
					// parents when it compares.
					if (Bodies[Index]
						&& UCataclysmDebuffs::TagsOnActor(Bodies[Index])
							   .HasTag(Cripple))
					{
						State.CrippledHostileDistancesMetres.Add(Distances[Index]);
					}
				}
			}
		}
	}

	return State;
}

FCataclysmStatConditions UCataclysmAbilitySystemComponent::WithTargetState(
	const TArray<FCataclysmStatModifier>& Modifiers, const AActor* Target,
	FCataclysmStatConditions State)
{
	// WHO THE LOOKUP IS ABOUT, ALWAYS, AND BEFORE THE EARLY RETURN BELOW. Issue
	// #1833, phase 2. A pointer costs nothing to copy, which is why it is not
	// held back until some row asks as the readings below are. Only
	// `PerConsecutiveHit` reads it.
	State.LookupTarget = Target;

	// NOTHING UNLESS A ROW ACTUALLY ASKS, the shape `WithEnemiesInReach` above
	// uses and for a cost this project has already measured and named. Issue
	// #1515. `UCataclysmDebuffs::DamageAgainstSharedDebuff` reads its stat before
	// comparing anything and says why in its own words: "asking the other way
	// round would walk two tag containers on every blow anybody strikes." A walk
	// here would be a third, on every blow every creature in the game throws.
	//
	// ONE PASS ANSWERS BOTH QUESTIONS, which is why the health reading was added
	// here rather than in a second wrapper beside this one. A lookup asking about
	// neither pays one walk of its own modifier list and nothing else.
	bool bWantsAilments = false;
	bool bWantsHealth = false;
	bool bWantsBoss = false;
	bool bWantsHistory = false;
	for (const FCataclysmStatModifier& Modifier : Modifiers)
	{
		switch (Modifier.Condition)
		{
		case ECataclysmStatCondition::TargetCarriesCripple:
		case ECataclysmStatCondition::TargetCarriesCrippleAndWeaken:
		// AND THE VOID SPLINTER, WHICH MUST BE LISTED HERE OR IT READS NOTHING.
		// Issue #1642. `TargetDebuffs` is filled only when some modifier in this
		// lookup asks about an ailment, so a condition missing from this switch
		// is judged against an empty container, answers false every time, and
		// the row grants nothing with no error anywhere.
		case ECataclysmStatCondition::TargetCarriesVoidSplinter:
		// AND THE TWO THAT ASK ABOUT ANY DEBUFF. Issue #1815.
		case ECataclysmStatCondition::TargetCarriesAnyDebuff:
		case ECataclysmStatCondition::TargetCarriesADot:
			bWantsAilments = true;
			break;
		case ECataclysmStatCondition::TargetHealthBelowPercent:
			bWantsHealth = true;
			break;
		// AND WHETHER THE TARGET IS A BOSS, LISTED HERE FOR THE REASON THE
		// AILMENT COMMENT ABOVE GIVES. Issue #1815. A condition missing from
		// this switch is judged against a field nothing filled, answers false
		// every time, and the row grants nothing with no error anywhere.
		case ECataclysmStatCondition::TargetIsBoss:
		case ECataclysmStatCondition::TargetIsNotBoss:
			bWantsBoss = true;
			break;
		case ECataclysmStatCondition::TargetNotYetStruckByYou:
		case ECataclysmStatCondition::TargetNotYetCritByYou:
		// AND HOW LONG AGO, from the same record. Issue #1515, Set Upon.
		case ECataclysmStatCondition::TargetDamagedByYouWithinSeconds:
			bWantsHistory = true;
			break;
		default:
			break;
		}

		// A SCALE ASKS TOO, and asks separately from the condition. Issue
		// #1815: "Each unique debuff on an enemy increases your crit chance
		// against them" carries no condition at all, so the switch above never
		// sees it, and without this its count would read an empty container.
		if (Modifier.Scale == ECataclysmStatScale::PerTargetDebuff)
		{
			bWantsAilments = true;
		}

		if (bWantsAilments && bWantsHealth && bWantsBoss && bWantsHistory)
		{
			// NOTHING LEFT TO LEARN, so stop rather than walking the rest.
			break;
		}
	}

	// NO ROW ASKING AND NO TARGET BOTH LEAVE EVERYTHING UNREAD, and unread is
	// what the conditions refuse on -- an empty container for the ailments and a
	// negative percentage for the health. The two cases are not distinguished
	// because nothing could do anything differently with the distinction: a
	// lookup with no row asking has no condition to answer.
	if (!Target
		|| (!bWantsAilments && !bWantsHealth && !bWantsBoss && !bWantsHistory))
	{
		return State;
	}

	// WHETHER "YOU" HAVE STRUCK IT YET. Issue #1815, the first hit against each
	// enemy. The target keeps the record and the state says who is asking; both
	// are needed, so a lookup missing either leaves the reading unknown and the
	// two conditions refuse.
	if (bWantsHistory && State.AskingAbilitySystem)
	{
		if (const UCataclysmAbilitySystemComponent* Struck =
				Cast<const UCataclysmAbilitySystemComponent>(
					UCataclysmTargeting::AbilitySystemOf(Target)))
		{
			State.bTargetStruckByYou = Struck->WasStruckBy(State.AskingAbilitySystem);
			State.bTargetCritByYou =
				Struck->WasCriticallyStruckBy(State.AskingAbilitySystem);
			State.SecondsSinceStruckByYou =
				Struck->SecondsSinceStruckBy(State.AskingAbilitySystem);
			State.bTargetStrikeHistoryKnown = true;
		}
	}

	// `OpponentCarriesWeaken` IS DELIBERATELY NOT IN THAT SWITCH. It reads
	// `State.Blow.OpponentDebuffs`, which is the other end of the blow and is
	// filled where the incoming hit is built, not here. A row carrying it is
	// asking about whoever struck this character, and this function has the
	// character this one is striking. Answering it from here would read the
	// ailments of the wrong character, which is the exact fault the separate
	// names exist to prevent.
	if (bWantsAilments)
	{
		State.TargetDebuffs = UCataclysmDebuffs::TagsOnActor(Target);
	}

	if (bWantsBoss)
	{
		// THE SAME TEST THE STUN RULE USES, so "a boss" means one thing in the
		// game. `ACataclysmEnemyCharacter::IsBoss` derives it from the rarity the
		// spawner set rather than from a flag or a tag. Issue #1815.
		//
		// ANYTHING THAT IS NOT AN ENEMY CHARACTER IS NOT A BOSS, which is
		// ordinary rather than a fault: a patch of burning ground and a piece of
		// terrain are both actors and neither is a boss. The cast failing leaves
		// this false, which is the refusing direction.
		if (const ACataclysmEnemyCharacter* Enemy =
				Cast<const ACataclysmEnemyCharacter>(Target))
		{
			State.bTargetIsBoss = Enemy->IsBoss();
		}

		// AND THAT IT WAS LOOKED AT, WHICH BOTH HALVES OF THE PAIR ASK FIRST.
		// Set even when the cast failed: "this is not an enemy character" is a
		// read answer of "not a boss", not an absence of one. Issue #1815.
		State.bTargetIsBossKnown = true;
	}

	if (bWantsHealth)
	{
		// THE SHARED ARITHMETIC, so the two ends of a blow cannot compute a
		// share differently. `FromHealth` clamps to 0-100 and leaves the
		// reading negative when the maximum is not positive, which is the
		// "cannot be read" case the condition refuses on.
		//
		// A TARGET WITH NO ABILITY SYSTEM IS THAT CASE TOO, and it is ordinary
		// rather than a fault: a patch of burning ground and a piece of terrain
		// are both actors and neither has health.
		if (const UAbilitySystemComponent* Struck =
				UCataclysmTargeting::AbilitySystemOf(Target))
		{
			State.TargetHealthPercent =
				FCataclysmStatConditions::FromHealth(
					Struck->GetNumericAttribute(
						UCataclysmVitalAttributeSet::GetHealthAttribute()),
					Struck->GetNumericAttribute(
						UCataclysmVitalAttributeSet::GetMaxHealthAttribute()))
					.HealthPercent;
		}
	}

	return State;
}

void UCataclysmAbilitySystemComponent::NoteHealthCostPaid(float HealthSpent)
{
	// NO WORLD MEANS NO CLOCK, so there is nothing to record and nothing that
	// could read it back. Leaving the stamp at its "never" value is right: a
	// window whose start cannot be timed must not be treated as open.
	if (const UWorld* World = GetWorld())
	{
		LastHealthCostAtSeconds = World->GetTimeSeconds();
	}

	// THE AMOUNT GOES ACROSS, because one authored row restores THAT AMOUNT as
	// mana rather than a fraction of a pool.
	ActOnEvent(FName(TEXT("health_cost")), /*EventTags=*/nullptr, HealthSpent);
}

float UCataclysmAbilitySystemComponent::SecondsSinceHealthCostPaid() const
{
	const UWorld* World = GetWorld();
	if (!World || LastHealthCostAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	// CLAMPED AT ZERO RATHER THAN ALLOWED NEGATIVE. World time does not run
	// backwards in play, but a test that sets it by hand can, and a negative
	// answer would read as "never paid" and shut a window that had just opened.
	return FMath::Max(0.0f, World->GetTimeSeconds() - LastHealthCostAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteMovedMetres(float Metres)
{
	bMovedInLastSample = true;

	// THE DISTANCE IS COUNTED WHETHER OR NOT THERE IS A WORLD, because it needs
	// no clock. A negative distance is refused rather than subtracted: a sampler
	// measures a length, and a length is not negative.
	MetresMovedSinceOwnAttackSoFar += FMath::Max(0.0f, Metres);

	// AND THE TOTAL, WHICH NO ATTACK RESETS. Issue #41, slice 2.
	MetresWalkedTotalSoFar += FMath::Max(0.0f, Metres);

	// NO WORLD MEANS NO CLOCK, so the stamp keeps its "never" value, which is the
	// same answer the three timestamps beside it give.
	if (const UWorld* World = GetWorld())
	{
		LastMovedAtSeconds = World->GetTimeSeconds();
	}
}

void UCataclysmAbilitySystemComponent::NoteDidNotMove()
{
	bMovedInLastSample = false;

	// THE FIRST SAMPLE STARTS THE CLOCK EVEN THOUGH NOTHING MOVED. Without this a
	// character that has never moved would read "never", the stationary
	// conditions would refuse it, and a bonus for standing still would never
	// reach the one character most obviously standing still. A character is
	// watched from its first sample, so that is when standing still begins.
	if (LastMovedAtSeconds < 0.0f)
	{
		if (const UWorld* World = GetWorld())
		{
			LastMovedAtSeconds = World->GetTimeSeconds();
		}
	}
}

void UCataclysmAbilitySystemComponent::NoteSampledAt(const FVector& Where)
{
	LastSampledLocation = Where;
	bHasSampledLocation = true;
}

void UCataclysmAbilitySystemComponent::NoteRelocatedInstantly()
{
	// THE CLOCK ONLY: no distance, and the character is not marked as moving.
	// Issue #41, slice 2.
	if (const UWorld* World = GetWorld())
	{
		LastMovedAtSeconds = World->GetTimeSeconds();
	}
}

void UCataclysmAbilitySystemComponent::NoteOwnAttack()
{
	// THE TALLY GOES BACK TO NOTHING WHETHER OR NOT THERE IS A WORLD, because
	// that is what makes the next attack the first one after moving: the distance
	// is counted from here.
	MetresMovedSinceOwnAttackSoFar = 0.0f;

	if (const UWorld* World = GetWorld())
	{
		LastOwnAttackAtSeconds = World->GetTimeSeconds();
	}
}

float UCataclysmAbilitySystemComponent::SecondsSinceMoved() const
{
	const UWorld* World = GetWorld();
	if (!World || LastMovedAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	// CLAMPED AT ZERO for the reason `SecondsSinceHealthCostPaid` is: a test that
	// sets world time by hand can move it backwards, and a negative answer would
	// read as "never moved" and hand a standing-still bonus to a character that
	// had just moved.
	return FMath::Max(0.0f, World->GetTimeSeconds() - LastMovedAtSeconds);
}

float UCataclysmAbilitySystemComponent::SecondsSinceOwnAttack() const
{
	const UWorld* World = GetWorld();
	if (!World || LastOwnAttackAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	// CLAMPED AT ZERO FOR THE SAME REASON AS THE READING ABOVE.
	return FMath::Max(0.0f, World->GetTimeSeconds() - LastOwnAttackAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteHealthDebtDueIn(float Seconds)
{
	// NO WORLD MEANS NO CLOCK, so nothing is recorded and the debt never falls
	// due. The same reasoning as the two timestamps above, and the safe
	// direction here too: a debt whose due time cannot be timed must not be
	// taken at an arbitrary moment. Issue #991.
	if (const UWorld* World = GetWorld())
	{
		// THE EARLIER OF THE TWO WHEN SOMETHING IS ALREADY OWED, so a second
		// cast cannot push an existing debt further away by accident. Making
		// it later is what the Rolling Debt node is for, and that node is not
		// built yet; until it is, the debt falls due when the first deferral
		// said it would.
		const float Due = World->GetTimeSeconds() + FMath::Max(0.0f, Seconds);
		HealthDebtDueAtSeconds = HealthDebtDueAtSeconds < 0.0f
			? Due
			: FMath::Min(HealthDebtDueAtSeconds, Due);
	}
}

bool UCataclysmAbilitySystemComponent::IsHealthDebtDue() const
{
	const UWorld* World = GetWorld();
	if (!World || HealthDebtDueAtSeconds < 0.0f)
	{
		return false;
	}
	return World->GetTimeSeconds() >= HealthDebtDueAtSeconds;
}

void UCataclysmAbilitySystemComponent::ClearHealthDebtDue()
{
	HealthDebtDueAtSeconds = -1.0f;

	// AND THE NEXT DEBT GETS ITS WHOLE ALLOWANCE. Issue #995. The cap Rolling
	// Debt states is how far ONE debt may be pushed out; a debt that has been
	// settled or cleared is finished, and what is incurred afterwards is a new
	// one. Leaving the total behind would make the second debt of a fight
	// unextendable for no reason a player could see.
	HealthDebtExtensionAppliedSeconds = 0.0f;
}

bool UCataclysmAbilitySystemComponent::IsConvertingDamageToBleeding() const
{
	const UWorld* World = GetWorld();
	if (!World || DamageToBleedingUntilSeconds < 0.0f)
	{
		return false;
	}

	// STRICTLY BEFORE, so the window is over at the instant it says it ends
	// rather than one frame later. The debt's own comparison above is the other
	// way round because a debt falls due AT its time; a window runs UNTIL its
	// time. The two are opposite questions and the boundary belongs to the debt.
	return World->GetTimeSeconds() < DamageToBleedingUntilSeconds;
}

bool UCataclysmAbilitySystemComponent::MayStartDamageConversion() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		// NO CLOCK MEANS NO CONVERSION, which is the safe direction: a window
		// that cannot be timed would never close.
		return false;
	}

	// NEVER HAPPENED IS ALLOWED. A negative time means no conversion has ever
	// begun, so there is nothing to wait for.
	return DamageToBleedingNextAllowedSeconds < 0.0f
		|| World->GetTimeSeconds() >= DamageToBleedingNextAllowedSeconds;
}

void UCataclysmAbilitySystemComponent::NoteDamageConversionStarted(
	float WindowSeconds, float CooldownSeconds)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A WINDOW OF NOTHING OPENS NOTHING. A character whose window stat somehow
	// resolved to zero or less would otherwise be marked as converting for an
	// instant and start its cooldown for no benefit at all.
	if (WindowSeconds <= 0.0f)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	DamageToBleedingUntilSeconds = Now + WindowSeconds;

	// FROM THE START AND NOT FROM THE END. See the header: "cannot happen more
	// than once every 10 seconds" is one occurrence per ten second period.
	DamageToBleedingNextAllowedSeconds = Now + CooldownSeconds;
}

bool UCataclysmAbilitySystemComponent::MayReleaseNova() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		// NO CLOCK MEANS NO NOVA, the same refusal MayStartDamageConversion
		// makes and the safe direction: an interval that cannot be timed would
		// let a nova fire on every step of the timer instead of every fifth
		// second.
		return false;
	}

	// NEVER RELEASED ONE IS ALLOWED. A negative time means no nova has ever
	// come, so there is nothing to wait for and the first arrives as soon as
	// the character is hurt enough for the node's condition.
	return NovaNextAllowedSeconds < 0.0f
		|| World->GetTimeSeconds() >= NovaNextAllowedSeconds;
}

void UCataclysmAbilitySystemComponent::NoteEnemyInReach()
{
	if (const UWorld* World = GetWorld())
	{
		EnemyLastInReachSeconds = World->GetTimeSeconds();
	}
}

bool UCataclysmAbilitySystemComponent::OutOfContactFor(float Seconds) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		// NO CLOCK MEANS NO DECAY. A lapse that cannot be timed would otherwise
		// read as "out of contact for ever" and empty the pool of any character
		// whose world is gone, which is every character in a test that never
		// began play.
		return false;
	}

	if (EnemyLastInReachSeconds < 0.0f)
	{
		// NEVER IN CONTACT IS OUT OF CONTACT. See the header: this asks whether
		// contact has lapsed rather than whether an event has recurred. It costs
		// nothing today because a character that has never been near an enemy
		// has an empty pool to drain, and it is the answer the node's sentence
		// gives rather than the one that happens to be harmless.
		return true;
	}

	return World->GetTimeSeconds() - EnemyLastInReachSeconds >= Seconds;
}

bool UCataclysmAbilitySystemComponent::MayReplaceMinion(bool bForExplosion) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float NextAllowed = bForExplosion
		? MinionExplosionReplacementNextAllowedSeconds
		: MinionDeathReplacementNextAllowedSeconds;
	return NextAllowed < 0.0f || World->GetTimeSeconds() >= NextAllowed;
}

void UCataclysmAbilitySystemComponent::NoteMinionReplaced(bool bForExplosion,
														   float IntervalSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || IntervalSeconds <= 0.0f)
	{
		return;
	}

	float& NextAllowed = bForExplosion
		? MinionExplosionReplacementNextAllowedSeconds
		: MinionDeathReplacementNextAllowedSeconds;
	NextAllowed = World->GetTimeSeconds() + IntervalSeconds;
}

const TCHAR* UCataclysmAbilitySystemComponent::LethalHitSurvivedEverySecondsStat =
	TEXT("lethal_hit_survived_every_seconds");
const TCHAR* UCataclysmAbilitySystemComponent::ImmuneAfterLethalHitSecondsStat =
	TEXT("damage_immunity_after_lethal_hit_seconds");

bool UCataclysmAbilitySystemComponent::MayShoulderThrough(const AActor* Enemy) const
{
	const UWorld* World = GetWorld();
	if (!World || !Enemy)
	{
		return false;
	}
	const float* Until = ShoulderedThroughUntil.Find(Enemy);
	return !Until || World->GetTimeSeconds() >= *Until;
}

void UCataclysmAbilitySystemComponent::NoteShoulderedThrough(const AActor* Enemy,
															 float UntilSeconds)
{
	if (!Enemy)
	{
		return;
	}
	for (auto It = ShoulderedThroughUntil.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	ShoulderedThroughUntil.Add(Enemy, UntilSeconds);
}

bool UCataclysmAbilitySystemComponent::MaySurviveLethalHit() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	return LethalHitSurvivalNextAllowedSeconds < 0.0f
		|| World->GetTimeSeconds() >= LethalHitSurvivalNextAllowedSeconds;
}

void UCataclysmAbilitySystemComponent::NoteLethalHitSurvived(float IntervalSeconds,
															 float ImmuneSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || IntervalSeconds <= 0.0f)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	LethalHitSurvivalNextAllowedSeconds = Now + IntervalSeconds;
	ImmuneAfterLethalHitUntilSeconds =
		ImmuneSeconds > 0.0f ? Now + ImmuneSeconds : -1.0f;
}

bool UCataclysmAbilitySystemComponent::MayFollowThrough() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	return FollowThroughNextAllowedSeconds < 0.0f
		|| World->GetTimeSeconds() >= FollowThroughNextAllowedSeconds;
}

float UCataclysmAbilitySystemComponent::NoteFollowedThrough(float IntervalSeconds)
{
	const float Before = FollowThroughNextAllowedSeconds;
	const UWorld* World = GetWorld();
	if (World && IntervalSeconds > 0.0f)
	{
		FollowThroughNextAllowedSeconds = World->GetTimeSeconds() + IntervalSeconds;
	}
	return Before;
}

float UCataclysmAbilitySystemComponent::FollowThroughSecondsLeft() const
{
	const UWorld* World = GetWorld();
	if (!World || FollowThroughNextAllowedSeconds < 0.0f)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, FollowThroughNextAllowedSeconds - World->GetTimeSeconds());
}

const TCHAR* UCataclysmAbilitySystemComponent::ShieldBreakDestroysMinionEverySecondsStat =
	TEXT("shield_break_destroys_minion_every_seconds");

bool UCataclysmAbilitySystemComponent::MaySpendMinionForShield() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	return ShieldWardNextAllowedSeconds < 0.0f
		|| World->GetTimeSeconds() >= ShieldWardNextAllowedSeconds;
}

void UCataclysmAbilitySystemComponent::NoteMinionSpentForShield(float IntervalSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || IntervalSeconds <= 0.0f)
	{
		return;
	}

	ShieldWardNextAllowedSeconds = World->GetTimeSeconds() + IntervalSeconds;
}

bool UCataclysmAbilitySystemComponent::IsImmuneAfterLethalHit() const
{
	const UWorld* World = GetWorld();
	return World && ImmuneAfterLethalHitUntilSeconds >= 0.0f
		&& World->GetTimeSeconds() < ImmuneAfterLethalHitUntilSeconds;
}

void UCataclysmAbilitySystemComponent::NoteNovaReleased(float IntervalSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || IntervalSeconds <= 0.0f)
	{
		// AN INTERVAL OF NOTHING RECORDS NOTHING, rather than allowing the next
		// nova immediately. The one caller passes a constant above zero, so this
		// guards a future one rather than anything that happens today.
		return;
	}

	// FROM THIS NOVA AND NOT FROM THE LAST ONE. "Every 5 seconds" measured
	// from the moment one is released is what keeps the rate steady when a
	// character leaves the health condition and comes back into it.
	NovaNextAllowedSeconds = World->GetTimeSeconds() + IntervalSeconds;
}

bool UCataclysmAbilitySystemComponent::MayTakeLowHealthRelief() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		// NO CLOCK MEANS NO RELIEF, the same refusal `MayReleaseNova` makes and
		// the safe direction: a cooldown that cannot be timed would let the
		// relief fire on every crossing instead of once every thirty seconds.
		return false;
	}

	// NEVER TAKEN IS ALLOWED. A negative time means no crossing has ever been
	// honoured, so there is nothing to wait for and the first is taken at once.
	return LowHealthReliefNextAllowedSeconds < 0.0f
		|| World->GetTimeSeconds() >= LowHealthReliefNextAllowedSeconds;
}

void UCataclysmAbilitySystemComponent::NoteLowHealthReliefTaken(
	float CooldownSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || CooldownSeconds <= 0.0f)
	{
		// A COOLDOWN OF NOTHING RECORDS NOTHING, rather than allowing the next
		// crossing immediately. The one caller passes a constant above zero, so
		// this guards a future one rather than anything that happens today.
		return;
	}

	LowHealthReliefNextAllowedSeconds =
		World->GetTimeSeconds() + CooldownSeconds;
}

bool UCataclysmAbilitySystemComponent::MayApplyAura() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		// NO CLOCK MEANS NO PULSE, the same refusal `MayReleaseNova` makes just
		// above and the same safe direction: an interval that cannot be timed
		// would apply the aura on every step of the regeneration timer instead
		// of every third second.
		return false;
	}

	// NEVER PULSED IS ALLOWED, so the aura starts the moment the first point is
	// spent rather than three seconds afterwards.
	return AuraNextAllowedSeconds < 0.0f
		|| World->GetTimeSeconds() >= AuraNextAllowedSeconds;
}

void UCataclysmAbilitySystemComponent::NoteAuraApplied(float IntervalSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || IntervalSeconds <= 0.0f)
	{
		return;
	}

	AuraNextAllowedSeconds = World->GetTimeSeconds() + IntervalSeconds;
}

int32 UCataclysmAbilitySystemComponent::StacksHeld(ECataclysmStackKind Kind,
												   float WindowSeconds) const
{
	const int32 Index = static_cast<int32>(Kind);
	if (Index < 0 || Index >= UCataclysmStacks::KindCount)
	{
		return 0;
	}

	// A COUNT OF NOTHING IS THE ANSWER BEFORE THE CLOCK IS ASKED. Issue #1002.
	// It is also what lets the timestamp need no "never" sentinel: a character
	// that has earned no stack of this kind returns here.
	if (StackCounts[Index] <= 0)
	{
		return 0;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		// NO WORLD MEANS NO CLOCK, so the window cannot be judged. Zero is the
		// safe direction, the same one every other unreadable state takes: a
		// bonus nobody can time must not be granted for ever.
		return 0;
	}

	// REPORTED RATHER THAN STORED, so asking does not reset anything. The count
	// held in the field is only meaningful inside the window; outside it the
	// answer is zero, and the field is corrected on the next grant.
	const float Since = World->GetTimeSeconds() - StackGrantedAtSeconds[Index];
	return Since > FMath::Max(0.0f, WindowSeconds) ? 0 : StackCounts[Index];
}

void UCataclysmAbilitySystemComponent::GrantStack(ECataclysmStackKind Kind,
												  float WindowSeconds,
												  int32 Cap)
{
	const int32 Index = static_cast<int32>(Kind);
	if (Index < 0 || Index >= UCataclysmStacks::KindCount)
	{
		return;
	}

	// A CAP OF NOTHING GRANTS NOTHING, AND NOW SAYS SO. Issue #1534. The refusal
	// is as old as this function; the silence was the fault. Infernal Brand
	// passed a cap of zero meaning "clear the count", was refused without a
	// word, and exploded on every hit after the fifth. Stacks are removed by
	// `ClearStacks`, never by a grant.
	if (Cap <= 0)
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("%s was asked to grant a stack of %s with a cap of %d, which "
					"grants nothing and removes nothing. Stacks are removed by "
					"ClearStacks."),
			   *GetNameSafe(GetOwner()), UCataclysmStacks::NameOf(Kind), Cap);
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		// Nothing is recorded rather than recorded at an arbitrary time, the
		// same refusal `NoteHealthCostPaid` and `NoteHealthDebtDueIn` make.
		return;
	}

	// A LAPSED COUNT RESTARTS AT ONE. Asked through `StacksHeld` rather than
	// read off the field, so the window is applied exactly once and in one
	// place: a character whose stacks ran out has one again, not one more than
	// it had before they ran out.
	const int32 Standing = StacksHeld(Kind, WindowSeconds);

	StackCounts[Index] = FMath::Min(Standing + 1, Cap);

	// AND THE WHOLE LOT'S EXPIRY MOVES WITH IT. See the header for why this is
	// one timestamp per kind rather than one per stack.
	StackGrantedAtSeconds[Index] = World->GetTimeSeconds();
}

void UCataclysmAbilitySystemComponent::ClearStacks(ECataclysmStackKind Kind)
{
	const int32 Index = static_cast<int32>(Kind);
	if (Index < 0 || Index >= UCataclysmStacks::KindCount)
	{
		return;
	}

	// BACK TO WHAT A CHARACTER THAT NEVER EARNED ONE HOLDS, timestamp and all,
	// so a cleared kind cannot be told apart from one never touched. The next
	// grant starts again at one, because `StacksHeld` answers a count of zero
	// with nothing before it looks at the clock.
	StackCounts[Index] = 0;
	StackGrantedAtSeconds[Index] = 0.0f;
}

bool UCataclysmAbilitySystemComponent::SpendStack(ECataclysmStackKind Kind,
												  float WindowSeconds)
{
	const int32 Index = static_cast<int32>(Kind);
	if (Index < 0 || Index >= UCataclysmStacks::KindCount)
	{
		return false;
	}

	// ASKED THROUGH `StacksHeld` RATHER THAN READ OFF THE FIELD, which is the
	// same choice `GrantStack` makes and for the same reason: the window is
	// applied in one place. A count that lapsed two seconds ago holds nothing,
	// so there is nothing to spend, and this must not take one off a number
	// nobody can still see. It also covers the no-world case without a branch
	// of its own, because `StacksHeld` answers nothing when it cannot read a
	// clock.
	const int32 Standing = StacksHeld(Kind, WindowSeconds);

	// NOTHING TO SPEND IS AN ANSWER, NOT A FAULT. No warning: see the header.
	// This is also the floor. `Standing` cannot be negative -- `StacksHeld`
	// returns a stored count that only `GrantStack` writes, and it writes
	// `Min(Standing + 1, Cap)` -- so refusing at zero is what keeps the count
	// off negative numbers, rather than a clamp after the subtraction.
	if (Standing <= 0)
	{
		return false;
	}

	// AND THE EXPIRY IS LEFT EXACTLY WHERE IT WAS. Spending is not gaining, so
	// the stacks that remain go on expiring when they always would have. A
	// version that refreshed here would let a debuff be held open for ever by
	// something that is supposed to be using it up.
	StackCounts[Index] = Standing - 1;

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s spent a stack of %s, %d left."),
		   *GetNameSafe(GetOwner()), UCataclysmStacks::NameOf(Kind),
		   StackCounts[Index]);

	return true;
}

FCataclysmWhatDeathEnded UCataclysmAbilitySystemComponent::ClearWhatDeathEnds()
{
	FCataclysmWhatDeathEnded Ended;

	// THE RUNNING SELF BUFFS FIRST, AND ENDED RATHER THAN STRIPPED. Their own
	// `EndAbility` takes the bonus back and stops their repeating timers.
	// Removing the modifier from the list instead would leave the skill running,
	// and Butcher's Heat puts its bonus straight back on at the next kill.
	//
	// COLLECTED FIRST AND CANCELLED AFTER, because cancelling changes the list
	// being walked.
	TArray<FGameplayAbilitySpecHandle> RunningBuffs;
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.IsActive()
			&& Cast<UCataclysmSelfBuffSkill>(Spec.GetPrimaryInstance()))
		{
			RunningBuffs.Add(Spec.Handle);
		}
	}
	for (const FGameplayAbilitySpecHandle& Handle : RunningBuffs)
	{
		CancelAbilityHandle(Handle);
	}
	Ended.BuffsEnded = RunningBuffs.Num();

	// EVERY TIMED GAMEPLAY EFFECT, A SKILL'S COOLDOWN INCLUDED. Nothing permanent
	// is at risk here: every effect that can reach a player today is built at run
	// time with a duration or is instant, and the passive tree and gear are
	// written as attribute values rather than applied as effects.
	//
	// COOLDOWNS GO WITH THE REST, WHICH IS THE PROJECT OWNER'S ANSWER OF
	// 2026-09-10, recorded in `docs/DECISIONS.md`. They are counted apart for the
	// log line, known by the tag each grants, and the tags are asked of the one
	// function that names them -- the tag the skill bar reads -- rather than
	// spelled here.
	//
	// THE DURATION IS CHECKED, THOUGH NOTHING TODAY IS INFINITE, so that an
	// effect somebody later makes permanent is kept rather than cleared.
	FGameplayTagContainer CooldownTags;
	for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
	{
		const FGameplayTag Cooldown = UCataclysmSkillSlots::CooldownTag(Slot);
		if (Cooldown.IsValid())
		{
			CooldownTags.AddTag(Cooldown);
		}
	}

	FGameplayEffectQuery Timed;
	Timed.CustomMatchDelegate.BindLambda(
		[](const FActiveGameplayEffect& Effect)
		{
			return Effect.Spec.Def != nullptr
				&& Effect.Spec.Def->DurationPolicy
					   == EGameplayEffectDurationType::HasDuration;
		});

	for (const FActiveGameplayEffectHandle& Handle : GetActiveEffects(Timed))
	{
		// ASKED BEFORE IT GOES, because an effect that has been removed can no
		// longer say what it granted.
		bool bCooldown = false;
		if (const FActiveGameplayEffect* Effect = GetActiveGameplayEffect(Handle))
		{
			FGameplayTagContainer Granted;
			Effect->Spec.GetAllGrantedTags(Granted);
			bCooldown = Granted.HasAny(CooldownTags);
		}

		if (RemoveActiveGameplayEffect(Handle))
		{
			++Ended.TimedEffects;
			if (bCooldown)
			{
				++Ended.Cooldowns;
			}
		}
	}

	// EVERY STACK OF EVERY KIND, through `ClearStacks`, the one function that
	// empties a count. Counted first through `Held`, so the log line says how
	// many were standing rather than how many had already lapsed.
	for (int32 Index = 0; Index < UCataclysmStacks::KindCount; ++Index)
	{
		const ECataclysmStackKind Kind = static_cast<ECataclysmStackKind>(Index);
		Ended.Stacks += UCataclysmStacks::Held(this, Kind);
		ClearStacks(Kind);
	}

	// AND EVERY ROW'S OWN STACKS, for the same reason. Issue #1833.
	for (const TPair<FName, FOwnStack>& Held : OwnStacks)
	{
		Ended.Stacks += OwnStacksHeld(Held.Key);
	}
	OwnStacks.Empty();

	// AND EVERY ROW'S HITS IN A ROW, ruled 2026-09-24. Issue #1833, phase 2.
	for (const TPair<FName, FConsecutiveHits>& Held : ConsecutiveHits)
	{
		Ended.Stacks += Held.Value.Count;
	}
	ConsecutiveHits.Empty();

	// AND EVERY STACK OTHERS PLACED ON IT, ruled 2026-09-24. Issue #1833,
	// phase 2: an enemy's lost armour and cut damage end with it.
	for (const TPair<FName, FPlacedStack>& Held : PlacedStacks)
	{
		Ended.Stacks += Held.Value.Count;
	}
	PlacedStacks.Empty();

	// AND EVERY "EVERY Nth" COUNT, ruled 2026-09-24. Issue #1833, phase 2.
	NthCounts.Empty();

	// AND EVERY HELD NEXT-USE CHARGE, ruled 2026-09-24. Issue #1833, phase 2.
	for (const TPair<FName, FNextUseCharge>& Held : NextUseCharges)
	{
		Ended.Stacks += Held.Value.Count;
	}
	NextUseCharges.Empty();

	// AND NOTHING WASTED'S STORE, which waits, like a charge, until a melee
	// blow spends it or the character dies. Issue #1515.
	StoredMitigatedDamage = 0.0f;

	// THE HEALTH DEBT, WHAT IS OWED AND WHEN IT FALLS DUE TOGETHER, the pair
	// `UCataclysmHealthDebt::ClearOnKill` writes. Issue #1013, answered by the
	// same ruling: every character, The Reckoning included. That keystone's debt
	// "is cleared by killing an enemy and never by time", which says what clears
	// it in play and does not say it survives a death.
	const FGameplayAttribute Owed =
		UCataclysmClassResourceAttributeSet::GetHealthOwedAttribute();
	if (HasAttributeSetForAttribute(Owed))
	{
		Ended.HealthOwed = FMath::Max(0.0f, GetNumericAttribute(Owed));
		SetNumericAttributeBase(Owed, 0.0f);
	}
	ClearHealthDebtDue();

	// THE WINDOWS A RECENT EVENT OPENED, back to the "never" every character
	// starts with. Each is a bonus or a protection that lasts a few seconds after
	// something happened, which is a temporary buff by another name. The health
	// cost timestamp's own comment already said that carrying it across a respawn
	// "would only let a character keep a window it did not earn".
	LastHealthCostAtSeconds = -1.0f;
	LastForeignDamageAtSeconds = -1.0f;
	LastChargeSkillAtSeconds = -1.0f;
	LastBasicAttackAtSeconds = -1.0f;
	LastBlockAtSeconds = -1.0f;
	LastSummonAtSeconds = -1.0f;
	LastEvadeAtSeconds = -1.0f;
	LastHitTakenAtSeconds = -1.0f;
	LastClassResourceFullAtSeconds = -1.0f;
	LastClassResourceEmptyAtSeconds = -1.0f;
	// THE BOSS WINDOW TOO, which issue #2010 added without adding it here, so a
	// window opened by striking a Boss survived the character's revival. Found
	// while adding the five below; the respawn test now opens every window.
	LastStruckABossAtSeconds = -1.0f;
	// AND WHO HAS STRUCK IT, so a revived character is "not yet struck" by
	// everyone again. Issue #1815.
	StruckBy.Reset();
	CriticallyStruckBy.Reset();
	// AND THE COMBAT CLOCK: a revived character starts out of combat, counted
	// from its revival. Issue #1815.
	LastCombatEventAtSeconds = -1.0f;
	CombatStartedAtSeconds = -1.0f;
	if (const UWorld* World = GetWorld())
	{
		RevivedAtSeconds = World->GetTimeSeconds();
	}
	LastSupportSkillAtSeconds = -1.0f;
	LastMovementSkillAtSeconds = -1.0f;
	LastSpellAtSeconds = -1.0f;
	LastMeleeHitTakenAtSeconds = -1.0f;
	LastCrowdControlAtSeconds = -1.0f;
	DamageToBleedingUntilSeconds = -1.0f;
	DisplacementCount = 0;
	LastDisplacedAtSeconds = -1.0f;

	// AND THE WAITS PASSIVE NODES KEEP, BY THE SAME ANSWER AS THE COOLDOWNS: The
	// Breaking Point's cooldown, Rock Bottom's cooldown, and the intervals of the
	// Unstable Aura's nova and Beacon of Despair. Back to "never" as well, so the
	// first of each after the respawn is allowed at once, as it is for a
	// character that has never had one.
	DamageToBleedingNextAllowedSeconds = -1.0f;
	LowHealthReliefNextAllowedSeconds = -1.0f;
	NovaNextAllowedSeconds = -1.0f;
	AuraNextAllowedSeconds = -1.0f;
	MinionDeathReplacementNextAllowedSeconds = -1.0f;
	MinionExplosionReplacementNextAllowedSeconds = -1.0f;
	LethalHitSurvivalNextAllowedSeconds = -1.0f;
	ImmuneAfterLethalHitUntilSeconds = -1.0f;
	ShieldWardNextAllowedSeconds = -1.0f;

	// AND LEECH NOT YET PAID. `UCataclysmLeech::PayOutStep` skips a corpse, so a
	// payment promised by a hit before the death would resume paying out after
	// the respawn.
	LeechPayments.Reset();

	return Ended;
}

float UCataclysmAbilitySystemComponent::ExtendHealthDebtDueBy(
	float Seconds, float MostAltogether)
{
	// NOTHING OUTSTANDING MEANS NOTHING TO PUSH. Issue #995. The node says
	// "paying a health cost WHILE ONE IS STILL OWED", so a payment made with no
	// debt in hand does nothing at all, and setting a due time here would invent
	// a debt of nothing that then had to be cleared.
	//
	// NO WORLD MEANS NO CLOCK, the same refusal `NoteHealthDebtDueIn` makes.
	if (!GetWorld() || HealthDebtDueAtSeconds < 0.0f || Seconds <= 0.0f)
	{
		return 0.0f;
	}

	// AND NO MORE THAN THE ALLOWANCE THIS DEBT HAS LEFT. The design caps how
	// far a debt may be pushed out altogether rather than how far one payment
	// may push it, so the remaining allowance is the cap minus what previous
	// payments already used. Issue #996 carries the reading and its sources.
	const float Remaining =
		FMath::Max(0.0f, MostAltogether) - HealthDebtExtensionAppliedSeconds;
	const float Moved = FMath::Min(Seconds, Remaining);
	if (Moved <= 0.0f)
	{
		return 0.0f;
	}

	HealthDebtDueAtSeconds += Moved;
	HealthDebtExtensionAppliedSeconds += Moved;
	return Moved;
}

void UCataclysmAbilitySystemComponent::NoteForeignDamageTaken()
{
	// NO WORLD MEANS NO CLOCK, so there is nothing to record and nothing
	// that could read it back. The same reasoning as the health cost stamp.
	if (const UWorld* World = GetWorld())
	{
		LastForeignDamageAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("foreign_damage")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceForeignDamageTaken() const
{
	const UWorld* World = GetWorld();
	if (!World || LastForeignDamageAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	// Clamped at zero for the reason the health cost reading is: a test that
	// sets world time by hand can move it backwards, and a negative answer
	// would read as "never" and shut a window that had just opened.
	return FMath::Max(
		0.0f, World->GetTimeSeconds() - LastForeignDamageAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteChargeSkillUsed()
{
	// NO WORLD MEANS NO CLOCK, the same reasoning as the two stamps above.
	if (const UWorld* World = GetWorld())
	{
		LastChargeSkillAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("charge_skill")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceChargeSkillUsed() const
{
	const UWorld* World = GetWorld();
	if (!World || LastChargeSkillAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	// Clamped at zero for the reason the two readings above are: a test that
	// sets world time by hand can move it backwards, and a negative answer
	// would read as "never" and shut a window that had just opened.
	return FMath::Max(
		0.0f, World->GetTimeSeconds() - LastChargeSkillAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteBasicAttackUsed()
{
	if (const UWorld* World = GetWorld())
	{
		LastBasicAttackAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("basic_attack")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceBasicAttackUsed() const
{
	const UWorld* World = GetWorld();
	if (!World || LastBasicAttackAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(
		0.0f, World->GetTimeSeconds() - LastBasicAttackAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteBlocked()
{
	if (const UWorld* World = GetWorld())
	{
		LastBlockAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("block")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceBlocked() const
{
	const UWorld* World = GetWorld();
	if (!World || LastBlockAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastBlockAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteSummonUsed()
{
	if (const UWorld* World = GetWorld())
	{
		LastSummonAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("summon")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceSummonUsed() const
{
	const UWorld* World = GetWorld();
	if (!World || LastSummonAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastSummonAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteEvaded()
{
	if (const UWorld* World = GetWorld())
	{
		LastEvadeAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("dodge")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceEvaded() const
{
	const UWorld* World = GetWorld();
	if (!World || LastEvadeAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastEvadeAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteHitTaken(bool bLanded)
{
	if (const UWorld* World = GetWorld())
	{
		LastHitTakenAtSeconds = World->GetTimeSeconds();
	}

	// AND A HIT TAKEN IS HALF OF WHAT "IN COMBAT" MEANS. Issue #1815.
	NoteCombatEvent();

	ActOnEvent(FName(TEXT("hit_taken")), nullptr, 0.0f, bLanded);
}

float UCataclysmAbilitySystemComponent::SecondsSinceHitTaken() const
{
	const UWorld* World = GetWorld();
	if (!World || LastHitTakenAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastHitTakenAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteHitDealt()
{
	// THE OTHER HALF. Issue #1815.
	NoteCombatEvent();
}

void UCataclysmAbilitySystemComponent::NoteCombatEvent()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A NEW COMBAT BEGINS WHEN THE LAST ONE HAD LAPSED, or there was none. The
	// same boundary `SecondsInCombat` draws: an event exactly the lapse after
	// the one before is still the same combat.
	const float Now = World->GetTimeSeconds();
	if (LastCombatEventAtSeconds < 0.0f
		|| Now - LastCombatEventAtSeconds > CombatLapseSeconds)
	{
		CombatStartedAtSeconds = Now;
	}
	LastCombatEventAtSeconds = Now;
}

float UCataclysmAbilitySystemComponent::SecondsInCombat() const
{
	const UWorld* World = GetWorld();
	if (!World || LastCombatEventAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastCombatEventAtSeconds > CombatLapseSeconds)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, Now - CombatStartedAtSeconds);
}

void UCataclysmAbilitySystemComponent::RefreshLiveMaximumHealth()
{
	const FName Stat(TEXT("max_health"));
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (!StatInputs.Contains(Stat) || !HasAttributeSetForAttribute(MaxHealth))
	{
		return;
	}

	// THE WHOLE LINE WITH THE STATE NOW, which the fold could not do, and the
	// converted mana on top, which the stat line does not know about.
	const float Live =
		StatForSkill(Stat, FGameplayTagContainer(), GetNumericAttributeBase(MaxHealth))
		+ ConvertedManaToHealth;

	// ONLY A CHANGE IS WRITTEN, so a step that finds nothing new sends nothing.
	if (!FMath::IsNearlyEqual(Live, GetNumericAttributeBase(MaxHealth), 0.001f))
	{
		SetNumericAttributeBase(MaxHealth, Live);
	}
}

bool UCataclysmAbilitySystemComponent::MaximumHealthMovesWithState() const
{
	const FCataclysmStatInputs* Line = StatInputs.Find(FName(TEXT("max_health")));
	if (!Line)
	{
		return false;
	}

	for (const FCataclysmStatModifier& Modifier : Line->Modifiers)
	{
		if (Modifier.Scale != ECataclysmStatScale::Fixed
			|| Modifier.Condition != ECataclysmStatCondition::Always)
		{
			return true;
		}
	}
	return false;
}

float UCataclysmAbilitySystemComponent::SecondsOutOfCombat() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return -1.0f;
	}

	const float Now = World->GetTimeSeconds();
	if (LastCombatEventAtSeconds >= 0.0f)
	{
		// FROM THE MOMENT THE LAST COMBAT LAPSED, not from its last hit.
		const float SinceLapse =
			Now - LastCombatEventAtSeconds - CombatLapseSeconds;
		return SinceLapse > 0.0f ? SinceLapse : -1.0f;
	}

	// NO COMBAT SINCE A REVIVAL, OR SINCE THE CHARACTER SPAWNED. Out of combat
	// for as long as it has existed, judged under the owner's delegation on
	// 2026-09-23. No avatar is no character to read.
	if (RevivedAtSeconds >= 0.0f)
	{
		return FMath::Max(0.0f, Now - RevivedAtSeconds);
	}

	const AActor* Avatar = GetAvatarActor_Direct();
	return Avatar ? FMath::Max(0.0f, Avatar->GetGameTimeSinceCreation()) : -1.0f;
}

void UCataclysmAbilitySystemComponent::NoteClassResourceFull()
{
	if (const UWorld* World = GetWorld())
	{
		LastClassResourceFullAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("resource_full")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceClassResourceFull() const
{
	const UWorld* World = GetWorld();
	if (!World || LastClassResourceFullAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(
		0.0f, World->GetTimeSeconds() - LastClassResourceFullAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteClassResourceEmptied()
{
	if (const UWorld* World = GetWorld())
	{
		LastClassResourceEmptyAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("resource_empty")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceClassResourceEmptied() const
{
	const UWorld* World = GetWorld();
	if (!World || LastClassResourceEmptyAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(
		0.0f, World->GetTimeSeconds() - LastClassResourceEmptyAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteStruckABoss()
{
	if (const UWorld* World = GetWorld())
	{
		LastStruckABossAtSeconds = World->GetTimeSeconds();
	}

	// AND THE ACTION EVENT FIRES, WHICH IS NOT OPTIONAL. `action_events()` in
	// `tools/generate_datatables.py` builds the vocabulary an action row may
	// name from every condition called `seconds_after_<event>`, so adding the
	// clock added `striking_a_boss` to that vocabulary whether or not anything
	// raised it. A row naming an event nothing raises grants nothing and says
	// so nowhere, which is the silent failure that file warns about elsewhere.
	ActOnEvent(FName(TEXT("striking_a_boss")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceStruckABoss() const
{
	const UWorld* World = GetWorld();
	if (!World || LastStruckABossAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastStruckABossAtSeconds);
}

const TCHAR* UCataclysmAbilitySystemComponent::RendPercentStat =
	TEXT("third_melee_hit_armour_removed_percent");
const TCHAR* UCataclysmAbilitySystemComponent::RendSecondsStat =
	TEXT("third_melee_hit_armour_removed_seconds");

void UCataclysmAbilitySystemComponent::NoteLandedMeleeHitFrom(
	const UCataclysmAbilitySystemComponent* Striker)
{
	if (!Striker || Striker == this)
	{
		return;
	}
	const float Percent =
		Striker->StatForSkill(FName(RendPercentStat), FGameplayTagContainer(), 0.0f);
	if (Percent <= 0.0f)
	{
		return;
	}

	int32& Count = LandedMeleeHitsFrom.FindOrAdd(Striker);
	++Count;
	if (Count < RendEveryHits)
	{
		return;
	}
	Count = 0;

	const float Seconds =
		Striker->StatForSkill(FName(RendSecondsStat), FGameplayTagContainer(), 0.0f);
	const UWorld* World = GetWorld();
	if (Seconds <= 0.0f || !World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	const bool bRunning = Now < ArmourRemovedUntil;
	ArmourRemovedPercent = bRunning ? FMath::Max(ArmourRemovedPercent, Percent) : Percent;
	ArmourRemovedUntil = Now + Seconds;
}

float UCataclysmAbilitySystemComponent::ArmourRemovedPercentNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	// RENDING BLOWS' SHARE, while it runs, AND THE STACKS PLACED ON THIS
	// CHARACTER, SUMMED AND CLAMPED AT 100. Issue #1833, phase 2, ruled
	// 2026-09-24: all armour removed, from every source, stops at all of it.
	const float Rending = World->GetTimeSeconds() < ArmourRemovedUntil
		? FMath::Max(0.0f, ArmourRemovedPercent) : 0.0f;
	return FMath::Clamp(Rending + PlacedPercentNow(/*bCutsDamage=*/false), 0.0f, 100.0f);
}

int32 UCataclysmAbilitySystemComponent::StandingNthCount(FName Key) const
{
	const FNthCount* Held = NthCounts.Find(Key);
	if (!Held)
	{
		return 0;
	}
	// LEAVING A COMBAT THE COUNT WAS TAKEN IN ENDS IT, ruled 2026-09-24. A count
	// taken out of combat is kept, so spells cast before a fight still count,
	// and entering combat does not end it.
	if (Held->bInCombat
		&& (SecondsInCombat() < 0.0f
			|| CombatStartedAtSeconds != Held->CombatStartedAtSeconds))
	{
		return 0;
	}
	return Held->Count;
}

float UCataclysmAbilitySystemComponent::NthPercentDue(ECataclysmEveryNth Kind) const
{
	float Due = 0.0f;
	TSet<FName> Asked;
	for (const FCataclysmPoolAction& Action : PoolActions)
	{
		if (Action.NthKind != Kind || Action.EveryNth < 1 || Action.NthKey.IsNone()
			|| Asked.Contains(Action.NthKey))
		{
			continue;
		}
		Asked.Add(Action.NthKey);
		if (StandingNthCount(Action.NthKey) + 1 >= Action.EveryNth)
		{
			Due += FMath::Max(0.0f, Action.Percent);
		}
	}
	return Due;
}

float UCataclysmAbilitySystemComponent::NthHitTakenBonusPercent() const
{
	return NthPercentDue(ECataclysmEveryNth::HitTaken);
}

float UCataclysmAbilitySystemComponent::NthSpellExtraManaPercent() const
{
	return NthPercentDue(ECataclysmEveryNth::SpellCast);
}

bool UCataclysmAbilitySystemComponent::NextAttackIsNth() const
{
	// AN ATTACK ROW'S PERCENT IS NOT READ, so it asks whether any is due rather
	// than what they are worth.
	for (const FCataclysmPoolAction& Action : PoolActions)
	{
		if (Action.NthKind == ECataclysmEveryNth::Attack && Action.EveryNth >= 1
			&& !Action.NthKey.IsNone()
			&& StandingNthCount(Action.NthKey) + 1 >= Action.EveryNth)
		{
			return true;
		}
	}
	return false;
}

void UCataclysmAbilitySystemComponent::NoteNthEvent(ECataclysmEveryNth Kind)
{
	TSet<FName> Counted;
	for (const FCataclysmPoolAction& Action : PoolActions)
	{
		if (Action.NthKind != Kind || Action.EveryNth < 1 || Action.NthKey.IsNone()
			|| Counted.Contains(Action.NthKey))
		{
			continue;
		}
		// ONE COUNT PER ROW, however many copies are worn, for the reason the
		// own stacks give.
		Counted.Add(Action.NthKey);
		const int32 Next = StandingNthCount(Action.NthKey) + 1;
		FNthCount& Held = NthCounts.FindOrAdd(Action.NthKey);
		Held.Count = Next >= Action.EveryNth ? 0 : Next;
		Held.bInCombat = SecondsInCombat() >= 0.0f;
		Held.CombatStartedAtSeconds = CombatStartedAtSeconds;
	}
}

TArray<UCataclysmAbilitySystemComponent::FHeldNthCount>
UCataclysmAbilitySystemComponent::NthCountsForDisplay() const
{
	TArray<FHeldNthCount> Out;
	TSet<FName> Shown;
	for (const FCataclysmPoolAction& Action : PoolActions)
	{
		if (Action.NthKind == ECataclysmEveryNth::None || Action.NthKey.IsNone()
			|| Shown.Contains(Action.NthKey))
		{
			continue;
		}
		Shown.Add(Action.NthKey);
		const int32 Count = StandingNthCount(Action.NthKey);
		if (Count > 0)
		{
			FHeldNthCount Entry;
			Entry.Kind = Action.NthKind;
			Entry.Count = Count;
			Entry.EveryNth = Action.EveryNth;
			Out.Add(Entry);
		}
	}
	return Out;
}

float UCataclysmAbilitySystemComponent::DamageCutPercentNow() const
{
	return FMath::Clamp(PlacedPercentNow(/*bCutsDamage=*/true), 0.0f, 100.0f);
}

float UCataclysmAbilitySystemComponent::PlacedPercentNow(bool bCutsDamage) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	float Total = 0.0f;
	for (const TPair<FName, FPlacedStack>& Each : PlacedStacks)
	{
		const FPlacedStack& Held = Each.Value;
		if (Held.bCutsDamage == bCutsDamage && Held.Count > 0
			&& World->GetTimeSeconds() - Held.GrantedAtSeconds <= Held.WindowSeconds)
		{
			Total += Held.Count * Held.PercentPerStack;
		}
	}
	return Total;
}

void UCataclysmAbilitySystemComponent::ReceivePlacedStack(FName Key,
	float PercentPerStack, float WindowSeconds, int32 Cap, bool bCutsDamage)
{
	const UWorld* World = GetWorld();
	if (Key.IsNone() || PercentPerStack <= 0.0f || WindowSeconds <= 0.0f || !World)
	{
		return;
	}
	// THE OWN-STACK RULE, ruled 2026-09-24: a grant restarts the window and the
	// whole count lapses together, so a count whose window has passed starts
	// again at one. A CAP OF NOUGHT IS NONE: "stacking indefinitely".
	FPlacedStack& Held = PlacedStacks.FindOrAdd(Key);
	const float Now = World->GetTimeSeconds();
	const bool bStanding =
		Held.Count > 0 && Now - Held.GrantedAtSeconds <= Held.WindowSeconds;
	const int32 Next = bStanding ? Held.Count + 1 : 1;
	Held.Count = Cap > 0 ? FMath::Min(Next, Cap) : Next;
	Held.GrantedAtSeconds = Now;
	Held.WindowSeconds = WindowSeconds;
	Held.PercentPerStack = PercentPerStack;
	Held.bCutsDamage = bCutsDamage;
}

void UCataclysmAbilitySystemComponent::NoteStruckBy(
	const UAbilitySystemComponent* Striker, bool bCritical)
{
	if (!Striker)
	{
		return;
	}
	// WITH THE TIME, which Set Upon reads. Issue #1515. A later blow from the
	// same striker overwrites it, so the entry is always the most recent.
	const UWorld* World = GetWorld();
	StruckBy.Add(Striker, World ? World->GetTimeSeconds() : -1.0f);
	if (bCritical)
	{
		CriticallyStruckBy.Add(Striker);
	}
}

bool UCataclysmAbilitySystemComponent::WasStruckBy(
	const UAbilitySystemComponent* Striker) const
{
	return Striker && StruckBy.Contains(Striker);
}

bool UCataclysmAbilitySystemComponent::WasCriticallyStruckBy(
	const UAbilitySystemComponent* Striker) const
{
	return Striker && CriticallyStruckBy.Contains(Striker);
}

float UCataclysmAbilitySystemComponent::SecondsSinceStruckBy(
	const UAbilitySystemComponent* Striker) const
{
	const UWorld* World = GetWorld();
	const float* At = Striker ? StruckBy.Find(Striker) : nullptr;
	if (!World || !At || *At < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - *At);
}

void UCataclysmAbilitySystemComponent::NoteSupportSkillUsed()
{
	if (const UWorld* World = GetWorld())
	{
		LastSupportSkillAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("support_skill")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceSupportSkillUsed() const
{
	const UWorld* World = GetWorld();
	if (!World || LastSupportSkillAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastSupportSkillAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteMovementSkillUsed()
{
	if (const UWorld* World = GetWorld())
	{
		LastMovementSkillAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("movement_skill")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceMovementSkillUsed() const
{
	const UWorld* World = GetWorld();
	if (!World || LastMovementSkillAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastMovementSkillAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteSpellCast()
{
	if (const UWorld* World = GetWorld())
	{
		LastSpellAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("spell")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceSpellCast() const
{
	const UWorld* World = GetWorld();
	if (!World || LastSpellAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastSpellAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteMeleeHitTaken(bool bLanded)
{
	NoteMeleeHitTaken(bLanded, nullptr);
}

void UCataclysmAbilitySystemComponent::NoteMeleeHitTaken(bool bLanded,
														 const AActor* Attacker)
{
	if (const UWorld* World = GetWorld())
	{
		LastMeleeHitTakenAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("melee_hit_taken")), nullptr, 0.0f, bLanded, Attacker);
}

float UCataclysmAbilitySystemComponent::SecondsSinceMeleeHitTaken() const
{
	const UWorld* World = GetWorld();
	if (!World || LastMeleeHitTakenAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastMeleeHitTakenAtSeconds);
}

void UCataclysmAbilitySystemComponent::NoteCrowdControlApplied()
{
	if (const UWorld* World = GetWorld())
	{
		LastCrowdControlAtSeconds = World->GetTimeSeconds();
	}

	ActOnEvent(FName(TEXT("crowd_control")));
}

float UCataclysmAbilitySystemComponent::SecondsSinceCrowdControlApplied() const
{
	const UWorld* World = GetWorld();
	if (!World || LastCrowdControlAtSeconds < 0.0f)
	{
		return -1.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - LastCrowdControlAtSeconds);
}

namespace
{
	// THE FOUR POOL NAMES, spelled as `POOL_ACTIONS` in
	// `tools/generate_datatables.py` spells them. The generator refuses any
	// other, so a name reaching here that is not one of these means the CSV was
	// hand-edited or the build is older than the data.
	const FName HealthPoolName(TEXT("health"));
	const FName ManaPoolName(TEXT("mana"));
	const FName EnergyShieldPoolName(TEXT("energy_shield"));
	const FName ClassResourcePoolName(TEXT("class_resource"));
}

bool UCataclysmAbilitySystemComponent::PoolAttributesFor(
	FName Pool, FGameplayAttribute& Held, FGameplayAttribute& Maximum)
{
	if (Pool == HealthPoolName)
	{
		Held = UCataclysmVitalAttributeSet::GetHealthAttribute();
		Maximum = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
		return true;
	}
	if (Pool == ManaPoolName)
	{
		Held = UCataclysmVitalAttributeSet::GetManaAttribute();
		Maximum = UCataclysmVitalAttributeSet::GetMaxManaAttribute();
		return true;
	}
	if (Pool == EnergyShieldPoolName)
	{
		Held = UCataclysmVitalAttributeSet::GetEnergyShieldAttribute();
		Maximum = UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute();
		return true;
	}
	if (Pool == ClassResourcePoolName)
	{
		Held = UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
		Maximum =
			UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute();
		return true;
	}
	return false;
}

const TCHAR* UCataclysmAbilitySystemComponent::NextSkillDamageAction =
	TEXT("next_skill_damage");
const TCHAR* UCataclysmAbilitySystemComponent::NextAttackDamageAction =
	TEXT("next_attack_damage");
const TCHAR* UCataclysmAbilitySystemComponent::NextSkillEffectivenessAction =
	TEXT("next_skill_effectiveness");
const TCHAR* UCataclysmAbilitySystemComponent::EnemyArmorRemovedAction =
	TEXT("enemy_armor_removed");
const TCHAR* UCataclysmAbilitySystemComponent::AttackerDamageRemovedAction =
	TEXT("attacker_damage_removed");
const TCHAR* UCataclysmAbilitySystemComponent::NthHitTakenDamageAction =
	TEXT("nth_hit_taken_damage");
const TCHAR* UCataclysmAbilitySystemComponent::NthSpellManaCostAction =
	TEXT("nth_spell_mana_cost");
const TCHAR* UCataclysmAbilitySystemComponent::NthAttackNoDamageAction =
	TEXT("nth_attack_no_damage");
const TCHAR* UCataclysmAbilitySystemComponent::CooldownResetAllAction =
	TEXT("cooldown_reset_all");
const TCHAR* UCataclysmAbilitySystemComponent::CooldownResetOthersAction =
	TEXT("cooldown_reset_others");
const TCHAR* UCataclysmAbilitySystemComponent::CooldownResetHeavyAction =
	TEXT("cooldown_reset_heavy");
const TCHAR* UCataclysmAbilitySystemComponent::CooldownResetSpecialAction =
	TEXT("cooldown_reset_special");
const TCHAR* UCataclysmAbilitySystemComponent::CooldownResetMovementAction =
	TEXT("cooldown_reset_movement");
const TCHAR* UCataclysmAbilitySystemComponent::CooldownResetEventSkillAction =
	TEXT("cooldown_reset_event_skill");

/**
 * Pins the roll a cooldown reset action makes, 0 to 100, for tests. Negative,
 * the default, rolls for real. Issue #1833, the cooldown reset action; the same
 * shape as `Cataclysm.CooldownSkipRoll`.
 */
static TAutoConsoleVariable<float> CVarCooldownResetRoll(
	TEXT("Cataclysm.CooldownResetRoll"), -1.0f,
	TEXT("Pins the 0-100 roll a cooldown reset enchantment makes. Negative rolls for real."),
	ECVF_Default);

int32 UCataclysmAbilitySystemComponent::RollAndResetCooldowns(
	const FCataclysmPoolAction& Action, const FGameplayTagContainer* EventTags)
{
	using EReset = ECataclysmCooldownReset;
	if (Action.CooldownReset == EReset::None || Action.Percent <= 0.0f)
	{
		return 0;
	}
	const float Pinned = CVarCooldownResetRoll.GetValueOnAnyThread();
	const float Roll = Pinned >= 0.0f ? Pinned : FMath::FRandRange(0.0f, 100.0f);
	if (Roll >= Action.Percent)
	{
		return 0;
	}

	// THE SLOT THE EVENT'S SKILL IS IN, read off its `Slot.*` tag, for the two
	// kinds that ask. None when the event names no slot.
	ECataclysmAbilitySlot EventSlot = ECataclysmAbilitySlot::None;
	if (EventTags)
	{
		for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
		{
			const FGameplayTag Named = CataclysmAbilitySlots::Tag(Slot);
			if (Named.IsValid() && EventTags->HasTagExact(Named))
			{
				EventSlot = Slot;
				break;
			}
		}
	}

	FGameplayTagContainer Clearing;
	const auto Add = [&Clearing](ECataclysmAbilitySlot Slot)
	{
		const FGameplayTag Cooldown = UCataclysmSkillSlots::CooldownTag(Slot);
		if (Cooldown.IsValid())
		{
			Clearing.AddTag(Cooldown);
		}
	};
	switch (Action.CooldownReset)
	{
	case EReset::All:
	case EReset::Others:
		for (const ECataclysmAbilitySlot Slot : CataclysmAbilitySlots::All())
		{
			// "ALL OTHER" LEAVES OUT ONLY THE SLOT THAT FIRED, ruled 2026-09-25.
			// With no slot named there is nothing to leave out, and nothing is
			// cleared: an "other" with no "one" is not what the row says.
			if (Action.CooldownReset == EReset::Others
				&& (EventSlot == ECataclysmAbilitySlot::None || Slot == EventSlot))
			{
				continue;
			}
			Add(Slot);
		}
		break;
	case EReset::Heavy:
		Add(ECataclysmAbilitySlot::Heavy);
		break;
	case EReset::Special:
		Add(ECataclysmAbilitySlot::Special);
		break;
	case EReset::Movement:
		Add(ECataclysmAbilitySlot::Movement);
		break;
	case EReset::EventSkill:
		if (EventSlot != ECataclysmAbilitySlot::None)
		{
			Add(EventSlot);
		}
		break;
	default:
		break;
	}
	if (Clearing.IsEmpty())
	{
		UE_LOG(LogCataclysm, Verbose,
			TEXT("A cooldown reset from '%s' succeeded and named no slot to clear."),
			*Action.ResetKey.ToString());
		return 0;
	}
	return RemoveActiveEffectsWithGrantedTags(Clearing);
}
const TCHAR* UCataclysmAbilitySystemComponent::TimedEvent =
	TEXT("every_seconds");

void UCataclysmAbilitySystemComponent::StepTimedGrants()
{
	const float InCombat = SecondsInCombat();

	// A NEW COMBAT, OR NONE, STARTS EVERY COUNT AGAIN, ruled 2026-09-24.
	if (InCombat < 0.0f || CombatStartedAtSeconds != TimedGrantsCombatStartedAt)
	{
		TimedGrantsGiven.Reset();
		TimedGrantsCombatStartedAt = CombatStartedAtSeconds;
	}
	if (InCombat < 0.0f || PoolActions.IsEmpty())
	{
		return;
	}

	// A COPY, for the reason `ActOnEvent` gives: a grant can reach the
	// equipment refresh that replaces this very list.
	const FName Timed(TimedEvent);
	const TArray<FCataclysmPoolAction> Firing = PoolActions;
	for (const FCataclysmPoolAction& Action : Firing)
	{
		if (Action.Event != Timed || Action.EverySeconds <= 0.0f
			|| !PoolActionAllowed(Action, nullptr))
		{
			continue;
		}
		const FName Key = !Action.NextUseKey.IsNone() ? Action.NextUseKey
			: !Action.StackKey.IsNone() ? Action.StackKey
			: FName(*FString::Printf(TEXT("%s@%g"), *Action.Pool.ToString(),
									 Action.EverySeconds));

		// ONCE FOR EVERY WHOLE PERIOD OF THIS COMBAT, N seconds in first.
		const int32 Due = FMath::FloorToInt(InCombat / Action.EverySeconds);
		int32& Given = TimedGrantsGiven.FindOrAdd(Key);
		while (Given < Due)
		{
			++Given;
			if (!Action.NextUseKey.IsNone())
			{
				GrantNextUseCharge(Action.NextUseKey, Action.bNextUseIsAttack,
								   Action.Percent, Action.NextUseCap,
								   Action.bNextUseIsEffectiveness);
			}
			else if (!Action.StackKey.IsNone())
			{
				GrantOwnStack(Action.StackKey, Action.StackSeconds, Action.StackCap);
			}
			// A COOLDOWN RESET ON A CLOCK. Issue #1833: "Every 20 seconds all
			// your skill cooldowns are instantly reset".
			else if (Action.CooldownReset != ECataclysmCooldownReset::None)
			{
				RollAndResetCooldowns(Action, nullptr);
			}
			else
			{
				ApplyPoolAction(Action, nullptr, 0.0f);
			}
		}
	}
}

void UCataclysmAbilitySystemComponent::GrantNextUseCharge(FName Key, bool bAttack,
													   float Percent, int32 Cap,
													   bool bEffectiveness)
{
	if (Key.IsNone() || Cap <= 0)
	{
		return;
	}
	FNextUseCharge& Held = NextUseCharges.FindOrAdd(Key);
	Held.Count = FMath::Min(Held.Count + 1, Cap);
	Held.Cap = Cap;
	Held.Percent = Percent;
	Held.bAttack = bAttack;
	Held.bEffectiveness = bEffectiveness;
}

const TCHAR* UCataclysmAbilitySystemComponent::MitigatedAddedCapStat =
	TEXT("mitigated_damage_added_to_next_melee_cap_percent");

void UCataclysmAbilitySystemComponent::NoteMitigatedDamage(float Removed)
{
	if (Removed <= 0.0f
		|| StatForSkill(FName(MitigatedAddedCapStat), FGameplayTagContainer(), 0.0f)
			   <= 0.0f)
	{
		return;
	}
	StoredMitigatedDamage += Removed;
}

float UCataclysmAbilitySystemComponent::SpendStoredMitigatedDamage(float HitDamage)
{
	if (StoredMitigatedDamage <= 0.0f || HitDamage <= 0.0f)
	{
		return 0.0f;
	}
	const float CapPercent =
		StatForSkill(FName(MitigatedAddedCapStat), FGameplayTagContainer(), 0.0f);
	if (CapPercent <= 0.0f)
	{
		return 0.0f;
	}
	const float Added = FMath::Min(StoredMitigatedDamage, HitDamage * CapPercent / 100.0f);
	StoredMitigatedDamage = 0.0f;
	return Added;
}

float UCataclysmAbilitySystemComponent::SpendNextUseCharges(bool bUseIsSpell,
															float* OutMoreMultiplier)
{
	float Spent = 0.0f;
	float More = 1.0f;
	for (auto It = NextUseCharges.CreateIterator(); It; ++It)
	{
		const FNextUseCharge& Held = It.Value();
		// A SPELL LEAVES A "NEXT ATTACK" CHARGE WHERE IT IS, for the next use
		// that is an attack. Ruled 2026-09-24.
		if (Held.bAttack && bUseIsSpell)
		{
			continue;
		}
		// AN EFFECTIVENESS CHARGE MULTIPLIES, A DAMAGE CHARGE ADDS. Issue #1833:
		// 300% effectiveness is three times the use's damage, a "more".
		if (Held.bEffectiveness)
		{
			More *= FMath::Pow(FMath::Max(0.0f, Held.Percent) / 100.0f, Held.Count);
		}
		else
		{
			Spent += Held.Count * Held.Percent;
		}
		It.RemoveCurrent();
	}
	if (OutMoreMultiplier)
	{
		*OutMoreMultiplier = More;
	}
	return Spent;
}

float UCataclysmAbilitySystemComponent::NextUseEffectivenessHeld() const
{
	float Multiplier = 1.0f;
	bool bAny = false;
	for (const TPair<FName, FNextUseCharge>& Each : NextUseCharges)
	{
		if (Each.Value.bEffectiveness && Each.Value.Count > 0)
		{
			Multiplier *= FMath::Pow(Each.Value.Percent / 100.0f, Each.Value.Count);
			bAny = true;
		}
	}
	return bAny ? Multiplier * 100.0f : 0.0f;
}

TArray<UCataclysmAbilitySystemComponent::FHeldOwnStacks>
UCataclysmAbilitySystemComponent::OwnStacksByEnchantment() const
{
	// KEYED "ENCHANTMENT:STAT" by `UCataclysmItemModifiers::OwnStackKeyFor`, so
	// the part before the colon groups one enchantment's rows together.
	TMap<FString, FHeldOwnStacks> ByEnchantment;
	for (const TPair<FName, FOwnStack>& Each : OwnStacks)
	{
		const int32 Held = OwnStacksHeld(Each.Key);
		if (Held <= 0)
		{
			continue;
		}
		FString Enchantment;
		FString Stat;
		if (!Each.Key.ToString().Split(TEXT(":"), &Enchantment, &Stat))
		{
			Stat = Each.Key.ToString();
		}
		FHeldOwnStacks& Entry = ByEnchantment.FindOrAdd(Enchantment);
		Entry.Stats.AddUnique(FName(*Stat));
		Entry.Held = FMath::Max(Entry.Held, Held);
		for (const FCataclysmPoolAction& Action : PoolActions)
		{
			if (Action.StackKey == Each.Key)
			{
				Entry.Cap = FMath::Max(Entry.Cap, Action.StackCap);
			}
		}
	}

	// AND EVERY ROW'S HITS IN A ROW, on whichever enemy it is counting. Issue
	// #1833, phase 2: the owner's rule that every system has a basic interface.
	// The count shown is what the next hit on that enemy reads.
	for (const TPair<FName, FConsecutiveHits>& Each : ConsecutiveHits)
	{
		if (!ConsecutiveHitsStanding(Each.Value) || !Each.Value.Target.IsValid())
		{
			continue;
		}
		FString Enchantment;
		FString Stat;
		if (!Each.Key.ToString().Split(TEXT(":"), &Enchantment, &Stat))
		{
			Stat = Each.Key.ToString();
		}
		FHeldOwnStacks& Entry = ByEnchantment.FindOrAdd(Enchantment);
		Entry.Stats.AddUnique(FName(*Stat));
		Entry.Held = FMath::Max(Entry.Held, Each.Value.Count);
		Entry.bConsecutiveHits = true;
		for (const FCataclysmPoolAction& Action : PoolActions)
		{
			if (Action.StackKey == Each.Key)
			{
				Entry.Cap = FMath::Max(Entry.Cap, Action.StackCap);
			}
		}
	}

	// IN A FIXED ORDER, by enchantment name, so the line does not reorder
	// itself from one frame to the next.
	TArray<FString> Order;
	ByEnchantment.GetKeys(Order);
	Order.Sort();
	TArray<FHeldOwnStacks> Out;
	for (const FString& Enchantment : Order)
	{
		FHeldOwnStacks Entry = ByEnchantment[Enchantment];
		Entry.Stats.Sort(FNameLexicalLess());
		Out.Add(MoveTemp(Entry));
	}
	return Out;
}

int32 UCataclysmAbilitySystemComponent::NextUseChargesHeld(FName Key) const
{
	const FNextUseCharge* Held = NextUseCharges.Find(Key);
	return Held ? Held->Count : 0;
}

void UCataclysmAbilitySystemComponent::NextUseChargesByKind(
	float& OutSkillPercent, int32& OutSkillCount,
	float& OutAttackPercent, int32& OutAttackCount) const
{
	OutSkillPercent = OutAttackPercent = 0.0f;
	OutSkillCount = OutAttackCount = 0;
	for (const TPair<FName, FNextUseCharge>& Held : NextUseCharges)
	{
		// AN EFFECTIVENESS CHARGE IS SHOWN ON ITS OWN, by
		// `NextUseEffectivenessHeld`, not added into a damage percentage.
		if (Held.Value.bEffectiveness)
		{
			continue;
		}
		float& Percent = Held.Value.bAttack ? OutAttackPercent : OutSkillPercent;
		int32& Count = Held.Value.bAttack ? OutAttackCount : OutSkillCount;
		Percent += Held.Value.Count * Held.Value.Percent;
		Count += Held.Value.Count;
	}
}

int32 UCataclysmAbilitySystemComponent::OwnStacksHeld(FName StackKey) const
{
	const FOwnStack* Held = OwnStacks.Find(StackKey);
	const UWorld* World = GetWorld();
	if (!Held || Held->Count <= 0 || !World)
	{
		return 0;
	}
	const float Since = World->GetTimeSeconds() - Held->GrantedAtSeconds;
	return Since > Held->WindowSeconds ? 0 : Held->Count;
}

bool UCataclysmAbilitySystemComponent::ConsecutiveHitsStanding(
	const FConsecutiveHits& Held) const
{
	// OUT OF COMBAT ENDS THE COUNT, ruled 2026-09-24, and so does a count begun
	// before this combat did. The second catches a count left from a fight
	// that lapsed when the next fight was begun by the enemy: the character is
	// in combat again, and the count is still not this combat's.
	//
	// "BEGUN BEFORE" IS STRICTLY BEFORE. A fight begun by this character's own
	// hit is counted a moment before that hit puts it in combat, at the same
	// world time, and that count is this combat's.
	return Held.Count > 0 && SecondsInCombat() >= 0.0f
		&& Held.LastAtSeconds >= CombatStartedAtSeconds;
}

int32 UCataclysmAbilitySystemComponent::ConsecutiveHitsOn(FName StackKey,
														  const AActor* Target) const
{
	const FConsecutiveHits* Held = ConsecutiveHits.Find(StackKey);
	if (!Held || !Target || Held->Target.Get() != Target
		|| !ConsecutiveHitsStanding(*Held))
	{
		return 0;
	}
	return Held->Count;
}

void UCataclysmAbilitySystemComponent::GrantConsecutiveHit(FName StackKey,
	const AActor* Target, int32 Cap)
{
	const UWorld* World = GetWorld();
	if (StackKey.IsNone() || !Target || Cap <= 0 || !World)
	{
		return;
	}

	// THE SAME QUESTION `ConsecutiveHitsOn` ASKS, and it is asked of the count
	// standing BEFORE this hit. The first hit of a fight is counted a moment
	// before it puts this character in combat, so it always finds nothing
	// standing and starts at one, which is right: it is the first.
	FConsecutiveHits& Held = ConsecutiveHits.FindOrAdd(StackKey);
	const bool bContinues =
		ConsecutiveHitsStanding(Held) && Held.Target.Get() == Target;
	Held.Count = bContinues ? FMath::Min(Held.Count + 1, Cap) : 1;
	Held.Target = Target;
	Held.LastAtSeconds = World->GetTimeSeconds();
}

void UCataclysmAbilitySystemComponent::GrantOwnStack(FName StackKey,
													 float WindowSeconds, int32 Cap)
{
	const UWorld* World = GetWorld();
	if (StackKey.IsNone() || WindowSeconds <= 0.0f || Cap <= 0 || !World)
	{
		return;
	}
	const int32 Standing = OwnStacksHeld(StackKey);
	FOwnStack& Held = OwnStacks.FindOrAdd(StackKey);
	Held.Count = FMath::Min(Standing + 1, Cap);
	Held.GrantedAtSeconds = World->GetTimeSeconds();
	Held.WindowSeconds = WindowSeconds;
}

void UCataclysmAbilitySystemComponent::ActOnEvent(
	FName Event, const FGameplayTagContainer* EventTags, float EventAmount,
	bool bLanded, const AActor* EventTarget)
{
	// ANNOUNCED FIRST, before either early return below. Issue #1821: a
	// listener asking again for a cached stat needs every event, and most
	// characters carry no action to fire.
	OnActionEvent.Broadcast(Event);

	// DEPTH ONE, BY CONSTRUCTION. See `PoolActionDepth` for why this is stated
	// rather than left to hold by accident.
	if (PoolActionDepth > 0 || PoolActions.IsEmpty())
	{
		return;
	}
	TGuardValue<int32> Depth(PoolActionDepth, 1);

	// A COPY, because applying one writes an attribute, and an attribute write
	// can reach the equipment refresh that replaces this very list.
	const TArray<FCataclysmPoolAction> Firing = PoolActions;
	// ONE STACK PER ROW PER EVENT, however many copies of the row are worn.
	// Issue #1833: two copies share the row's count, and would otherwise each
	// grant one and double the rate the sentence states.
	TSet<FName> StackedThisEvent;
	for (const FCataclysmPoolAction& Action : Firing)
	{
		if (Action.Event != Event || !PoolActionAllowed(Action, EventTags, EventTarget))
		{
			continue;
		}
		// A COOLDOWN RESET, rolled once per row per event, and only on an event
		// that landed. Issue #1833, the cooldown reset action.
		if (Action.CooldownReset != ECataclysmCooldownReset::None)
		{
			if (bLanded && !StackedThisEvent.Contains(Action.ResetKey))
			{
				StackedThisEvent.Add(Action.ResetKey);
				RollAndResetCooldowns(Action, EventTags);
			}
			continue;
		}
		// A CHARGE THE NEXT USE SPENDS. Issue #1833, phase 2. Landed only, and
		// once per row per event, for the reasons the stacks below give.
		if (!Action.NextUseKey.IsNone())
		{
			if (bLanded && !StackedThisEvent.Contains(Action.NextUseKey))
			{
				StackedThisEvent.Add(Action.NextUseKey);
				// AND WHETHER IT IS AN EFFECTIVENESS CHARGE, which this call
				// dropped until issue #1833's consecutive hits: an event-granted
				// "next skill at 300% effectiveness" was held as +300% increased
				// damage. Only the timed grant passed it, and the one authored
				// effectiveness row is timed, so no worn row was affected.
				GrantNextUseCharge(Action.NextUseKey, Action.bNextUseIsAttack,
								   Action.Percent, Action.NextUseCap,
								   Action.bNextUseIsEffectiveness);
			}
			continue;
		}
		// A STACK PLACED ON THE OTHER CHARACTER OF THE EVENT: the enemy
		// struck, or the attacker that struck. Issue #1833, phase 2. Landed
		// only, once per row per event, and only for an event naming that
		// character. `PoolActionAllowed` above has judged the row's tags.
		if (!Action.PlacedKey.IsNone())
		{
			if (bLanded && EventTarget && !StackedThisEvent.Contains(Action.PlacedKey))
			{
				if (UCataclysmAbilitySystemComponent* Other =
						Cast<UCataclysmAbilitySystemComponent>(
							UCataclysmTargeting::AbilitySystemOf(EventTarget)))
				{
					StackedThisEvent.Add(Action.PlacedKey);
					Other->ReceivePlacedStack(Action.PlacedKey, Action.Percent,
						Action.StackSeconds, Action.StackCap, Action.bPlacedCutsDamage);
				}
			}
			continue;
		}
		// HITS IN A ROW ON ONE ENEMY. Issue #1833, phase 2. Before the own
		// stacks below, because the row carries a stack key too. Landed only,
		// once per row per event, and only for an event naming who was struck.
		// `PoolActionAllowed` above has already judged the row's tags, so a
		// hit outside its scope neither counts nor starts the count again.
		if (Action.bConsecutiveHits)
		{
			if (bLanded && EventTarget && !Action.StackKey.IsNone()
				&& !StackedThisEvent.Contains(Action.StackKey))
			{
				StackedThisEvent.Add(Action.StackKey);
				GrantConsecutiveHit(Action.StackKey, EventTarget, Action.StackCap);
			}
			continue;
		}
		if (!Action.StackKey.IsNone())
		{
			// ONLY A LANDED EVENT GRANTS A STACK, ruled 2026-09-23. A pool
			// action refuses one too, since issue #1833's small engine halves:
			// see below.
			if (bLanded && !StackedThisEvent.Contains(Action.StackKey))
			{
				StackedThisEvent.Add(Action.StackKey);
				GrantOwnStack(Action.StackKey, Action.StackSeconds, Action.StackCap);
			}
			continue;
		}
		// AND ONLY A LANDED EVENT MOVES A POOL, since issue #1833's small engine
		// halves. "Every hit you take deals an additional 5%-10% of your maximum
		// HP as bonus damage" is a pool action on `hit_taken`, and an evaded blow
		// is not a hit: ruled 2026-09-23. Only `hit_taken` and `melee_hit_taken`
		// pass an unlanded event, and no pool row named either before that row,
		// measured 2026-09-25, so no row that existed changes.
		if (!bLanded)
		{
			continue;
		}
		ApplyPoolAction(Action, EventTags, EventAmount);
	}
}

bool UCataclysmAbilitySystemComponent::PoolActionAllowed(
	const FCataclysmPoolAction& Action,
	const FGameplayTagContainer* EventTags, const AActor* EventTarget) const
{
	// THE SAME TAG RULE THE STAT PIPELINE APPLIES, copied as a rule rather than
	// re-derived: `HasTag` matches a held tag against the required tag's children
	// as well, so a skill tagged `Type.AOE.PointBlank` satisfies a requirement of
	// `Type.AOE`. That hierarchy is why the design's tags are dotted.
	//
	// AN EVENT WITH NO TAGS CANNOT SATISFY A SCOPED ROW. A row scoped to melee
	// must not fire on an event that cannot say whether it was melee, so the
	// absence is a refusal rather than a pass.
	if (!Action.RequiredTags.IsEmpty())
	{
		if (!EventTags)
		{
			return false;
		}
		for (const FGameplayTag& Required : Action.RequiredTags)
		{
			if (!EventTags->HasTag(Required))
			{
				return false;
			}
		}
	}

	// AND THE CONDITION IS JUDGED NOW, which is the whole difference from a stat
	// row: the pipeline asks a stat row's condition when something reads the
	// stat, and a pool moves at a moment instead.
	//
	// AND AGAINST THE EVENT'S OTHER CHARACTER, since issue #1833's cooldown reset
	// action: "Hitting a staggered enemy resets your heavy attack cooldown" asks
	// whether the enemy struck is staggered. The state is built the way a stat
	// row's is, through `WithTargetState` for whatever this condition asks of a
	// target. With no target it refuses, as every target condition does.
	// Measured 2026-09-25: the one action row with a condition before this asked
	// `health_below`, which reads the wearer, so it judges as it did.
	if (Action.Condition != ECataclysmStatCondition::Always)
	{
		FCataclysmStatModifier Asking;
		Asking.Condition = Action.Condition;
		Asking.ConditionValue = Action.ConditionValue;
		const FCataclysmStatConditions State = WithTargetState(
			{Asking}, EventTarget,
			CurrentConditions(/*SkillHealthCostPercent=*/-1.0f,
							  FCataclysmBlowContext(),
							  /*MetresMovedBeforeBlow=*/-1.0f,
							  /*TargetDistanceMetres=*/-1.0f,
							  EventTarget && UCataclysmSkillEffects::IsStaggered(EventTarget)));
		if (!UCataclysmStatPipeline::ConditionHolds(
				Action.Condition, Action.ConditionValue, State))
		{
			return false;
		}
	}
	return true;
}

void UCataclysmAbilitySystemComponent::ApplyPoolAction(
	const FCataclysmPoolAction& Action, const FGameplayTagContainer* EventTags,
	float EventAmount)
{
	FGameplayAttribute Held;
	FGameplayAttribute Maximum;
	if (!PoolAttributesFor(Action.Pool, Held, Maximum))
	{
		UE_LOG(LogCataclysm, Warning,
			   TEXT("A worn enchantment moves the pool '%s', which this build has "
					"no attributes for, so it does nothing. Regenerate "
					"game/Data/EnchantmentEffects.csv from the workbook."),
			   *Action.Pool.ToString());
		return;
	}

	// THREE THINGS THE PERCENTAGE CAN BE OF, and they differ: the maximum and
	// what is held differ on a hurt character, and the amount the event carried
	// is not a property of the pool at all.
	float Base = 0.0f;
	switch (Action.Base)
	{
	case ECataclysmPoolActionBase::Current:
		Base = GetNumericAttribute(Held);
		break;
	case ECataclysmPoolActionBase::EventAmount:
		Base = EventAmount;
		break;
	case ECataclysmPoolActionBase::Maximum:
	default:
		Base = GetNumericAttribute(Maximum);
		break;
	}
	(void)EventTags;
	const float Amount = Base * Action.Percent / 100.0f;
	if (FMath::IsNearlyZero(Amount))
	{
		return;
	}

	if (Amount > 0.0f)
	{
		// A RESTORE GOES THROUGH `TopUp`, WHICH IS THE HEALING PATH. Ruled
		// 2026-09-14: a restore of health IS healing, so the nodes that boost
		// healing reach it and the Masochist's rule that healing removes Fervour
		// applies to it. Both of those are written about "healing" with no
		// exception for an item, and `TopUp` is what makes both true.
		//
		// NO TAGS. `Keyword.Regeneration` is for a regeneration step and leech
		// carries none; an item's restore is neither, so it carries nothing and a
		// node scoped to one source does not reach it while an unscoped one does.
		UCataclysmRegeneration::TopUp(*this, Held, Maximum, Amount,
									  FGameplayTagContainer());
		return;
	}

	// A DRAIN TAKES THE RULE OF A COST AND NOT THE PATH OF ONE. Ruled
	// 2026-09-14. It writes the pool and nothing else: no on-damage effect
	// fires, no Sanguine Momentum stack is granted, and the health-cost clock is
	// not stamped. Those last two are what
	// `UCataclysmSkillTemplate::PayHealthCost` does beside moving health, and
	// they are about paying for a skill rather than about an item taking
	// something.
	//
	// AND IT CANNOT KILL. Health floors at one and every other pool at zero. A
	// percentage of what is currently held cannot reach zero by itself anyway,
	// which is why the two bases are not the same question.
	const float Floor = (Action.Pool == HealthPoolName) ? 1.0f : 0.0f;
	const float Current = GetNumericAttribute(Held);
	const float Change = FMath::Max(Floor, Current + Amount) - Current;
	if (!FMath::IsNearlyZero(Change))
	{
		ApplyModToAttribute(Held, EGameplayModOp::Additive, Change);
	}
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

int32 UCataclysmAbilitySystemComponent::ExecutePeriodicEffectsGrantingForTests(
	const FGameplayTag& GrantedTag)
{
	if (!GrantedTag.IsValid())
	{
		return 0;
	}

	// FOUND THE WAY `UCataclysmSkillEffects::RemoveEffectsGranting` FINDS THEM:
	// by the tag the effect grants this character, which for a damage-over-time
	// effect is its ailment.
	FGameplayTagContainer Granted;
	Granted.AddTag(GrantedTag);
	const TArray<FActiveGameplayEffectHandle> Running = GetActiveEffects(
		FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Granted));

	for (const FActiveGameplayEffectHandle& Handle : Running)
	{
		ExecutePeriodicEffect(Handle);
	}
	return Running.Num();
}
