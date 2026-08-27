// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
// For the class resource a scaling bonus counts points of. Issue #980.
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
// For the debuffs a conditional or scaling bonus asks about. Issue #962.
#include "AbilitySystem/CataclysmDebuffs.h"
// For the health a conditional bonus is judged against. Issue #959.
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Cataclysm.h"
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
	float SkillHealthCostPercent) const
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
			   CurrentConditions(SkillHealthCostPercent)).Final;
}

float UCataclysmAbilitySystemComponent::AttackDamageIncreasesForSkill(
	const FGameplayTagContainer& SkillTags,
	float SkillHealthCostPercent) const
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
			   CurrentConditions(SkillHealthCostPercent))
			   .SumOfIncreases / 100.0f;
}

FCataclysmStatConditions UCataclysmAbilitySystemComponent::CurrentConditions(
	float SkillHealthCostPercent) const
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
	}

	State.SecondsSinceHealthCost = SecondsSinceHealthCostPaid();
	State.SecondsSinceForeignDamage = SecondsSinceForeignDamageTaken();

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
		// AND A POOL WITH NO MAXIMUM LEAVES IT UNKNOWN, which is what makes
		// "at maximum" refuse for such a character. Issue #1029. The Final Vow's
		// second option removes the maximum, and `MaxClassResource` still holds
		// its old number because the bar is still drawn against it -- so
		// comparing the pool against that number would make Communion of Pain
		// hold PERMANENTLY for an Apotheosis character rather than never, since
		// the pool can now sit above it for ever. There is no top of the bar to
		// be at, so the honest reading is the unknown one, and the two options
		// are an anti-synergy rather than a free bonus.
		State.ClassResourceMaximum =
			UCataclysmClassResourceAttributeSet::PoolIsUncapped(this)
				? -1.0f
				: FMath::Max(0.0f, Resource->GetMaxClassResource());

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

	return State;
}

void UCataclysmAbilitySystemComponent::NoteHealthCostPaid()
{
	// NO WORLD MEANS NO CLOCK, so there is nothing to record and nothing that
	// could read it back. Leaving the stamp at its "never" value is right: a
	// window whose start cannot be timed must not be treated as open.
	if (const UWorld* World = GetWorld())
	{
		LastHealthCostAtSeconds = World->GetTimeSeconds();
	}
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
	if (Index < 0 || Index >= UCataclysmStacks::KindCount || Cap <= 0)
	{
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
