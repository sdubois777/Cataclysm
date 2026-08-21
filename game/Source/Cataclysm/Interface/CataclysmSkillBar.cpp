// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmSkillBar.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Player/CataclysmPlayerController.h"

const TCHAR* UCataclysmSkillBar::ReadyHex = TEXT("2A2E38FF");
const TCHAR* UCataclysmSkillBar::CoolingHex = TEXT("0A0C10C8");
const TCHAR* UCataclysmSkillBar::UnaffordableHex = TEXT("3A1F22FF");
const TCHAR* UCataclysmSkillBar::EmptyHex = TEXT("16181EFF");
const TCHAR* UCataclysmSkillBar::BoxEdgeHex = TEXT("0A0B0EFF");

namespace
{
	/**
	 * Whether the skill bar is drawn.
	 *
	 * NAMED FOR THIS FILE, like every other console variable in this module.
	 * Unreal merges a module's `.cpp` files into one translation unit, so two
	 * files declaring the same file-scope name collide, and only once both are
	 * committed.
	 */
	static TAutoConsoleVariable<int32> CVarCataclysmShowSkillBar(
		TEXT("Cataclysm.ShowSkillBar"),
		1,
		TEXT("Draw the player's skill bar along the bottom of the screen. "
			 "0 hides it."),
		ECVF_Default);

	/** The ability granted into a slot, or null. Named for this file. */
	const UCataclysmGameplayAbility* CataclysmSkillBarAbilityIn(
		const UAbilitySystemComponent* Abilities, ECataclysmAbilitySlot Slot)
	{
		const FGameplayTag SlotTag = CataclysmAbilitySlots::Tag(Slot);
		if (!Abilities || !SlotTag.IsValid())
		{
			return nullptr;
		}

		// THE SLOT IS ON THE SPEC AND NOT ON THE ABILITY CLASS, because the
		// equipped weapon decides which skill sits in each slot:
		// `UCataclysmAbilitySystemComponent::GiveAbilityInSlot` stamps the
		// Slot.* tag onto the granted spec. Reading the class's own declared
		// slot instead would find nothing, because one placeholder class stands
		// in for six different slots.
		for (const FGameplayAbilitySpec& Spec : Abilities->GetActivatableAbilities())
		{
			if (Spec.GetDynamicSpecSourceTags().HasTagExact(SlotTag))
			{
				return Cast<UCataclysmGameplayAbility>(Spec.Ability);
			}
		}

		return nullptr;
	}

	/** The player controller driving a pawn, or null. Named for this file. */
	const ACataclysmPlayerController* CataclysmSkillBarControllerOf(const AActor* Player)
	{
		const APawn* Pawn = Cast<APawn>(Player);
		return Pawn ? Cast<ACataclysmPlayerController>(Pawn->GetController()) : nullptr;
	}
}

TArray<ECataclysmAbilitySlot> UCataclysmSkillBar::SlotsShown()
{
	// EVERY SLOT A PLAYER PRESSES, IN THE DESIGN DOCUMENT'S ORDER. The Basic
	// Attack is deliberately absent -- see the header for why.
	return {
		ECataclysmAbilitySlot::Heavy,
		ECataclysmAbilitySlot::Special,
		ECataclysmAbilitySlot::Support,
		ECataclysmAbilitySlot::Aura,
		ECataclysmAbilitySlot::Ultimate,
		ECataclysmAbilitySlot::Movement,
	};
}

float UCataclysmSkillBar::BarWidthFor(int32 Count)
{
	if (Count <= 0)
	{
		return 0.0f;
	}

	return Count * BoxSizePx + (Count - 1) * BoxGapPx;
}

FVector2D UCataclysmSkillBar::BoxOriginFor(int32 Index, int32 Count,
										   float ViewportWidth, float ViewportHeight)
{
	const float Width = BarWidthFor(Count);
	const float Left = (ViewportWidth - Width) * 0.5f;
	const float Top = ViewportHeight - BottomMarginPx - BoxSizePx;

	return FVector2D(Left + Index * (BoxSizePx + BoxGapPx), Top);
}

