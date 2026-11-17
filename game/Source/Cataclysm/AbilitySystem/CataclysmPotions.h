// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmPotions.generated.h"

class AActor;
class UCataclysmAbilitySystemComponent;

/**
 * Why a drink was refused, or `None` when it was taken.
 *
 * SEVERAL ANSWERS RATHER THAN A YES OR NO, because the player is told which one
 * applies and the tests assert which one fired: "too few charges" and "a heal is
 * already running" are different things to wait for.
 */
UENUM(BlueprintType)
enum class ECataclysmPotionRefusal : uint8
{
	/** The drink was taken. */
	None,

	/** No character, or one with no ability system or vitals. */
	NoCharacter,

	/** The character is dead. */
	Dead,

	/** The slot is not one of the four. */
	NoSuchSlot,

	/** The slot holds fewer charges than a drink costs. */
	TooFewCharges,

	/** A potion heal is still being paid out. One runs at a time. */
	AHealIsRunning,

	/** The floor forbids potions: the Famine modifier Hard Mode. */
	Forbidden
};

/**
 * The potion heal a character is being paid, a quarter-second at a time.
 *
 * THE SAME SHAPE AS A LEECH PAYMENT, and for the same reason: an amount owed
 * and the seconds it is spread over, paid in proportion each step.
 */
USTRUCT(BlueprintType)
struct FCataclysmPotionHeal
{
	GENERATED_BODY()

	/** Health still to be paid. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Potions")
	float Remaining = 0.0f;

	/** Seconds the rest is spread over. Zero when no heal is running. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Potions")
	float SecondsLeft = 0.0f;

	bool IsRunning() const { return SecondsLeft > 0.0f; }
};

/**
 * The four potion slots: what a drink does, what fills a slot, and how the slots
 * are laid out on screen. Issue #806.
 *
 * WHAT THE DESIGN DECIDES, AND WHAT IT LEAVES. Section VI of
 * `docs/Cataclysm_GDD_v2.md` gives "Consumables: 4 Potion slots" and says they
 * "are consumables rather than gear and contribute through their sockets only".
 * Everything else here -- the heal, the charges, the refill, the keys -- was
 * proposed on 2026-09-25 and accepted under the project owner's delegation of
 * unstated numbers. `docs/DECISIONS.md` has the entry and the sources.
 *
 * THE SHAPE IS PATH OF EXILE'S FLASK. A slot holds charges, a drink spends some,
 * and every kill adds charges to every slot by how strong the creature was. The
 * Famine modifier Recession, "Potions take 4x as many kills to fill", is written
 * in exactly those terms.
 *
 * FOUR IDENTICAL HEALTH POTIONS FOR NOW. The design tells potions apart by the gem
 * in each slot's socket, and gems do not exist yet (issue #46).
 *
 * THE CHARGES AND THE RUNNING HEAL LIVE ON THE ABILITY SYSTEM COMPONENT, beside the
 * stacks and the leech payments, so they belong to the character and survive a
 * floor change. This file holds the rules and the numbers.
 */
UCLASS()
class CATACLYSM_API UCataclysmPotions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** "4 Potion slots", section VI of the design document. */
	static constexpr int32 SlotCount = 4;

	/** The most charges a slot holds. A play-test value: three drinks. */
	static constexpr float MaxCharges = 30.0f;

	/** What one drink spends. */
	static constexpr float ChargesPerDrink = 10.0f;

	/**
	 * A drink heals this share of maximum health. Diablo IV's potion heals 35% of
	 * maximum life over 3 seconds. A share rather than Path of Exile's fixed
	 * amounts, because there are no potion tiers here to grow with a character.
	 */
	static constexpr float HealShareOfMaximum = 0.35f;

	/** Over this many seconds. Path of Exile's life flasks take 3 to 4. */
	static constexpr float HealSeconds = 3.0f;

	/**
	 * The charges a kill adds to every slot, by the dead creature's rarity rung.
	 *
	 * PATH OF EXILE'S FOUR FIGURES, MAPPED UPWARD: normal 1, magic 3.5, rare 6,
	 * unique 11 (Maxroll's flask guide). Common, Elite and Legendary take the
	 * first three; Herald, Boss and Cataclysm Boss all take the unique figure.
	 * A rung outside the table gives the Common figure.
	 *
	 * Weighted by the spawn weights in `game/Data/EnemyRarities.csv` a kill is
	 * worth 2.75 charges, so a slot earns a drink back every 3.6 kills. That is
	 * the first number to tune after play.
	 */
	static float ChargesForKillOf(int32 RarityStep);

