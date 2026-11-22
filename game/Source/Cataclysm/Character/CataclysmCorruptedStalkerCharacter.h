// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Empire/CataclysmEmpireMap.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "CataclysmCorruptedStalkerCharacter.generated.h"

class UCataclysmPrimaryAttributeSet;
class UCataclysmResistanceAttributeSet;
class UCataclysmClassResourceAttributeSet;
class UStaticMeshComponent;

/** One worn piece of a Corrupted Stalker entry: where it is worn, its base, and its affixes, with no values. */
USTRUCT()
struct CATACLYSM_API FCataclysmStalkerPiece
{
	GENERATED_BODY()

	UPROPERTY()
	ECataclysmGearSlot Slot = ECataclysmGearSlot::Count;

	/** An `ItemBases` row. */
	UPROPERTY()
	FName Base;

	/** `Affixes` rows. Their tiers and rolls are drawn when the Stalker is raised, at the tier being played. */
	UPROPERTY()
	TArray<FName> Affixes;
};

/**
 * One authored Corrupted Stalker: a corrupted former player character.
 *
 * "THE SHAPE OF THE EQUIPMENT AND NONE OF ITS NUMBERS", the design document's rule for the snapshot: an entry names
 * its class, its passives, its weapon and its eighteen pieces' bases and affixes, and every value is drawn at the tier
 * being played. ONE ENTRY PER DAMAGE TYPE, the owner's answer of 2026-10-25 ("Maybe one per damage type. So 8").
 */
USTRUCT()
struct CATACLYSM_API FCataclysmStalkerEntry
{
	GENERATED_BODY()

	/** The damage type the entry is for, as `UCataclysmRoster` names a Cataclysm. */
	UPROPERTY()
	FName DamageType;

	/** What the player is told it is called. Empty for a slot no entry fills yet. */
	UPROPERTY()
	FString Name;

	/** A `ClassStats` class line. */
	UPROPERTY()
	FString ClassName;

	/** As `ItemBases` spells a weapon type: what its basic attack is. */
	UPROPERTY()
	FString WeaponType;

	UPROPERTY()
	FCataclysmPassiveAllocation Allocation;

	UPROPERTY()
	TArray<FCataclysmStalkerPiece> Pieces;

	bool IsAuthored() const { return !Name.IsEmpty(); }
};

/**
 * The Corrupted Stalker: the creature `Generic_Corrupted_Stalker` raises, carrying a player's build. Issue #1308,
 * part B, layer 1.
 *
 * A CREATURE OF THE MONSTERS' SIDE WITH THE PLAYER'S NUMBERS. It holds the five attribute sets a player holds, not the
 * three every other creature holds, and the entry's eighteen pieces, and its numbers come from
 * `UCataclysmPlayerClassStats::ApplyTo`, as a player's do.
 *
 * NO EQUIPMENT COMPONENT IS LEFT ON IT. Its pieces' modifiers are gathered through one that belongs to no actor and is
 * then dropped, because `UCataclysmAbilitySystemComponent::RefreshWornStatLine` refreshes any equipment component it
 * finds on an avatar with the console's class and no passives, which would replace the build. The diagnostic window w54d (2026-10-25) measured that
 * this gives a creature exactly what it gives a player.
 *
 * WHAT LAYER 1 DOES NOT DO: it swings its weapon's basic attack and nothing else (layer 2 is its skills and potions),
 * and its death drops what any creature of its rung drops (layer 3 is its drops and experience).
 */
UCLASS()
class CATACLYSM_API ACataclysmCorruptedStalkerCharacter : public ACataclysmEnemyCharacter
{
	GENERATED_BODY()

public:
	ACataclysmCorruptedStalkerCharacter();

