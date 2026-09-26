// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmSkillBar.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Player/CataclysmPlayerController.h"

const TCHAR* UCataclysmSkillBar::ReadyHex = TEXT("2A2E38FF");
const TCHAR* UCataclysmSkillBar::CoolingHex = TEXT("0A0C10C8");
const TCHAR* UCataclysmSkillBar::UnaffordableHex = TEXT("3A1F22FF");

// A LOCKED BOX IS DARKER AND COLDER THAN AN UNPAYABLE ONE, and it has to differ
// from every other state here or the bar would have two states drawn the same.
// `ALockedBoxLooksDifferentFromEveryOtherState` is what holds that.
const TCHAR* UCataclysmSkillBar::LockedHex = TEXT("1B2430FF");

// THE WORDS ARE NOT WHITE, so they are not read as another skill name. A pale
// blue against the dark boxes below them.
const TCHAR* UCataclysmSkillBar::LockedNoticeInkHex = TEXT("9FC4E8FF");
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
			if (!Spec.GetDynamicSpecSourceTags().HasTagExact(SlotTag))
			{
				continue;
			}

			// THE GRANTED INSTANCE, NOT THE CLASS DEFAULT OBJECT, AND THIS LINE
			// USED TO BE THE OTHER WAY. Issue #1810. `Spec.Ability` is the class
			// default object, and everything that tells one granted skill from
			// another is stamped on the INSTANCE:
			// `UCataclysmWeaponSlotsComponent` writes `SkillName`,
			// `SkillDescription`, `Params`, `SkillTags` and `CritChancePercent`
			// onto `Spec->GetPrimaryInstance()`, with its own comment saying why
			// -- one class stands for every skill of that shape.
			//
			// WHAT IT COST BEFORE THE LOCK WORK FOUND IT: every box showed its
			// slot's generic name. `DisplayedName()` returns `SkillName`, which
			// is empty on the class default object, so `Read` fell through to
			// `NameForEmptySlot` every time and the bar read "Heavy", "Special",
			// "Support", "Aura", "Ultimate", "Movement" whatever the weapon had
			// granted. The fallback written to stop an empty box was covering
			// it, and nothing noticed because nothing called `Read` with a real
			// character until the tests for issue #1810 did.
			//
			// A CLASS DEFAULT OBJECT IS STILL BETTER THAN NOTHING when there is
			// no instance: an ability granted but never instanced still fills
			// its slot, and a box that vanished would be worse than one naming
			// its slot.
			if (const UCataclysmGameplayAbility* Instance =
					Cast<UCataclysmGameplayAbility>(Spec.GetPrimaryInstance()))
			{
				return Instance;
			}
			return Cast<UCataclysmGameplayAbility>(Spec.Ability);
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

bool UCataclysmSkillBar::CanAfford(const UAbilitySystemComponent* AbilitySystem,
								   const FGameplayAttribute& Pool, float ManaCost)
{
	if (ManaCost <= 0.0f)
	{
		return true;
	}
	if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Pool))
	{
		return true;
	}
	if (UCataclysmGameplayAbility::PoolCovers(AbilitySystem, Pool, ManaCost))
	{
		return true;
	}

	// AND WHAT THE CAST ITSELF WOULD ACCEPT, when the pool asked about is the one
	// the cast pays from: Cast from Ward pays a cost the mana cannot cover out of
	// the energy shield. Issue #1515.
	return Pool == UCataclysmGameplayAbility::CostPool(AbilitySystem)
		&& UCataclysmGameplayAbility::PoolPaying(AbilitySystem, ManaCost).IsValid();
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

bool UCataclysmSkillBar::IsLocked(float LockValue)
{
	return LockValue > 0.0f;
}

