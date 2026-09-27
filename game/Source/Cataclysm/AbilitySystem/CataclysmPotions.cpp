// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmPotions.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Cataclysm.h"

namespace
{
	/**
	 * `Cataclysm.ShowPotions`. NAMED FOR THIS FILE, because Unreal merges a
	 * module's `.cpp` files into one translation unit and two files declaring
	 * the same file-scope name collide.
	 */
	static TAutoConsoleVariable<int32> CVarCataclysmShowPotions(
		TEXT("Cataclysm.ShowPotions"),
		1,
		TEXT("Draw the four potion slots in the bottom-right corner of the "
			 "screen. 0 hides them."),
		ECVF_Default);
}

const TCHAR* UCataclysmPotions::ForbiddenStat = TEXT("potions_forbidden");
const TCHAR* UCataclysmPotions::KillChargesLessStat = TEXT("potion_kill_charges_less_percent");
const TCHAR* UCataclysmPotions::HealLessPerDrinkStat =
	TEXT("potion_heal_less_percent_per_drink");

UCataclysmAbilitySystemComponent* UCataclysmPotions::PotionHolderOf(const AActor* Character)
{
	// ONLY A PLAYER CHARACTER HOLDS POTIONS. Every character's ability system
	// has the four counts, and a creature killing a minion goes through the same
	// death, so the rule refuses anything that is not a player rather than
	// relying on nobody asking.
	if (!Cast<ACataclysmPlayerCharacter>(Character))
	{
		return nullptr;
	}
	return Cast<UCataclysmAbilitySystemComponent>(
		UCataclysmTargeting::AbilitySystemOf(Character));
}

float UCataclysmPotions::ChargesForKillOf(int32 RarityStep)
{
	// Common, Elite, Legendary, Herald, Boss, Cataclysm Boss. Path of Exile's
	// normal, magic, rare and unique figures, the unique one covering the top
	// three rungs. See the header.
	static constexpr float ByRung[] = {1.0f, 3.5f, 6.0f, 11.0f, 11.0f, 11.0f};
	if (RarityStep < 0 || RarityStep >= static_cast<int32>(UE_ARRAY_COUNT(ByRung)))
	{
		return ByRung[0];
	}
	return ByRung[RarityStep];
}

float UCataclysmPotions::NoteEnemyKilled(AActor* Killer, int32 RarityStep)
{
	UCataclysmAbilitySystemComponent* Holder = PotionHolderOf(Killer);
	if (!Holder)
	{
		return 0.0f;
	}

	// RECESSION TAKES A SHARE OFF EVERY KILL, so four kills fill what one did.
	// Asked with no tags: a potion is not a skill.
	const float Less = FMath::Clamp(
		Holder->StatForSkill(FName(KillChargesLessStat), FGameplayTagContainer(), 0.0f),
		0.0f, 100.0f);
	const float Added = ChargesForKillOf(RarityStep) * (100.0f - Less) / 100.0f;
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		Holder->SetPotionCharges(Slot, Holder->GetPotionCharges(Slot) + Added);
	}
	return Added;
}