float UCataclysmSkillBar::CooldownFractionFor(float Remaining, float Duration)
{
	if (Duration <= 0.0f || Remaining <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Clamp(Remaining / Duration, 0.0f, 1.0f);
}

FString UCataclysmSkillBar::CooldownTextFor(float Remaining)
{
	if (Remaining <= 0.0f)
	{
		return FString();
	}

	// TENTHS BELOW TEN SECONDS, WHOLE SECONDS ABOVE. See the header for why.
	if (Remaining < 10.0f)
	{
		return FString::Printf(TEXT("%.1f"), Remaining);
	}

	// ROUNDED UP, because rounding down would show "12" for a wait of 12.9
	// seconds and then show "12" again a second later. Counting down never shows
	// the same number twice this way, and it never claims to be over early.
	return FString::Printf(TEXT("%d"), FMath::CeilToInt(Remaining));
}

bool UCataclysmSkillBar::CanAfford(float ManaCost, float Mana)
{
	return Mana >= ManaCost;
}

FString UCataclysmSkillBar::KeyTextFor(const FKey& Key)
{
	if (!Key.IsValid())
	{
		return FString();
	}

	// THE ENGINE'S SHORT NAME, AND A HAND-WRITTEN TABLE WAS TRIED FIRST AND
	// DELETED. `FKey` carries two names: `GetDisplayName(true)` gives "Right
	// Mouse Button", "Space Bar" and "One", none of which fits a 56 pixel box,
	// and `GetDisplayName(false)` gives "RMB", "Space" and "1".
	//
	// THE TABLE WAS FOUND TO BE DEAD BY BREAKING IT. It listed the mouse
	// buttons, the space bar and the number row with the spellings this
	// interface wanted; removing it and running the tests changed nothing at
	// all, because the short names already were those spellings. It was fifteen
	// lines that could drift from the engine without anything noticing.
	//
	// WHAT HOLDS THE ENGINE TO IT is `Cataclysm.SkillBar.EveryKeyTheGameBindsFitsInABox`,
	// which names the four spellings this interface depends on rather than
	// trusting them.
	return Key.GetDisplayName(/*bLongDisplayName=*/false).ToString();
}

FString UCataclysmSkillBar::NameForEmptySlot(ECataclysmAbilitySlot Slot)
{
	switch (Slot)
	{
	case ECataclysmAbilitySlot::BasicAttack:	return TEXT("Basic");
	case ECataclysmAbilitySlot::Heavy:			return TEXT("Heavy");
	case ECataclysmAbilitySlot::Special:		return TEXT("Special");
	case ECataclysmAbilitySlot::Support:		return TEXT("Support");
	case ECataclysmAbilitySlot::Aura:			return TEXT("Aura");
	case ECataclysmAbilitySlot::Ultimate:		return TEXT("Ultimate");
	case ECataclysmAbilitySlot::Movement:		return TEXT("Movement");
	default:									return FString();
	}
}

FString UCataclysmSkillBar::ShortNameFor(const FString& Name)
{
	if (Name.Len() <= MostNameCharacters)
	{
		return Name;
	}

	// THE MARK COUNTS TOWARD THE LIMIT, so a shortened name is never wider than
	// one that fitted. A full stop rather than an ellipsis character, because the
	// heads-up display's font is the engine's default and nothing has checked
	// that it carries one.
	return Name.Left(MostNameCharacters - 1) + TEXT(".");
}

FLinearColor UCataclysmSkillBar::TintFor(const FCataclysmSkillBarSlot& Slot)
{
	if (!Slot.bFilled)
	{
		return UCataclysmCombatOverlay::ColourFromHex(EmptyHex);
	}

	// UNAFFORDABLE IS SHOWN BEFORE THE WAIT, and the order matters. A skill that
	// is both waiting and unpayable will be unpayable for longer than it waits,
	// so that is the fact worth showing; the wait draws its own sweep over the
	// top of whichever colour this is.
	if (!Slot.bAffordable)
	{
		return UCataclysmCombatOverlay::ColourFromHex(UnaffordableHex);
	}

	return UCataclysmCombatOverlay::ColourFromHex(ReadyHex);
}

bool UCataclysmSkillBar::Enabled()
{
	return CVarCataclysmShowSkillBar.GetValueOnAnyThread() != 0;
}

TArray<FCataclysmSkillBarSlot> UCataclysmSkillBar::Read(const AActor* Player)
{
	TArray<FCataclysmSkillBarSlot> Bar;
	if (!Player)
	{
		return Bar;
	}

	const UAbilitySystemComponent* Abilities =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Player);

	// MANA IS READ ONCE FOR THE WHOLE BAR rather than per slot, because every
	// box is asking the same character the same question and the answer cannot
	// change between two boxes of one frame.
	float Mana = 0.0f;
	float MaxMana = 0.0f;
	const bool bHasMana = UCataclysmCombatOverlay::ManaOf(Player, Mana, MaxMana);

	const ACataclysmPlayerController* Controller = CataclysmSkillBarControllerOf(Player);

	for (const ECataclysmAbilitySlot Slot : SlotsShown())
	{
		FCataclysmSkillBarSlot Box;
		Box.Slot = Slot;

		if (Controller)
		{
			Box.Key = KeyTextFor(
				Controller->KeyForAbilitySlot(CataclysmAbilitySlots::Tag(Slot)));
		}

		const UCataclysmGameplayAbility* Ability =
			CataclysmSkillBarAbilityIn(Abilities, Slot);

		if (!Ability)
		{
			// AN EMPTY SLOT STILL GETS A BOX. See `Read` in the header: the bar
			// must not change width when a weapon is swapped.
			Box.Name = NameForEmptySlot(Slot);
			Bar.Add(Box);
			continue;
		}

		Box.bFilled = true;
		Box.Name = Ability->DisplayedName();
		if (Box.Name.IsEmpty())
		{
			Box.Name = NameForEmptySlot(Slot);
		}

		Box.ManaCost = Ability->GetManaCost();

		// A CHARACTER WITH NO MANA POOL CAN AFFORD EVERYTHING. That is not a
		// guess: `UCataclysmCombatOverlay::ManaOf` answers false when there is no
		// ability system to ask yet, which happens for some frames after a pawn
		// appears, and greying out every skill for those frames would look like
		// the fault issue #653 was reported as.
		Box.bAffordable = !bHasMana || CanAfford(Box.ManaCost, Mana);

		const FGameplayTag CooldownTag = UCataclysmSkillSlots::CooldownTag(Slot);
		if (Abilities && CooldownTag.IsValid())
		{
			const FGameplayEffectQuery Query =
				FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
					FGameplayTagContainer(CooldownTag));

			// LONGEST WAIT WINS. There is normally exactly one cooldown effect
			// per slot, but nothing in the ability system forbids two, and a bar
			// that showed the shorter of them would say a skill was ready while
			// it was refused.
			for (const TPair<float, float>& Wait :
					Abilities->GetActiveEffectsTimeRemainingAndDuration(Query))
			{
				if (Wait.Key > Box.CooldownRemaining)
				{
					Box.CooldownRemaining = Wait.Key;
					Box.CooldownDuration = Wait.Value;
				}
			}
		}

		Bar.Add(Box);
	}

	return Bar;
}
