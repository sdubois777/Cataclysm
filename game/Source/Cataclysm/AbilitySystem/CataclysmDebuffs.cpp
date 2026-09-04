// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDebuffs.h"

// For asking a character what a stat is worth with its own state in hand,
// rather than reading the attribute. Issue #1033.
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
// For the attribute the duration stat is folded into. Issue #1033.
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
// For refusing a corpse, which `HoldStep` does. Issue #1070.
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
// For FGameplayEffectQuery, which is how the debuffs running on a character are
// found. Issue #1070.
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"

const TCHAR* UCataclysmDebuffs::DurationStat = TEXT("debuff_duration_taken");

// WHAT CEASELESS PENANCE GRANTS. Issue #1070: "Debuffs on you no longer expire
// while you are above 50% health."
const TCHAR* UCataclysmDebuffs::DoNotExpireStat = TEXT("debuffs_do_not_expire");

// WHAT WOUND CHANNELING'S SECOND CLAUSE GRANTS. Issue #1061: "you deal 1%
// increased damage per point to enemies carrying a debuff you also carry."
const TCHAR* UCataclysmDebuffs::SharedDebuffDamageStat =
	TEXT("damage_to_enemies_sharing_a_debuff");

const TCHAR* const UCataclysmDebuffs::DebuffRootNames[] = {
	// EVERY DAMAGE OVER TIME, AS ONE BRANCH. Bleed, Burn, Disease, Generic,
	// Necrosis, Poison and Void Splinter all hang off this, so naming the parent
	// covers the ones that exist and the ones the design has not built yet.
	TEXT("Keyword.DoT"),

	// AND BEING STUNNED, WHICH IS NOT DAMAGE AND IS STILL A DEBUFF. All three
	// games in the genre count it: Diablo 4 lists Stun among its eleven crowd
	// control effects and Path of Exile lists stunning among its debuffs.
	//
	// `State.StunImmune` IS DELIBERATELY NOT HERE. It arrives with the stun,
	// from the same call, and it protects the character rather than harming it.
	// See the header.
	TEXT("State.Stunned"),

	// AND EVERY NAMED CURSE. Issue #1145. Shred, Madness, Cripple, Weaken,
	// Quarry and the other twenty-two come from the Debuffs sheet of
	// `docs/All_Things_Cataclysm.xlsx`, and since that sheet's name became a
	// segment of the tag they all hang off this one parent.
	//
	// THE BUFFS ARE EXCLUDED BY NOT BEING UNDER IT, rather than by a list. The
	// eighteen from the Buffs sheet are `Status.Buff.*`, so the Commander buff a
	// Succubus grants its allies cannot be counted as a debuff by forgetting to
	// name it. That is the whole reason the branch was split.
	//
	// `Status.DoT` IS NOT HERE ON PURPOSE. See the header: it would double-count
	// against `Keyword.DoT` above.
	TEXT("Status.Debuff"),
};

const int32 UCataclysmDebuffs::DebuffRootCount = UE_ARRAY_COUNT(DebuffRootNames);

FGameplayTagContainer UCataclysmDebuffs::DebuffRoots()
{
	FGameplayTagContainer Roots;
	for (int32 Index = 0; Index < DebuffRootCount; ++Index)
	{
		// ASKED FOR BY NAME AND NOT REQUIRED TO EXIST, the same way every other
		// tag in this module is fetched. A vocabulary that has lost one leaves
		// it out of the container instead of failing to start the game, and the
		// count is then short by that branch rather than wrong about everything.
		const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(DebuffRootNames[Index]), /*ErrorIfNotFound=*/false);
		if (Tag.IsValid())
		{
			Roots.AddTag(Tag);
		}
	}
	return Roots;
}

FGameplayTagContainer UCataclysmDebuffs::TagsOn(
	const UAbilitySystemComponent* AbilitySystem)
{
	FGameplayTagContainer Carried;

	if (!AbilitySystem)
	{
		return Carried;
	}

	const FGameplayTagContainer Roots = DebuffRoots();
	if (Roots.IsEmpty())
	{
		return Carried;
	}

	// WHAT WAS REALLY APPLIED, NOT WHAT THE CHARACTER ANSWERS YES TO. See the
	// header: the engine counts a tag against its parents as well, so a single
	// bleed makes `HasMatchingGameplayTag` true for both `Keyword.DoT.Bleed` and
	// `Keyword.DoT`. This list holds one entry per effect.
	FGameplayTagContainer Owned;
	AbilitySystem->GetOwnedGameplayTags(Owned);

	for (const FGameplayTag& Tag : Owned)
	{
		// MATCHES THE ROOT ITSELF AND ANYTHING UNDER IT. `MatchesAny` asks
		// whether this tag or one of its parents is in the container, which is
		// the direction wanted here: `Keyword.DoT.Bleed` matches the root
		// `Keyword.DoT`, and `Keyword.Leech` matches nothing.
		if (Tag.MatchesAny(Roots))
		{
			Carried.AddTag(Tag);
		}
	}

	return Carried;
}