ECataclysmPotionRefusal UCataclysmPotions::Drink(AActor* Character, int32 Slot)
{
	UCataclysmAbilitySystemComponent* Holder = PotionHolderOf(Character);
	const UCataclysmVitalAttributeSet* Vitals =
		Holder ? Holder->GetSet<UCataclysmVitalAttributeSet>() : nullptr;
	if (!Holder || !Vitals)
	{
		return ECataclysmPotionRefusal::NoCharacter;
	}
	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return ECataclysmPotionRefusal::Dead;
	}
	if (Slot < 0 || Slot >= SlotCount)
	{
		return ECataclysmPotionRefusal::NoSuchSlot;
	}
	// HARD MODE FIRST AMONG WHAT THE SLOT HOLDS, so a player on such a floor is
	// told the floor forbids it rather than that the slot is short.
	if (Holder->StatForSkill(FName(ForbiddenStat), FGameplayTagContainer(), 0.0f) > 0.0f)
	{
		return ECataclysmPotionRefusal::Forbidden;
	}
	if (Holder->GetPotionCharges(Slot) < ChargesPerDrink)
	{
		return ECataclysmPotionRefusal::TooFewCharges;
	}

	// ONE HEAL AT A TIME. Four drinks overlapping would restore 140% of maximum
	// health in three seconds; Diablo IV carries one potion for the same reason.
	if (Holder->GetPotionHeal().IsRunning())
	{
		return ECataclysmPotionRefusal::AHealIsRunning;
	}

	// THE AMOUNT IS FIXED WHEN THE POTION IS DRUNK, from the maximum at that
	// moment, as a leech payment's is fixed when the hit lands.
	//
	// AND SMALLER FOR EACH DRINK ALREADY TAKEN IN THIS DUNGEON, on a floor carrying
	// Diminishing Returns. The drinks are counted from entering the dungeon,
	// whether or not every floor carried the row, because the row speaks of the
	// potions losing effectiveness and not of the floor.
	const float LessPerDrink = FMath::Max(
		0.0f,
		Holder->StatForSkill(FName(HealLessPerDrinkStat), FGameplayTagContainer(), 0.0f));
	FCataclysmPotionHeal Heal;
	Heal.Remaining = Vitals->GetMaxHealth() * HealShareOfMaximum
		* HealShareAfter(Holder->GetPotionsDrunk(), LessPerDrink);
	Heal.SecondsLeft = HealSeconds;

	Holder->SetPotionCharges(Slot, Holder->GetPotionCharges(Slot) - ChargesPerDrink);
	Holder->SetPotionHeal(Heal);
	Holder->SetPotionsDrunk(Holder->GetPotionsDrunk() + 1);

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s drank the potion in slot %d: %.1f health over %.1f seconds, %.1f "
				"charges left in it."),
		   *GetNameSafe(Character), Slot + 1, Heal.Remaining, Heal.SecondsLeft,
		   Holder->GetPotionCharges(Slot));
	return ECataclysmPotionRefusal::None;
}

float UCataclysmPotions::HealStep(AActor* Character, float StepSeconds)
{
	UCataclysmAbilitySystemComponent* Holder = PotionHolderOf(Character);
	if (!Holder || StepSeconds <= 0.0f)
	{
		return 0.0f;
	}

	FCataclysmPotionHeal Heal = Holder->GetPotionHeal();
	if (!Heal.IsRunning())
	{
		return 0.0f;
	}

	// A CORPSE IS SKIPPED, as leech skips one. The heal is cleared outright by
	// `ClearWhatDeathEnds` on the respawn.
	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return 0.0f;
	}

	// IN PROPORTION TO THE TIME LEFT, so the last step pays whatever remains
	// however the steps fell, and a step longer than the time left pays it all.
	const float Paid = StepSeconds >= Heal.SecondsLeft
		? Heal.Remaining
		: Heal.Remaining * StepSeconds / Heal.SecondsLeft;

	UCataclysmRegeneration::TopUp(*Holder, UCataclysmVitalAttributeSet::GetHealthAttribute(),
								  UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), Paid);

	Heal.Remaining -= Paid;
	Heal.SecondsLeft = FMath::Max(0.0f, Heal.SecondsLeft - StepSeconds);
	if (Heal.SecondsLeft <= 0.0f)
	{
		Heal = FCataclysmPotionHeal();
	}
	Holder->SetPotionHeal(Heal);
	return Paid;
}

void UCataclysmPotions::RefillAll(AActor* Character)
{
	if (UCataclysmAbilitySystemComponent* Holder = PotionHolderOf(Character))
	{
		for (int32 Slot = 0; Slot < SlotCount; ++Slot)
		{
			Holder->SetPotionCharges(Slot, MaxCharges);
		}
		Holder->SetPotionsDrunk(0);
	}
}

