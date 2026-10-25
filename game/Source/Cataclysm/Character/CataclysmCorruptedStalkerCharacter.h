// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "CataclysmCorruptedStalkerCharacter.generated.h"

class UCataclysmPrimaryAttributeSet;
class UCataclysmResistanceAttributeSet;
class UCataclysmClassResourceAttributeSet;

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
	 * ONLY DEMONIC IS AUTHORED, ruled by the coordinating session on 2026-10-25. War waits under the standing order of
	 * no War work in the game. The other six types' weapon skills are undesigned placeholders in `WeaponSkills.csv`, so
	 * an entry of theirs could not fight with player skills; each is a named slot until its skills are designed.
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
	 * the placeholder multipliers (`UCataclysmDungeonModifierEffects::CorruptedStalkerHealthMultiplier`), fills its
	 * pools, and sets `bHoldsAPlayerBuild`, so no archetype setter or floor damage rule writes over it afterwards.
	 *
	 * @return false when a table could not be read or the creature has no ability system; nothing is set then
	 */
	bool WearTheBuild(const FCataclysmStalkerEntry& Entry, int32 DifficultyTier, FRandomStream& Stream);

	/** The name of the entry it wears, or empty before `WearTheBuild`. */
	const FString& EntryName() const { return WornEntryName; }

	/** Its weapon's basic attack reach once it wears a build, and a creature's melee reach before. */
	virtual float AttackReachCm() const override;

	/** Its weapon's basic attack once it wears a build, cast as a player's is; a creature's blow before. */
	virtual void AttackTarget(AActor* Target) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	TObjectPtr<UCataclysmPrimaryAttributeSet> PrimaryAttributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	TObjectPtr<UCataclysmResistanceAttributeSet> TypedResistanceAttributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Stalker")
	TObjectPtr<UCataclysmClassResourceAttributeSet> ClassResourceAttributes;

	/** The pieces it wears, as items. What layer 3 drops from. */
	const TArray<FCataclysmItem>& WornPieces() const { return Pieces; }

private:
	UPROPERTY()
	TArray<FCataclysmItem> Pieces;

	FString WornEntryName;
	FCataclysmWeaponSkill BasicAttack;
};