bool UCataclysmSkillBar::EverySkillIsLocked(const TArray<FCataclysmSkillBarSlot>& Slots)
{
	int32 Holding = 0;
	for (const FCataclysmSkillBarSlot& Slot : Slots)
	{
		if (!Slot.bFilled)
		{
			continue;
		}
		++Holding;
		if (!Slot.bLocked)
		{
			return false;
		}
	}

	// A CHARACTER WITH NO SKILLS AT ALL IS NOT SILENCED, they are unarmed. Saying
	// every skill is locked to somebody who has none would be true and useless,
	// and it would put the words on screen for the frames after a pawn appears
	// and before its weapon grants anything -- which is the fault issue #653 was
	// reported as, arriving from a new direction.
	return Holding > 0;
}

FString UCataclysmSkillBar::OwnStacksEntry(const TArray<FName>& Stats, int32 Held,
										  int32 Cap, bool bConsecutiveHits)
{
	const FName Attack(TEXT("attack_damage"));
	const FName Spell(TEXT("spell_damage"));
	FString Name;
	if (Stats.Num() == 2 && Stats.Contains(Attack) && Stats.Contains(Spell))
	{
		Name = TEXT("attack/spell damage");
	}
	else
	{
		TArray<FString> Words;
		for (const FName& Stat : Stats)
		{
			Words.Add(Stat.ToString().Replace(TEXT("_"), TEXT(" ")));
		}
		Name = FString::Join(Words, TEXT("/"));
	}
	return FString::Printf(TEXT("%s %d/%d%s"), *Name, Held, Cap,
						   bConsecutiveHits ? TEXT(" (hits in a row)") : TEXT(""));
}

FString UCataclysmSkillBar::StoredDamageLine(float Stored)
{
	if (Stored <= 0.0f)
	{
		return FString();
	}
	return FString::Printf(TEXT("Next melee +%d"),
						   FMath::Max(1, FMath::RoundToInt(Stored)));
}

FString UCataclysmSkillBar::NextSpellCooldownLine(float Seconds)
{
	if (Seconds <= 0.0f)
	{
		return FString();
	}
	return FString::Printf(TEXT("Next spell cooldown -%.1fs"), Seconds);
}

FString UCataclysmSkillBar::NthEntry(ECataclysmEveryNth Kind, int32 Count,
									int32 EveryNth)
{
	const TCHAR* Name = Kind == ECataclysmEveryNth::HitTaken ? TEXT("Hit taken")
		: Kind == ECataclysmEveryNth::SpellCast ? TEXT("Spell")
		: Kind == ECataclysmEveryNth::Attack ? TEXT("Attack")
		: nullptr;
	return Name ? FString::Printf(TEXT("%s %d/%d"), Name, Count, EveryNth) : FString();
}

FString UCataclysmSkillBar::FollowThroughLine(float SecondsLeft)
{
	if (SecondsLeft <= 0.0f)
	{
		return FString();
	}
	return FString::Printf(TEXT("Follow Through %ds"),
						   FMath::Max(1, FMath::CeilToInt(SecondsLeft)));
}

FString UCataclysmSkillBar::NextUseLine(float SkillPercent, int32 SkillCount,
									   float AttackPercent, int32 AttackCount,
									   float EffectivenessPercent,
									   const TArray<FString>& OwnStacks)
{
	const auto Entry = [](const TCHAR* Kind, float Percent, int32 Count)
	{
		FString Text = FString::Printf(TEXT("Next %s +%d%%"), Kind,
									   FMath::RoundToInt(Percent));
		if (Count > 1)
		{
			Text += FString::Printf(TEXT(" (%d)"), Count);
		}
		return Text;
	};

	TArray<FString> Entries;
	if (SkillCount > 0)
	{
		Entries.Add(Entry(TEXT("skill"), SkillPercent, SkillCount));
	}
	if (AttackCount > 0)
	{
		Entries.Add(Entry(TEXT("attack"), AttackPercent, AttackCount));
	}
	if (EffectivenessPercent > 0.0f)
	{
		Entries.Add(FString::Printf(TEXT("Next skill %d%% effectiveness"),
									 FMath::RoundToInt(EffectivenessPercent)));
	}
	Entries.Append(OwnStacks);
	return FString::Join(Entries, TEXT("   "));
}