float UCataclysmPotions::HealShareAfter(int32 DrinksTaken, float LessPercentPerDrink)
{
	const float Lost = FMath::Max(0, DrinksTaken) * FMath::Max(0.0f, LessPercentPerDrink) / 100.0f;
	return FMath::Max(LeastHealShare, 1.0f - Lost);
}

float UCataclysmPotions::ChargesIn(const AActor* Character, int32 Slot)
{
	const UCataclysmAbilitySystemComponent* Holder = PotionHolderOf(Character);
	return Holder ? Holder->GetPotionCharges(Slot) : 0.0f;
}

bool UCataclysmPotions::AreForbiddenFor(const AActor* Character)
{
	const UCataclysmAbilitySystemComponent* Holder = PotionHolderOf(Character);
	return Holder
		&& Holder->StatForSkill(FName(ForbiddenStat), FGameplayTagContainer(), 0.0f) > 0.0f;
}

int32 UCataclysmPotions::DrinksIn(float Charges)
{
	return Charges <= 0.0f ? 0 : FMath::FloorToInt(Charges / ChargesPerDrink + KINDA_SMALL_NUMBER);
}

FString UCataclysmPotions::DescribeRefusal(ECataclysmPotionRefusal Refusal)
{
	switch (Refusal)
	{
	case ECataclysmPotionRefusal::None:			  return TEXT("drunk");
	case ECataclysmPotionRefusal::NoCharacter:	  return TEXT("there is no player character to drink it");
	case ECataclysmPotionRefusal::Dead:			  return TEXT("the character is dead");
	case ECataclysmPotionRefusal::NoSuchSlot:	  return TEXT("there is no such potion slot");
	case ECataclysmPotionRefusal::TooFewCharges:  return TEXT("the slot holds too few charges for a drink");
	case ECataclysmPotionRefusal::AHealIsRunning: return TEXT("a potion is still healing");
	case ECataclysmPotionRefusal::Forbidden:	  return TEXT("this floor forbids potions");
	}
	return TEXT("refused");
}

FVector2D UCataclysmPotions::BoxOriginFor(int32 Slot, float ViewportWidth, float ViewportHeight)
{
	const float RowWidth = SlotCount * BoxSizePx + (SlotCount - 1) * BoxGapPx;
	const float Left = ViewportWidth - MarginPx - RowWidth;
	const float Top = ViewportHeight - MarginPx - BoxSizePx;
	return FVector2D(Left + Slot * (BoxSizePx + BoxGapPx), Top);
}

float UCataclysmPotions::FillFractionFor(float Charges)
{
	return FMath::Clamp(Charges / MaxCharges, 0.0f, 1.0f);
}

bool UCataclysmPotions::Enabled()
{
	return CVarCataclysmShowPotions.GetValueOnAnyThread() != 0;
}

// ---------------------------------------------------------------------------
// A way to drink before the input assets carry the keys
// ---------------------------------------------------------------------------

/**
 * `Cataclysm.DrinkPotion <1-4>`. THE KEYS 2 TO 5 NEED THE INPUT ASSETS
 * REGENERATED IN THE EDITOR, as `Cataclysm.CharacterCreation` explains for the C
 * key, so a checkout without them can still drink and be told why a drink was
 * refused.
 */
static FAutoConsoleCommandWithWorldArgsAndOutputDevice GCataclysmDrinkPotion(
	TEXT("Cataclysm.DrinkPotion"),
	TEXT("Drink the potion in slot 1, 2, 3 or 4. The keys 2 to 5 do the same, "
		 "once the input assets have been generated."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			const APlayerController* Controller =
				World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			const int32 Slot = Args.Num() > 0 ? FCString::Atoi(*Args[0]) - 1 : 0;

			const ECataclysmPotionRefusal Refusal = UCataclysmPotions::Drink(Pawn, Slot);
			Ar.Logf(TEXT("Potion slot %d: %s. Charges now %.1f."), Slot + 1,
					*UCataclysmPotions::DescribeRefusal(Refusal),
					UCataclysmPotions::ChargesIn(Pawn, Slot));
		}));