	/**
	 * Every entry, one per damage type in the order War, Demonic, Death, Pestilence, Famine, Celestial, Chaos, Void.
	 *
	 * EVERY TYPE BUT WAR IS AUTHORED. War waits under the standing order of no War work in the game, a named slot.
	 * Demonic was the only entry from 2026-10-25, ruled by the coordinating session, because the other six types' weapon
	 * skills were undesigned placeholders; issue #2580 authored one weapon kit for each of the six on the owner's rulings
	 * of 2026-11-17, and its entry for each wears that weapon on the shared `Default` class line with no passives.
	 */
	static TArray<FCataclysmStalkerEntry> Entries();

	/** The entries a floor may raise: those authored. */
	static TArray<FCataclysmStalkerEntry> AuthoredEntries();

	/**
	 * The entry's pieces as items at a tier: each affix at the highest tier a drop can carry there
	 * (`UCataclysmDropRoll::MaxAffixTierOnADrop`), "rebuilt to the peak of player power", and its roll drawn on the
	 * stream. The weapon carries the entry's damage type.
	 */
	static TArray<FCataclysmItem> PiecesAt(const FCataclysmStalkerEntry& Entry, int32 DifficultyTier,
										   FRandomStream& Stream);

	/**
	 * Wears the entry at a tier: gathers its pieces' modifiers, applies its class line at the tier's reference level, its passives
	 * and its gear through `UCataclysmPlayerClassStats::ApplyTo`, multiplies its maximum health and its attack damage by
	 * the tier's multipliers (`UCataclysmDungeonModifierEffects::CorruptedStalkerHealthMultiplier` and
	 * `CorruptedStalkerDamageMultiplier`, the latter on the floor's ring since issue #2749's ring slice), fills its
	 * pools, and sets `bHoldsAPlayerBuild`, so no archetype setter or floor damage rule writes over it afterwards.
	 *
	 * @param Ring  the ring the dungeon was rolled on; an Outpost, the figure every floor had before the ring, unless
	 *              told otherwise
	 * @return false when a table could not be read or the creature has no ability system; nothing is set then
	 */
	bool WearTheBuild(const FCataclysmStalkerEntry& Entry, int32 DifficultyTier, FRandomStream& Stream,
					  ECataclysmCityTier Ring = ECataclysmCityTier::Outpost);

	/** The name of the entry it wears, or empty before `WearTheBuild`. */
	const FString& EntryName() const { return WornEntryName; }

	/**
	 * What every damage over time it applies is multiplied by: the damage multiplier its build applied to its hits,
	 * `UCataclysmDungeonModifierEffects::CorruptedStalkerDamageMultiplier`, stored by `WearTheBuild`. One until then,
	 * and one for the corrupted double, which carries the player's own numbers and no multiplier (R4, ruled
	 * 2026-11-02). Ruled by the coordinating session on 2026-11-19, issue #2749's enemy burn slice: "The Corrupted
	 * Stalker's and Corrupted Double's kit DoTs are multiplied by the same multiplier their hits carry." Until then its
	 * kit's burns and bleeds were the player's table figures, 25 and 20 a tick, beside hits cut to 0.06 at tier 1.
	 * `UCataclysmSkillEffects::ApplyDamageOverTime` reads it.
	 */
	float DamageOverTimeMultiplier() const { return WornDamageMultiplier; }

	/** Its weapon's basic attack reach once it wears a build, and a creature's melee reach before. */
	virtual float AttackReachCm() const override;

	/** A range once it wears a build, its weapon's basic attack's, and a contact reach before. Issue #2706. */
	virtual bool AttackReachIsAContactReach() const override;

	/** Its weapon's basic attack once it wears a build, cast as a player's is; a creature's blow before. */
	virtual void AttackTarget(AActor* Target) override;

	// ----------------------------------------------------------------------
	// Its brain and its potions -- layer 2, 2026-10-25
	// ----------------------------------------------------------------------

	/**
	 * The slots its choice pass asks for, in order: aura, support, movement, ultimate, heavy, special. Its weapon's
	 * basic attack is the swing the controller makes when none of them is chosen.
	 */
	static TArray<ECataclysmAbilitySlot> ChoiceOrder();