FGameplayTagContainer UCataclysmDebuffs::TagsOnActor(const AActor* Actor)
{
	return TagsOn(UCataclysmTargeting::AbilitySystemOf(Actor));
}

int32 UCataclysmDebuffs::CountOn(const UAbilitySystemComponent* AbilitySystem)
{
	// THE LENGTH OF THE LIST, SO THE TWO ANSWERS CANNOT DISAGREE. Issue #1057.
	// This used to be its own walk of the owned tags; the list above is the same
	// walk, and keeping two of them would let the seven nodes that pay per
	// debuff count one set while the two nodes that spread one chose from
	// another.
	return TagsOn(AbilitySystem).Num();
}

int32 UCataclysmDebuffs::CountOnActor(const AActor* Actor)
{
	return CountOn(UCataclysmTargeting::AbilitySystemOf(Actor));
}

bool UCataclysmDebuffs::ShareADebuff(const UAbilitySystemComponent* AbilitySystem,
									 const AActor* Other)
{
	const FGameplayTagContainer Mine = TagsOn(AbilitySystem);
	if (Mine.IsEmpty())
	{
		// A CHARACTER SUFFERING FROM NOTHING SHARES NOTHING, which is the
		// ordinary answer for everybody but a hurt Masochist, and it is asked
		// first because it is the cheap half.
		return false;
	}

	const FGameplayTagContainer Theirs = TagsOnActor(Other);
	for (const FGameplayTag& Tag : Theirs)
	{
		// EXACTLY, NOT BY THE PARENT BRANCH. See the header: burning and
		// bleeding are both `Keyword.DoT` and are not the same debuff.
		if (Mine.HasTagExact(Tag))
		{
			return true;
		}
	}

	return false;
}

float UCataclysmDebuffs::DamageAgainstSharedDebuff(
	const UAbilitySystemComponent* Source, const AActor* Target)
{
	if (!Source)
	{
		return 0.0f;
	}

	const FGameplayAttribute Attribute =
		UCataclysmCombatAttributeSet::GetDamageVsSharedDebuffAttribute();
	if (!Source->HasAttributeSetForAttribute(Attribute))
	{
		// AN ABILITY SYSTEM WITHOUT A COMBAT SET. Every enemy's plain attack
		// comes through the hit path too, and several test harnesses build a
		// component with fewer sets than a character has.
		return 0.0f;
	}

	// THE STAT BEFORE THE COMPARISON, because it is the cheaper question and it
	// is zero for every character in the game without the node. Asking the other
	// way round would walk two tag containers on every blow anybody strikes.
	const UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(Source);
	const float Percent = Cataclysm
		? Cataclysm->StatForSkill(FName(SharedDebuffDamageStat),
								  FGameplayTagContainer(),
								  Source->GetNumericAttribute(Attribute))
		: Source->GetNumericAttribute(Attribute);

	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	if (!ShareADebuff(Source, Target))
	{
		return 0.0f;
	}

	// A FRACTION, because the caller adds it into the increases bucket, which is
	// a sum of fractions. The stat holds a percentage because that is what the
	// node's sentence states.
	return Percent / 100.0f;
}

FGameplayTag UCataclysmDebuffs::BleedTag()
{
	return UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Keyword.DoT.Bleed")), /*ErrorIfNotFound=*/false);
}

bool UCataclysmDebuffs::IsBleeding(const UAbilitySystemComponent* AbilitySystem)
{
	const FGameplayTag Bleed = BleedTag();
	return AbilitySystem && Bleed.IsValid()
		&& AbilitySystem->HasMatchingGameplayTag(Bleed);
}

