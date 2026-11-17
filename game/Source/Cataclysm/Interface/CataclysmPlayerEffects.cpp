// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmPlayerEffects.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAilments.h"
#include "AbilitySystem/CataclysmDebuffs.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Dungeon/CataclysmDungeonGameMode.h"
#include "Dungeon/CataclysmDungeonModifierEffects.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
// For FGameplayEffectQuery, which is how the effects granting a tag are found and their time read.
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"

const TCHAR* UCataclysmPlayerEffects::DebuffInkHex = TEXT("FF6B5B");
const TCHAR* UCataclysmPlayerEffects::BuffInkHex = TEXT("FFFFFF");

namespace
{
	/** A tag asked for by name and not required to exist, the way every tag in this module is fetched. */
	FGameplayTag PlayerEffectsTag(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	/**
	 * The seconds the longest-lasting effect granting `Tag` has left, or -1 when none is timed: a loose tag, an
	 * effect lasting for ever, or none at all. The engine answers a negative for an effect with no end.
	 *
	 * LONGEST WINS, the rule the skill bar's cooldown read keeps: two effects granting one tag leave the player under
	 * it until the later ends, and a list showing the shorter would say it ends sooner than it does.
	 */
	float SecondsLeftOn(const UAbilitySystemComponent* AbilitySystem, const FGameplayTag& Tag)
	{
		if (!AbilitySystem || !Tag.IsValid())
		{
			return -1.0f;
		}
		float Longest = -1.0f;
		for (const float Left : AbilitySystem->GetActiveEffectsTimeRemaining(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
		{
			Longest = FMath::Max(Longest, Left);
		}
		return Longest > 0.0f ? Longest : -1.0f;
	}

	/**
	 * The stacks held under `Tag`: the stack counts of every effect granting it, added; for a tag with no effect
	 * behind it, how many times it is held. 0 for one, which shows no count.
	 */
	int32 StacksOn(const UAbilitySystemComponent* AbilitySystem, const FGameplayTag& Tag)
	{
		if (!AbilitySystem || !Tag.IsValid())
		{
			return 0;
		}
		int32 Held = 0;
		for (const FActiveGameplayEffectHandle& Handle : AbilitySystem->GetActiveEffects(
				 FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
		{
			Held += AbilitySystem->GetCurrentStackCount(Handle);
		}
		if (Held == 0)
		{
			Held = AbilitySystem->GetTagCount(Tag);
		}
		return Held > 1 ? Held : 0;
	}

	/** An entry for a tag the player holds, named by `NameForTag`, with its count and its time. */
	FCataclysmPlayerEffect EntryForTag(const UAbilitySystemComponent* AbilitySystem, const FGameplayTag& Tag,
									   bool bDebuff)
	{
		FCataclysmPlayerEffect Entry;
		Entry.Name = UCataclysmPlayerEffects::NameForTag(Tag);
		Entry.bDebuff = bDebuff;
		Entry.Stacks = StacksOn(AbilitySystem, Tag);
		Entry.SecondsLeft = SecondsLeftOn(AbilitySystem, Tag);
		return Entry;
	}

	/**
	 * An entry carrying a text function's words as they are, or nothing when the function said nothing. `TagName`
	 * is the tag those words are about, whose seconds left are added; null for words with no tag behind them, or
	 * words that already say their own seconds ("Commander 12s").
	 */
	void AddText(TArray<FCataclysmPlayerEffect>& Into, const FString& Text, bool bDebuff,
				 const UAbilitySystemComponent* AbilitySystem, const TCHAR* TagName)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		FCataclysmPlayerEffect Entry;
		Entry.Name = Text;
		Entry.bDebuff = bDebuff;
		Entry.SecondsLeft = TagName ? SecondsLeftOn(AbilitySystem, PlayerEffectsTag(TagName)) : -1.0f;
		Into.Add(Entry);
	}

	/**
	 * The tags a text function already names, which the tag lists below leave out so nothing is shown twice. One
	 * place, beside the calls that name them.
	 */
	const TCHAR* const NamedByAText[] = {
		TEXT("State.Feared"),
		TEXT("State.Stunned"),
		TEXT("Status.Debuff.Madness"),
		TEXT("Status.Debuff.Cripple"),
		TEXT("Status.Debuff.Weaken"),
		TEXT("Status.Debuff.Shred"),
		TEXT("Status.Debuff.Withering"),
		TEXT("Status.Debuff.Wither"),
		TEXT("Status.Debuff.TouchofNothing"),
	};

	bool IsNamedByAText(const FGameplayTag& Tag)
	{
		for (const TCHAR* Name : NamedByAText)
		{
			if (Tag == PlayerEffectsTag(Name))
			{
				return true;
			}
		}
		return false;
	}

	/** The tags in `Tags`, in the order of their names, so an entry keeps its place while others come and go. */
	TArray<FGameplayTag> InNameOrder(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> Ordered;
		Tags.GetGameplayTagArray(Ordered);
		Ordered.Sort([](const FGameplayTag& A, const FGameplayTag& B)
		{
			return A.ToString() < B.ToString();
		});
		return Ordered;
	}
}

FString UCataclysmPlayerEffects::NameForTag(const FGameplayTag& Tag)
{
	// THE AILMENT'S OWN NAME FIRST, so "Void Splinter" reads as the affix sheet and the status table call it.
	for (const FCataclysmAilmentKind& Kind : UCataclysmAilments::Kinds())
	{
		if (Tag == PlayerEffectsTag(Kind.TagName))
		{
			return FString(Kind.Ailment);
		}
	}

	// ELSE THE LAST SEGMENT, SPACED AT EACH WORD. "State.KnockedDown" reads "Knocked Down".
	FString Leaf = Tag.ToString();
	int32 Dot = INDEX_NONE;
	if (Leaf.FindLastChar(TEXT('.'), Dot))
	{
		Leaf = Leaf.RightChop(Dot + 1);
	}
	FString Spaced;
	for (int32 Index = 0; Index < Leaf.Len(); ++Index)
	{
		if (Index > 0 && FChar::IsUpper(Leaf[Index]) && FChar::IsLower(Leaf[Index - 1]))
		{
			Spaced.AppendChar(TEXT(' '));
		}
		Spaced.AppendChar(Leaf[Index]);
	}
	return Spaced;
}

TArray<FCataclysmPlayerEffect> UCataclysmPlayerEffects::EntriesFor(const AActor* Actor)
{
	TArray<FCataclysmPlayerEffect> Entries;
	const UAbilitySystemComponent* AbilitySystem =
		Actor ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor) : nullptr;
	if (!AbilitySystem || UCataclysmSkillEffects::IsDead(Actor))
	{
		return Entries;
	}

	using Overlay = UCataclysmCombatOverlay;

	// THE TAGS UNDER THE DEBUFF ROOTS, AS THE DEBUFF COUNT READS THEM, split into damage over time and the rest. A
	// tag a text names is left to that text.
	const FGameplayTag DamageOverTime = PlayerEffectsTag(TEXT("Keyword.DoT"));
	FGameplayTagContainer DoTTags;
	FGameplayTagContainer OtherDebuffTags;
	for (const FGameplayTag& Tag : UCataclysmDebuffs::TagsOn(AbilitySystem))
	{
		if (IsNamedByAText(Tag))
		{
			continue;
		}
		(DamageOverTime.IsValid() && Tag.MatchesTag(DamageOverTime) ? DoTTags : OtherDebuffTags).AddTag(Tag);
	}

	// EVERY TAG UNDER `Status.Buff`, read from the owned tags as the debuffs are, one per tag.
	FGameplayTagContainer BuffTags;
	const FGameplayTag BuffRoot = PlayerEffectsTag(TEXT("Status.Buff"));
	if (BuffRoot.IsValid())
	{
		FGameplayTagContainer Owned;
		AbilitySystem->GetOwnedGameplayTags(Owned);
		for (const FGameplayTag& Tag : Owned)
		{
			if (Tag.MatchesTag(BuffRoot) && Tag != BuffRoot)
			{
				BuffTags.AddTag(Tag);
			}
		}
	}

	// AND THE FLOOR'S RULES, from the dungeon game mode in this world when there is one.
	ACataclysmDungeonGameMode* Floor = ACataclysmDungeonGameMode::InWorld(Actor->GetWorld());
	int32 ForcedMarchStacks = 0;
	float NihilLess = 0.0f;
	float NihilMore = 0.0f;
	float NihilMoreSecondsLeft = 0.0f;
	if (Floor)
	{
		// FORCED MARCH: THE STACKS THE NEXT BEAT DEALS, worked out the way `StepForcedMarch` works them out.
		const UCataclysmAbilitySystemComponent* Cataclysm = Cast<UCataclysmAbilitySystemComponent>(AbilitySystem);
		if (Cataclysm && Floor->FloorBrief.Modifiers.Contains(FName(UCataclysmDungeonModifierEffects::ForcedMarchKey)))
		{
			ForcedMarchStacks = UCataclysmDungeonModifierEffects::ForcedMarchStacksAfter(Cataclysm->SecondsSinceMoved());
		}
		Floor->NihilsEmbraceShownNow(NihilLess, NihilMore, NihilMoreSecondsLeft);
	}

	// ------------------------------------------------------------------------------------------------------------
	// THE DEBUFFS. What stops the player acting first, then what is taking health, then the rest. A judgement.
	// ------------------------------------------------------------------------------------------------------------
	TArray<FCataclysmPlayerEffect> Debuffs;

	// WHAT HOLDS THE PLAYER: fear and a stun by their texts, then the three states that are not debuffs.
	AddText(Debuffs, Overlay::FearedTextFor(Actor), true, AbilitySystem, TEXT("State.Feared"));
	AddText(Debuffs, Overlay::StunnedTextFor(Actor), true, AbilitySystem, TEXT("State.Stunned"));
	for (const FGameplayTag& State : {UCataclysmSkillEffects::KnockedDownTag(), UCataclysmSkillEffects::PinnedTag(),
									  UCataclysmSkillEffects::StaggeredTag()})
	{
		if (State.IsValid() && AbilitySystem->HasMatchingGameplayTag(State))
		{
			Debuffs.Add(EntryForTag(AbilitySystem, State, true));
		}
	}

	// WHAT IS TAKING HEALTH: each damage over time, then Forced March, then the delayed damage still owed.
	for (const FGameplayTag& Tag : InNameOrder(DoTTags))
	{
		Debuffs.Add(EntryForTag(AbilitySystem, Tag, true));
	}
	if (ForcedMarchStacks > 0)
	{
		FCataclysmPlayerEffect March;
		March.Name = TEXT("Forced March");
		March.bDebuff = true;
		March.Stacks = ForcedMarchStacks;
		March.MaxStacks = UCataclysmDungeonModifierEffects::ForcedMarchMostStacks;
		Debuffs.Add(March);
	}
	AddText(Debuffs, Overlay::OwedTextFor(Actor), true, AbilitySystem, nullptr);

	// THE REST: the named debuffs the line above the bars named before, in the order it named them, then every other
	// named debuff, then The Nihil's Embrace's loss.
	AddText(Debuffs, Overlay::MaddenedTextFor(Actor), true, AbilitySystem, TEXT("Status.Debuff.Madness"));
	AddText(Debuffs, Overlay::CrippledTextFor(Actor), true, AbilitySystem, TEXT("Status.Debuff.Cripple"));
	AddText(Debuffs, Overlay::WeakenedTextFor(Actor), true, AbilitySystem, TEXT("Status.Debuff.Weaken"));
	AddText(Debuffs, Overlay::ShreddedTextFor(Actor), true, AbilitySystem, TEXT("Status.Debuff.Shred"));
	AddText(Debuffs, Overlay::WitheringTextFor(Actor), true, AbilitySystem, TEXT("Status.Debuff.Withering"));
	AddText(Debuffs, Overlay::WitheredTextFor(Actor), true, AbilitySystem, TEXT("Status.Debuff.Wither"));
	AddText(Debuffs, Overlay::TouchOfNothingTextFor(Actor), true, AbilitySystem, TEXT("Status.Debuff.TouchofNothing"));
	AddText(Debuffs, Overlay::NegatedTextFor(Actor), true, AbilitySystem, nullptr);
	for (const FGameplayTag& Tag : InNameOrder(OtherDebuffTags))
	{
		Debuffs.Add(EntryForTag(AbilitySystem, Tag, true));
	}
	if (NihilLess > 0.0f)
	{
		FCataclysmPlayerEffect Loss;
		Loss.Name = FString::Printf(TEXT("Nihil's Embrace -%d%% resistances"), FMath::RoundToInt(NihilLess));
		Loss.bDebuff = true;
		Debuffs.Add(Loss);
	}

	// ------------------------------------------------------------------------------------------------------------
	// THE BUFFS. What a worn row gives first, as the line above the bars said it, then the floor's reward, then every
	// `Status.Buff` tag, which is where the player's own skills' buffs are.
	// ------------------------------------------------------------------------------------------------------------
	TArray<FCataclysmPlayerEffect> Buffs;
	AddText(Buffs, Overlay::HeldBuffTextFor(Actor), false, AbilitySystem, nullptr);
	AddText(Buffs, Overlay::DamageTypeImmunityTextFor(Actor), false, AbilitySystem, nullptr);
	AddText(Buffs, Overlay::KeptStacksTextFor(Actor), false, AbilitySystem, nullptr);
	if (NihilMore > 0.0f)
	{
		FCataclysmPlayerEffect Reward;
		Reward.Name = FString::Printf(TEXT("Nihil's Embrace +%d%% resistances"), FMath::RoundToInt(NihilMore));
		Reward.SecondsLeft = NihilMoreSecondsLeft > 0.0f ? NihilMoreSecondsLeft : -1.0f;
		Buffs.Add(Reward);
	}
	for (const FGameplayTag& Tag : InNameOrder(BuffTags))
	{
		Buffs.Add(EntryForTag(AbilitySystem, Tag, false));
	}

	// DEBUFFS FIRST, THEN BUFFS. Ruled 2026-11-01: the research's one settled point on order.
	Entries = MoveTemp(Debuffs);
	Entries.Append(Buffs);
	return Entries;
}

FString UCataclysmPlayerEffects::TextFor(const FCataclysmPlayerEffect& Entry)
{
	FString Text = Entry.Name;
	if (Entry.Stacks > 0)
	{
		Text += Entry.MaxStacks > 0 ? FString::Printf(TEXT(" %d/%d"), Entry.Stacks, Entry.MaxStacks)
									: FString::Printf(TEXT(" x%d"), Entry.Stacks);
	}
	if (Entry.SecondsLeft > 0.0f)
	{
		// ROUNDED UP, so the last second reads "1s" and a thing still there never reads "0s".
		Text += FString::Printf(TEXT(" %ds"), FMath::Max(1, FMath::CeilToInt(Entry.SecondsLeft)));
	}
	return Text;
}

TArray<FCataclysmPlayerEffect> UCataclysmPlayerEffects::ShownFrom(const TArray<FCataclysmPlayerEffect>& Entries)
{
	if (Entries.Num() <= MostShown)
	{
		return Entries;
	}
	TArray<FCataclysmPlayerEffect> Shown(Entries.GetData(), MostShown);
	FCataclysmPlayerEffect More;
	More.Name = FString::Printf(TEXT("+%d more"), Entries.Num() - MostShown);
	Shown.Add(More);
	return Shown;
}