	/**
	 * Its skills, one entry a slot in `ChoiceOrder`, ALWAYS ALL SIX AND IN THAT ORDER, because the controller keys each
	 * skill's last use by its index. One that may not be used now is given a range nothing satisfies (a minimum of 1 cm
	 * and a maximum of 0) instead of being left out: the aura when one runs or mana is below
	 * `CorruptedStalkerAuraOffBelowManaShare`; the support skill at `CorruptedStalkerSupportBelowHealthShare` health or
	 * above, or while its buff runs; the ultimate while it channels or without its Fervour; any without its mana.
	 * The movement skill reaches only beyond `CorruptedStalkerMovementBeyondCm`; the others reach as far as their
	 * shapes. Each cooldown is its slot's, after the Stalker's cooldown reduction, and the support skill's is at least
	 * its buff's duration, so two of its buffs never stack.
	 */
	virtual TArray<FCataclysmEnemyAbility> EnemyAbilities() const override;

	/** Casts the chosen skill as a player's is, paying its mana and Fervour, aimed at the target. */
	virtual void UseEnemyAbility(int32 Index, AActor* Target, const FVector& AimedAt) override;

	/** `CorruptedStalkerSecondsBetweenSkillUses` once it wears a build: the gap between two skill uses or swings. */
	virtual float DesignedSecondsBetweenAttacks() const override;

	/**
	 * Every quarter second: turns its aura off below `CorruptedStalkerAuraOffBelowManaShare` mana, drinks a health
	 * potion below `CorruptedStalkerPotionBelowHealthShare` health under the player's rules, and runs a drink's heal.
	 */
	virtual void AfterRegenerationStep() override;

	/** Its skill in a slot, or null. */
	const FCataclysmWeaponSkill* SkillIn(ECataclysmAbilitySlot Slot) const;

	/** Whether a copy of its skill in this slot is running now: an aura on, a buff held, a channel under way. */
	bool IsRunningASkillIn(ECataclysmAbilitySlot Slot) const;

	// ----------------------------------------------------------------------
	// Its death -- layer 3, 2026-10-25
	// ----------------------------------------------------------------------

	/** A Boss's rung once it wears a build: its experience and its materials and magic find are a Boss's. */
	virtual int32 RarityStepItPaysAs() const override;

	/** None of the drop row's random gear once it wears a build: what it drops is what it wore. */
	virtual int32 RandomGearDropsGiven() const override;

	/** Each piece it wore, with `CorruptedStalkerPieceDropChance`, marked as dropped by a creature a rule raised. */
	virtual void DropWhatItWore(UWorld* World, const FVector& At, FRandomStream& Stream) override;

	/** Which of `PieceCount` pieces drop, by index: each with `CorruptedStalkerPieceDropChance`, one draw a piece. */
	static TArray<int32> WornPiecesThatDrop(int32 PieceCount, FRandomStream& Stream);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	TObjectPtr<UCataclysmPrimaryAttributeSet> PrimaryAttributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	TObjectPtr<UCataclysmResistanceAttributeSet> TypedResistanceAttributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	TObjectPtr<UCataclysmClassResourceAttributeSet> ClassResourceAttributes;

	/** The pieces it wears, as items. What layer 3 drops from. */
	const TArray<FCataclysmItem>& WornPieces() const { return Pieces; }