FString UCataclysmSkillBar::LockedNotice()
{
	// BASIC ATTACKS ARE NAMED BECAUSE THEY STILL WORK. The bar draws no box for
	// that slot -- `SlotsShown` leaves it out, because it has no key and no
	// cooldown -- so the one thing a silenced player can still do is the one
	// thing nothing on the bar shows. Saying it here is the only place it gets
	// said.
	return TEXT("SKILLS LOCKED -- BASIC ATTACKS STILL WORK");
}

FLinearColor UCataclysmSkillBar::TintFor(const FCataclysmSkillBarSlot& Slot)
{
	if (!Slot.bFilled)
	{
		return UCataclysmCombatOverlay::ColourFromHex(EmptyHex);
	}

	// LOCKED IS SHOWN BEFORE UNPAYABLE, and the order matters for the same kind
	// of reason the next one gives. A locked skill is refused at every mana
	// level, so the lock is the fact that decides whether the box can be used;
	// paying for it would change nothing. The reverse order would tell a silenced
	// player to go and find mana.
	if (Slot.bLocked)
	{
		return UCataclysmCombatOverlay::ColourFromHex(LockedHex);
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

	// WHICH POOL PAYS IS ASKED ONCE FOR THE WHOLE BAR rather than per slot,
	// because every box is asking the same character the same question and the
	// answer cannot change between two boxes of one frame. It is the question
	// the cast asks, so a character whose mana pool became health is shown what
	// it can pay from health. Issue #1910.
	const FGameplayAttribute Pool = UCataclysmGameplayAbility::CostPool(Abilities);

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

		// WHAT THIS CHARACTER PAYS, NOT WHAT THE SLOT STATES. Issue #1815. The
		// box shows this number and greys itself out by it, so a bar left on
		// `GetManaCost` would show a price the cast does not charge -- and would
		// grey out a skill a row had made free.
		Box.ManaCost = Ability->ManaCostFor(Abilities);

		// `CanAfford` says why a character with no pool to read yet can afford
		// everything, which is issue #653.
		//
		// AND A SKILL WHOSE COST IS PAID IN HEALTH INSTEAD IS AFFORDABLE, the same
		// answer `UCataclysmGameplayAbility::CheckCost` gives. Issues #1820 and
		// #41: the dungeon floor rule `Famine_Desperate_Measures` moves the cost
		// onto current health below 10% mana, which is exactly when a mana check
		// alone would grey the box out.
		Box.bAffordable = Ability->ManaCostPaidAsHealthPercent(Abilities) > 0.0f
			|| CanAfford(Abilities, Pool, Box.ManaCost);

		// THE LOCK IS ASKED PER BOX, WITH THIS SKILL'S OWN TAGS. Issue #1810.
		// Unlike mana above, this is not one answer for the character:
		// `StatForSkill` scopes each modifier by the tags it is given, which is
		// what lets one stat serve both an enchantment that locks the Movement
		// slot and a dungeon rule that locks everything. Asking once with an
		// empty container would draw those two the same, and the first is what
		// the two rows in `game/Data/EnchantmentEffects.csv` do today.
		//
		// THE SAME CALL THE REFUSAL MAKES, deliberately.
		// `UCataclysmSkillTemplate::CanActivateAbility` asks
		// `StatForSkill(LockedStat, SkillTags, 0.0f) > 0.0f`; a bar that decided
		// it some other way would eventually disagree with the game.
		//
		// A FALLBACK OF ZERO MEANS AN UNKNOWN CHARACTER IS NOT LOCKED, matching
		// that refusal's own fallback and for its reason: a player's ability
		// system has no stat line before its first refresh, and greying out every
		// box for those frames would look like the fault this change is fixing.
		if (const UCataclysmSkillTemplate* Skill = Cast<UCataclysmSkillTemplate>(Ability))
		{
			if (const UCataclysmAbilitySystemComponent* Cataclysm =
					Cast<const UCataclysmAbilitySystemComponent>(Abilities))
			{
				Box.bLocked = IsLocked(Cataclysm->StatForSkill(
					FName(UCataclysmSkillSlots::LockedStat), Skill->SkillTags, 0.0f));
			}
		}

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