	/**
	 * A creature this character killed has died. Adds its charges to every slot,
	 * up to the maximum.
	 *
	 * @return the charges added to each slot before the cap, or 0 when the killer
	 *         has no potion slots, which is every creature.
	 */
	static float NoteEnemyKilled(AActor* Killer, int32 RarityStep);

	/**
	 * Drink the potion in a slot, 0 to 3: spend a drink's charges and start
	 * paying out the heal.
	 *
	 * A REFUSAL SPENDS NOTHING. Every check runs before a charge is taken.
	 */
	static ECataclysmPotionRefusal Drink(AActor* Character, int32 Slot);

	/**
	 * Pay one step of a running heal, through `UCataclysmRegeneration::TopUp`.
	 *
	 * THROUGH TopUp SO EVERY RULE ON HEALING APPLIES TO A POTION: a held swing
	 * that forbids healing, healing received reductions, and the healing ceiling
	 * that Point of No Return lowers. Path of Exile's Petrified Blood lets flasks
	 * heal past its cap; Point of No Return's sentence exempts nothing, so a
	 * potion obeys it. Ruled 2026-09-25, and the owner may overturn it.
	 *
	 * A heal paid into a full bar is spent all the same, as leech is.
	 *
	 * @return the health the step asked to restore, before TopUp's limits
	 */
	static float HealStep(AActor* Character, float StepSeconds);

	/**
	 * Fill every slot, and start counting drinks again. Called on entering a
	 * dungeon.
	 */
	static void RefillAll(AActor* Character);

	//~ What the three Famine dungeon modifiers write, as stats with no
	//~ attribute. `UCataclysmDungeonModifierEffects` writes them on a floor
	//~ carrying the row; this file asks for them through `StatForSkill`.

	/**
	 * Above zero, no potion may be drunk. `Famine_Hard_Mode`: "Players cannot
	 * use potions in this dungeon." The slots still fill from kills.
	 */
	static const TCHAR* ForbiddenStat;

	/**
	 * The percentage less a kill adds to each slot. `Famine_Recession`:
	 * "Potions take 4x as many kills to fill", which is 75% less a kill.
	 */
	static const TCHAR* KillChargesLessStat;

	/**
	 * The percentage of a drink's heal lost for each drink already taken in this
	 * dungeon. `Famine_Diminishing_Returns`: "Potions lose effectiveness over
	 * time". Ruled per drink on 2026-09-25, 10 each.
	 */
	static const TCHAR* HealLessPerDrinkStat;

	/**
	 * The smallest share of a full heal a drink falls to, whatever the drinks
	 * already taken. A judgement, ruled on 2026-09-25: 30%.
	 */
	static constexpr float LeastHealShare = 0.3f;

	/**
	 * The share of a full heal the next drink restores, given the drinks already
	 * taken in this dungeon and the percentage lost for each.
	 */
	static float HealShareAfter(int32 DrinksTaken, float LessPercentPerDrink);

	/** The charges in a slot, or 0 for no character or no such slot. */
	static float ChargesIn(const AActor* Character, int32 Slot);

	/** Whether this character's floor forbids potions. The boxes are crossed out. */
	static bool AreForbiddenFor(const AActor* Character);

	/** Whole drinks a number of charges pays for. */
	static int32 DrinksIn(float Charges);

	/** The text a refusal is reported with. */
	static FString DescribeRefusal(ECataclysmPotionRefusal Refusal);

	//~ The on-screen boxes. Kept here, beside the numbers they draw, so a headless
	//~ test can check the layout. `ACataclysmHUD::DrawPotions` draws them.

	/** A potion box's side. Smaller than a skill box's 56, as a secondary row. */
	static constexpr float BoxSizePx = 44.0f;

	/** The gap between two boxes. */
	static constexpr float BoxGapPx = 6.0f;

	/** The distance from the right and bottom edges of the screen. */
	static constexpr float MarginPx = 26.0f;

	/**
	 * The top-left corner of a slot's box: four in a row in the bottom-right
	 * corner, slot 0 on the left, which is the order of the keys 2 to 5.
	 */
	static FVector2D BoxOriginFor(int32 Slot, float ViewportWidth, float ViewportHeight);

	/** How full a slot's bar is drawn, 0 to 1. */
	static float FillFractionFor(float Charges);

	/** Whether the boxes are drawn at all. `Cataclysm.ShowPotions`, 1 by default. */
	static bool Enabled();

private:
	static UCataclysmAbilitySystemComponent* PotionHolderOf(const AActor* Character);
};