	// ----------------------------------------------------------------------
	// Its look: the player's own -- issue #2806, 2026-11-21
	// ----------------------------------------------------------------------
	//
	// THE OWNER'S REPORT OF 2026-11-21: "The corrupted dopples and nemesis should be using the character model we use,
	// it's kinda hard to see a grey cylinder coming at you before it murders you". Until this it set no mesh, so it kept
	// `ACataclysmEnemyCharacter`'s `PlaceholderBody`, the engine's basic cylinder. It now wears what the player wears,
	// through the player's own steps (`ACataclysmPlayerCharacter::WearTheMannequinsBody` and its four siblings), so the
	// two cannot drift: the Mannequin body, `ABP_Unarmed`, the six death clips, and the weapons it wears in its hands.
	// `ACataclysmConsumptionDoubleCharacter` inherits all of it.
	//
	// ITS OWN CAPSULE, KEPT: 48 by 80, the creature's, not the player's 42 by 96. The coordinating session's ruling J1 of
	// 2026-11-21: the player's capsule would change its collision, where a hunter fits when it arrives and the reach
	// measured from capsule edges, for a visual change. The Mannequin's feet are at the capsule's bottom, so the body
	// stands above the capsule's top by up to 32 cm, the two capsules' difference. A DARK RED TINT, the owner's ruling
	// of 2026-11-21 (J2): see `CopyTint`. NO SWING ON ITS BLOWS (ruling J3): issue #2811.

	/**
	 * Puts the player's body, animation Blueprint and death clips on, and hides the placeholder cylinder.
	 *
	 * PUBLIC SO A TEST CAN CALL IT, as every dressed creature's is. `BeginPlay` calls it.
	 *
	 * @return whether the body was found, which it is in every checkout: the Mannequin is in git
	 */
	bool ResolveBody();

	/**
	 * The copies' tint: the owner's ruling of 2026-11-21, verbatim, "give them a dark red tint", for the Corrupted
	 * Stalker and the Worn Residue double, so they are told apart from the player at a glance.
	 *
	 * THE COLOUR IS A JUDGEMENT AND IT IS LABELLED ONE: linear (0.30, 0.02, 0.02), which displays as about sRGB
	 * (149, 39, 39), a deep red. Dark enough to read as dark red, light enough to stay visible on a dark floor, and far
	 * from the player's own `Paint Tint`, a light grey of 0.9219 in each channel in `MI_Manny_01_New`.
	 */
	static const FLinearColor CopyTint;

	/**
	 * The Mannequin's material parameter the tint is written to. `M_Mannequin` declares it, and `MI_Manny_01_New`, the
	 * body's instance, sets it; read out of the assets' name tables, since the editor is not needed to name it.
	 */
	static const FName CopyTintParameter;

	/**
	 * Gives every material slot of its body a dynamic instance of its own and writes `CopyTint` to it. Called by
	 * `ResolveBody` once the body is on. HERE AND NOT IN THE STEPS THE PLAYER SHARES, so the player's body keeps its
	 * colours.
	 */
	void WearTheCopiesTint();

	virtual void PreRegisterAllComponents() override;
	virtual void BeginPlay() override;

	/** The hand its first worn weapon is drawn in, on the body's right-hand socket, as the player's. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	TObjectPtr<UStaticMeshComponent> RightHandWeapon;

	/** The hand its second worn weapon is drawn in, on the left-hand socket. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	TObjectPtr<UStaticMeshComponent> LeftHandWeapon;

	/**
	 * The item base each hand draws, none for an empty hand. WHAT IS IN A HAND WITHOUT THE WEAPONS PACK, which git does
	 * not carry and without which every weapon mesh is null: read by `Cataclysm.PlayerCopies.`.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	FName RightHandBase;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	FName LeftHandBase;

protected:
	/** Draws the weapons `Wardrobe` wears in its two hands. Called once it has put on what it wears. */
	void DrawWornWeapons(const UCataclysmEquipmentComponent* Wardrobe);

	// PROTECTED AND NOT PRIVATE since issue #2688 slice 2a, for `ACataclysmConsumptionDoubleCharacter`, which fills
	// them from a player's build rather than from an entry.
	UPROPERTY()
	TArray<FCataclysmItem> Pieces;

	FString WornEntryName;
	FCataclysmWeaponSkill BasicAttack;

	/** See `DamageOverTimeMultiplier`. One until `WearTheBuild` sets it, and one for the corrupted double. */
	float WornDamageMultiplier = 1.0f;

	/** Its weapon's skills, from `UCataclysmWeaponSkills::SkillsFor` for its weapon type and damage type. */
	TArray<FCataclysmWeaponSkill> Skills;
};