float UCataclysmDebuffs::DurationOn(const UAbilitySystemComponent* Defender,
								   float DurationSeconds)
{
	const FGameplayAttribute Attribute =
		UCataclysmCombatAttributeSet::GetDebuffDurationTakenAttribute();

	if (DurationSeconds <= 0.0f || !Defender
		|| !Defender->HasAttributeSetForAttribute(Attribute))
	{
		// NO COMBAT SET MEANS THE DURATION IS UNCHANGED, which is the ordinary
		// answer rather than a fault: a target that cannot hold the stat is not
		// a target that halves everything put on it.
		return DurationSeconds;
	}

	// ASKED FOR RATHER THAN READ, so a future row carrying a condition works.
	// Neither of the two rows today carries one, so the attribute holds the
	// same answer and both routes agree.
	//
	// NO SKILL TAGS. This is the DEFENDER'S stat, and the skill in the
	// attacker's hand is the wrong question to scope it by -- the same reason
	// the retaliation and damage-taken readings pass none either.
	const UCataclysmAbilitySystemComponent* Asking =
		Cast<const UCataclysmAbilitySystemComponent>(Defender);
	const float Held = Defender->GetNumericAttribute(Attribute);
	const float Percent = Asking
		? Asking->StatForSkill(FName(DurationStat), FGameplayTagContainer(),
							   Held)
		: Held;

	// FLOORED AT NOTHING RATHER THAN AT THE NORMAL DURATION. A stat of zero is
	// a legitimate future rule -- an effect that ends at once -- and a negative
	// one is not, because a duration below nothing would make
	// `ApplyTagForDuration` refuse the effect outright and read as immunity.
	// Nothing today reduces this stat: both rows raise it.
	return DurationSeconds * FMath::Max(0.0f, Percent) / NormalDuration;
}

bool UCataclysmDebuffs::DoNotExpireOn(
	const UAbilitySystemComponent* AbilitySystem)
{
	const UCataclysmAbilitySystemComponent* Asking =
		Cast<const UCataclysmAbilitySystemComponent>(AbilitySystem);
	if (!Asking)
	{
		return false;
	}

	// A FALLBACK OF ZERO AND NOT THE ATTRIBUTE, WHICH IS THE OPPOSITE OF WHAT
	// `DurationOn` ABOVE PASSES, and the difference is the condition. That stat
	// has a real base of 100 that every character carries, so its attribute
	// holds the right answer when no stat line has been recorded. This one is
	// granted only by a row carrying `health_above`, which is never folded into
	// an attribute, so the attribute is zero for everybody at all times and
	// passing it would say nothing. Zero is the honest fallback: an ability
	// system with no stat line does not hold the option.
	//
	// NO SKILL TAGS. This is about the character rather than about anything it
	// is casting, the same argument `DurationOn` makes.
	return Asking->StatForSkill(FName(DoNotExpireStat), FGameplayTagContainer(),
								0.0f) > 0.0f;
}

int32 UCataclysmDebuffs::HoldStep(AActor* Character, float StepSeconds)
{
	// A STEP OF NO TIME HOLDS NOTHING, rather than pushing every effect's start
	// backwards on a negative one, which would make them expire sooner.
	if (StepSeconds <= 0.0f)
	{
		return 0;
	}

	// A CORPSE IS SKIPPED, for the reason `UCataclysmHealthDebt::DrainIfDue`
	// gives: a creature is destroyed on the step AFTER it dies, so there is a
	// real window in which a dead one still stands there with an ability system
	// and a burn on it.
	if (!Character || UCataclysmSkillEffects::IsDead(Character))
	{
		return 0;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem || !DoNotExpireOn(AbilitySystem))
	{
		// EVERY CHARACTER IN THE GAME WITHOUT THAT CAPSTONE OPTION, and every
		// character holding it that is not above half health. Asked before the
		// effects are gathered, because gathering them is the expensive half.
		return 0;
	}

	const FGameplayTagContainer Roots = DebuffRoots();
	if (Roots.IsEmpty())
	{
		// THE VOCABULARY HAS LOST EVERY ROOT, which `DebuffRoots` allows rather
		// than failing to start the game. An empty container would match every
		// effect on the character, buffs included, so it is refused here.
		return 0;
	}

	// MATCHED AGAINST THE TAGS THE EFFECT GRANTS. Every lasting effect this
	// project applies attaches its tag through a
	// `UTargetTagsGameplayEffectComponent`, and an owning-tags query compares
	// against exactly those. The match honours parents, so naming `Keyword.DoT`
	// finds a bleed and a burn without listing either.
	const FGameplayEffectQuery Query =
		FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Roots);

	int32 Held = 0;
	for (const FActiveGameplayEffectHandle& Handle :
		 AbilitySystem->GetActiveEffects(Query))
	{
		// PUSHED FORWARD BY EXACTLY THE STEP, which is what makes the time
		// remaining stand still rather than grow. The engine adds this to the
		// effect's start time and re-arms its expiry.
		AbilitySystem->ModifyActiveEffectStartTime(Handle, StepSeconds);
		++Held;
	}

	if (Held > 0)
	{
		UE_LOG(LogCataclysm, VeryVerbose,
			   TEXT("%s held %d debuff(s) still for %.2f seconds."),
			   *GetNameSafe(Character), Held, StepSeconds);
	}

	return Held;
}
