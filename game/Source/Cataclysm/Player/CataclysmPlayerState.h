// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "Character/CataclysmCharacterCreation.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmClassStats.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "CataclysmPlayerState.generated.h"

class UDataTable;
class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;
class UCataclysmPrimaryAttributeSet;
class UCataclysmCombatAttributeSet;
class UCataclysmResistanceAttributeSet;
class UCataclysmClassResourceAttributeSet;

/**
 * Owns the player's ability system component and attribute sets.
 *
 * WHY THE PLAYER STATE AND NOT THE PAWN.
 *
 * The design has the player die and respawn at the capital, at a cost of 5 to 15
 * days depending on difficulty. Death is a routine, repeated event, not the end
 * of a session. A pawn is destroyed on death; the player state is not.
 *
 * Putting the ability system on the pawn would mean every death destroyed the
 * player's attributes, active effects, cooldowns and granted abilities, and all
 * of it would have to be saved and restored by hand. On the player state, it
 * simply survives.
 *
 * Enemies do the opposite and own their component on the pawn, because an enemy
 * that dies is gone.
 */
UCLASS()
class CATACLYSM_API ACataclysmPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACataclysmPlayerState();

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UCataclysmAbilitySystemComponent* GetCataclysmAbilitySystemComponent() const { return AbilitySystemComponent; }

	const UCataclysmVitalAttributeSet* GetVitalAttributes() const { return VitalAttributes; }
	const UCataclysmPrimaryAttributeSet* GetPrimaryAttributes() const { return PrimaryAttributes; }
	const UCataclysmCombatAttributeSet* GetCombatAttributes() const { return CombatAttributes; }
	const UCataclysmResistanceAttributeSet* GetResistanceAttributes() const { return ResistanceAttributes; }
	const UCataclysmClassResourceAttributeSet* GetClassResourceAttributes() const { return ClassResourceAttributes; }

	/**
	 * The attribute points this character has spent.
	 *
	 * HERE FOR THE SAME REASON THE ATTRIBUTE SETS ARE. A pawn is destroyed on
	 * death and the player state is not, and an allocation a player lost every
	 * time they died would be worse than no allocation at all.
	 *
	 * ONE PER LEVEL IS THE WHOLE SUPPLY TODAY. `docs/Cataclysm_GDD_v2.md` says
	 * "Players gain 1 attribute point per level" and names the Maw as a second
	 * source; the Maw does not exist, so AttributePointsAvailable below is the
	 * character's level and nothing else. Issue #50.
	 */
	const FCataclysmAttributePoints& GetSpentAttributePoints() const { return SpentAttributePoints; }

	/** How many a character may spend altogether. Its level, until the Maw exists. */
	int32 AttributePointsAvailable() const;

	/** How many of those are not spent yet. */
	int32 AttributePointsUnspent() const;

	/**
	 * Spend into one attribute, named as `game/Data/Attributes.csv` names it.
	 *
	 * REFUSED RATHER THAN CLAMPED when the character does not have that many,
	 * and `OutReason` says which refusal it was. Clamping would let "spend 40"
	 * quietly become "spend 3" and still read as success.
	 */
	bool SpendAttributePoints(const FString& Attribute, int32 Count, FString& OutReason);

	/** Return every spent point, so they can be spent again. */
	void ResetAttributePoints();

	// ----------------------------------------------------------------------
	// Level and experience
	// ----------------------------------------------------------------------

	/**
	 * The character's level, 1 to 100.
	 *
	 * WHY IT IS HERE AND NOT ON THE PAWN, which is the reason the ability system
	 * and the spent attribute points are here: a pawn is destroyed on death and
	 * the player state is not, and a level lost on every death would be worse
	 * than no levelling at all.
	 *
	 * FALLS BACK TO `Cataclysm.PlayerLevel` UNTIL A LEVEL HAS BEEN DECIDED,
	 * which is what `LevelNotYetDecided` below means. That console variable was
	 * the only level this project had, three call sites read it, and every
	 * automation test wanting a level 40 character sets it. Keeping it as the
	 * STARTING level means levelling arrives without moving any of that, and a
	 * character that has neither gained a level nor loaded a save behaves
	 * exactly as it did before.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Experience")
	int32 GetCharacterLevel() const;

	/**
	 * Experience earned toward the next level.
	 *
	 * PROGRESS INTO THE CURRENT LEVEL, NOT A RUNNING TOTAL, matching
	 * `FCataclysmCharacterRecord::Experience` and for the reason
	 * `UCataclysmExperience::Grant` gives: a running total would make the level
	 * derivable and therefore a second copy of the same fact, and retuning the
	 * curve would then silently move every existing character.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Experience")
	int64 GetExperienceIntoLevel() const { return ExperienceIntoLevel; }

	/**
	 * Add experience, raising the level as far as it pays for.
	 *
	 * @return how many levels were gained, which is what a caller awarding a
	 *         point per level needs and cannot recover afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Experience")
	int32 GrantExperience(int64 Amount);

	/**
	 * The share of a kill's experience this character receives, as a
	 * percentage over a base of 100, asked where the kill grants it. Issue
	 * #1833: "Kills no longer generate any experience" removes it. No
	 * attribute, so it is in `UCataclysmPlayerClassStats::StatsWithNoAttribute()`.
	 */
	static const TCHAR* ExperienceGainStat;

	/**
	 * What a kill worth `KillScore` experience grants this character: the score
	 * times `experience_gain` asked through its stat line over a base of 100,
	 * rounded down, and the whole score unchanged when nothing moves it. Issue
	 * #1833, ruled 2026-09-30: "Kills no longer generate any experience" removes
	 * the stat, which leaves nothing.
	 */
	int64 ExperienceAfterGain(int32 KillScore) const;

	/**
	 * Put a saved level and progress back onto the character.
	 *
	 * CLAMPED RATHER THAN REFUSED, because this is reached from a save record
	 * and a save record holds whatever was last written to it. Refusing would
	 * leave the character at whatever level it happened to have, which is a
	 * worse answer than the nearest legal one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Experience")
	void SetLevelAndExperience(int32 NewLevel, int64 NewExperience);

	// ----------------------------------------------------------------------
	// Kills
	// ----------------------------------------------------------------------

	/**
	 * One kill by this character: this run's count and the character's both
	 * rise by one. Issue #1833, the kill counter.
	 *
	 * CALLED WHERE THE "kill" EVENT FIRES, `ACataclysmPlayerCharacter::
	 * OnSomethingDied` when the death notice names this character as the
	 * killer, so the count agrees with every kill-triggered enchantment: a
	 * minion's kill is the minion's own unless the Conduit keystone is held.
	 * Ruled 2026-09-25 under the owner's delegation.
	 *
	 * HERE FOR THE REASON THE LEVEL IS HERE: a pawn is destroyed on death and
	 * the player state is not. "This run" is since the session began; nothing
	 * resets the run count within one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Kills")
	void NoteKill();

	/** Kills this run. Issue #1833. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Kills")
	int32 GetRunKills() const { return RunKills; }

	/**
	 * The world time the floor this character stands on began, recorded by the
	 * dungeon game mode each time it places one. Issue #1833: "Enemies deal
	 * 5%-8% increased damage for every 15 seconds spent on the same dungeon
	 * floor" counts from here, and a new floor starts it again.
	 */
	void NoteFloorBegan(float WorldSeconds);

	/** Seconds since `NoteFloorBegan` at this world time, or -1 if no floor began. */
	float SecondsOnFloor(float WorldSeconds) const;

	/**
	 * One more dungeon floor cleared this run, recorded by the dungeon game
	 * mode the first time it sees a floor's enemies all dead. Issue #1833:
	 * "Your armor is increased by 1%-2% for every dungeon floor cleared this
	 * run". "This run" is since the session began, as for the kills.
	 *
	 * NOTHING IS REFRESHED HERE. A blow asks for armour through the stat
	 * pipeline each time, so a row scaled by this count reaches play at once.
	 * A refresh cannot put a state-scaled row on the Armor attribute:
	 * `UCataclysmPlayerClassStats::ApplyTo` resolves each stat with no state. The
	 * character sheet adds what the refresh could not fold since issue #2183, so
	 * it shows the scaled armour too.
	 */
	void NoteFloorCleared();

	/** Floors cleared this run. Issue #1833. */
	int32 GetFloorsClearedThisRun() const { return FloorsClearedThisRun; }

	/** Kills across every run this character has played. Issue #1833. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Kills")
	int32 GetLifetimeKills() const { return LifetimeKills; }

	/**
	 * Put a saved lifetime count back, a negative one as nought. Issue #1833.
	 * Nothing loads a save yet (#753); this is here for when something does,
	 * as `SetLevelAndExperience` is.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Kills")
	void SetLifetimeKills(int32 Kills);

	// ----------------------------------------------------------------------
	// What was chosen when the character was created
	// ----------------------------------------------------------------------

	/**
	 * The starting weapon type and damage type the player chose.
	 *
	 * HERE FOR THE REASON THE LEVEL IS HERE. A pawn is destroyed on death and
	 * the player state is not, and a character that came back from the capital
	 * as a different damage type would be a different character.
	 *
	 * BOTH EMPTY UNTIL SOMEBODY CHOOSES, and the two stand-ins that used to be
	 * the whole answer are what a character has until then:
	 * `UCataclysmCharacterCreation::DefaultWeaponType` and `DefaultDamageType`.
	 * That is the same arrangement `Cataclysm.PlayerLevel` got when levelling
	 * arrived -- see `docs/DECISIONS.md`, 2026-08-24 -- and it is why every
	 * automation test that stands a character up without touching the creator
	 * gets exactly the character it got before.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	FCataclysmCreationChoice GetCreationChoice() const;

	/** The weapon type the character starts holding. The default until chosen. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	FName GetChosenWeaponType() const;

	/**
	 * The damage type chosen at creation: whose skills the character has, and the type the starting weapon carries.
	 * The class trees follow `CarriedDamageTypes` since issue #857 layer 1, which is this alone with nothing worn.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	FName GetChosenDamageType() const;

	/** Whether anybody has chosen yet, as opposed to sitting on the defaults. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Creation")
	bool HasChosenAtCreation() const { return CreationChoice.IsComplete(); }

	/**
	 * Record what the player chose in the character creator.
	 *
	 * REFUSED RATHER THAN CLAMPED when the pair is not one the design allows,
	 * and `OutReason` says which refusal it was, for the same reason
	 * `SpendAttributePoints` refuses: quietly turning a Staff and War into
	 * something else would still read as success.
	 *
	 * THE TABLES ARE PASSED IN rather than found, so a test can hand it a
	 * matrix it built and set up a pairing the real one does not contain.
	 * `ACataclysmPlayerCharacter` is what loads the real ones.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Creation")
	bool ChooseAtCreation(const UDataTable* WeaponSkillTable,
						  const UDataTable* BaseTable,
						  FName WeaponType, FName DamageType,
						  FString& OutReason);

	/**
	 * Put a saved choice back onto the character, without checking it.
	 *
	 * NOT VALIDATED, WHERE `ChooseAtCreation` IS, and the difference is where
	 * the value came from. A save record holds whatever was last written to it,
	 * and refusing it would leave a loaded character on the defaults -- which is
	 * a different character from the one the player saved. This is the same
	 * trust `SetLevelAndExperience` gives a saved level.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Creation")
	void SetCreationChoice(FName WeaponType, FName DamageType);

	// ----------------------------------------------------------------------
	// Passive points and where they went
	// ----------------------------------------------------------------------

	/**
	 * Where this character's passive points are spent.
	 *
	 * HERE FOR THE REASON THE ATTRIBUTE POINTS ARE HERE. A pawn is destroyed on
	 * death and the player state is not, and a tree a player lost every time
	 * they died would be worse than no tree at all.
	 */
	const FCataclysmPassiveAllocation& GetPassiveAllocation() const { return PassiveAllocation; }

	/**
	 * How many passive points this character has earned altogether.
	 *
	 * ONE PER LEVEL, FIVE MORE EVERY TEN, AND TEN PER FIRST BOSS KILL, which is
	 * `docs/Cataclysm_GDD_v2.md` section XII exactly.
	 * `UCataclysmPassivePoints` does the arithmetic.
	 *
	 * AND WHAT THE WORN GEAR GRANTS, since 2026-10-09: `PassivePointsFromGear`
	 * below. The term is added here so that every reader of the points earned
	 * sees it. With gear the figure can be above the budget of 230.
	 *
	 * IT CAN FALL, WHICH IT COULD NOT BEFORE. Taking the gear off lowers it, and
	 * the points already spent stay spent, so the allocation's total can be
	 * above this figure. `PassivePointsAddingNothing` says by how much.
	 *
	 * AND THE POINTS AN ALLOCATION WAS HANDED WITH, since 2026-10-10:
	 * `PassivePointsAnAllocationWasHandedWith` below. Nought on every character
	 * `SetPassiveAllocation` was never called on, and nothing in the game calls
	 * it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	int32 PassivePointsAvailable() const;

	/**
	 * The points earned by level and by boss kills, plus the points an
	 * allocation was handed with, plus this many from gear.
	 *
	 * THE ONE PLACE THE FOUR ARE ADDED. `PassivePointsAvailable` calls it with
	 * what the pawn's equipment component kept, and
	 * `UCataclysmEquipmentComponent::RefreshAttributes` calls it with the
	 * figure it has just read, before anything else can ask for it.
	 *
	 * A FIGURE BELOW NOUGHT IS READ AS NOUGHT. Gear grants points or grants none.
	 */
	int32 PassivePointsEarnedWith(int32 PointsFromGear) const;

	/**
	 * How many points of the allocation last handed to `SetPassiveAllocation`
	 * the character's level and boss kills did not pay for. Ruled 2026-10-10.
	 *
	 * WHAT IT IS. `SetPassiveAllocation` puts an allocation on a character
	 * without checking it, so the allocation can hold more points than the level
	 * and the bosses handed in the same call have earned. This is that
	 * difference, worked out once in that call and WITHOUT COUNTING GEAR:
	 * the points in the handed allocation, less
	 * `UCataclysmPassivePoints::Available` of the level and those bosses,
	 * and never below nought. `PassivePointsEarnedWith` adds it, so a handed
	 * allocation counts as paid for and every point of it is on the stat line.
	 *
	 * IT DOES NOT TURN THE RULE OFF. It is a fixed number, set when the
	 * allocation is handed over. Points spent afterwards with points that worn
	 * gear grants are above the points earned once that gear comes off, and they
	 * add nothing, exactly as on a character this is nought for.
	 *
	 * NOUGHT UNTIL `SetPassiveAllocation` IS CALLED. A later call replaces it,
	 * and `ResetPassivePoints` sets it back to nought.
	 *
	 * IT IS NOT IN THE SAVE RECORD. Nothing restores a save today (issue #753).
	 * A restore must not hand a saved allocation to `SetPassiveAllocation` as
	 * that function stands: points bought with points from gear would come back
	 * counted as paid for. It has to restore the level, the bosses and this
	 * figure as it was recorded.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	int32 PassivePointsAnAllocationWasHandedWith() const
	{
		return PointsAnAllocationWasHandedWith;
	}

	/**
	 * How many class points the gear this character is wearing grants: what the
	 * pawn's equipment component read at its last refresh
	 * (`UCataclysmEquipmentComponent::ClassPointsGranted`). Ruled 2026-10-09.
	 *
	 * NOUGHT FOR A PLAYER STATE WITH NO PAWN, and nought before the pawn's first
	 * refresh.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	int32 PassivePointsFromGear() const;

	/**
	 * How many of those are not spent yet. Never below nought: a character with
	 * more spent than earned has none to spend.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	int32 PassivePointsUnspent() const;

	/**
	 * How many spent points add nothing because more are spent than earned.
	 * Nought unless more are spent than earned, which happens when gear that
	 * granted points comes off after they were spent. Ruled 2026-10-09.
	 *
	 * NOUGHT STRAIGHT AFTER `SetPassiveAllocation`, whatever it was handed: the
	 * points an allocation was handed with are in the points earned. Ruled
	 * 2026-10-10.
	 *
	 * THE POINTS ARE STILL SPENT. `GetPassiveAllocation().Total()` counts them,
	 * and so does every row scaled by class points spent.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	int32 PassivePointsAddingNothing() const;

	/** The unique Cataclysm bosses this character has defeated at least once. */
	const TArray<FName>& GetDefeatedCataclysmBosses() const { return DefeatedCataclysmBosses; }

	/**
	 * Record that a unique Cataclysm boss has been defeated.
	 *
	 * THE FIRST TIME IS THE ONLY TIME THAT PAYS. The design says "Defeating a
	 * unique Cataclysm boss for the FIRST time: 10 bonus passive points", so a
	 * boss killed again grants nothing and this answers false.
	 *
	 * NOTHING CALLS IT YET. There is no unique Cataclysm boss in the game: the
	 * rarity exists in `game/Data/EnemyRarities.csv` with a spawn weight of zero
	 * and no named boss entity exists at all. `Cataclysm.DefeatCataclysmBoss` is
	 * how the award is exercised until one does, which is the same arrangement
	 * `Cataclysm.GrantExperience` had before a kill granted any.
	 *
	 * @return whether this was the first defeat, and so whether ten points were
	 *         earned by it
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	bool RecordCataclysmBossDefeat(FName Boss);

	/**
	 * Put one passive point into a node.
	 *
	 * REFUSED RATHER THAN CLAMPED, with `OutReason` saying which refusal it was,
	 * for the reason `SpendAttributePoints` refuses.
	 *
	 * A TREE THE CHARACTER CANNOT REACH IS REFUSED HERE rather than inside
	 * `UCataclysmPassiveTree`, and the split is deliberate: that class is about
	 * a tree's own rules and this one is about a particular character. Which
	 * trees are reachable follows from the damage type the character carries.
	 *
	 * @param Node  a row name in `game/Data/PassiveNodes.csv`, which is the tree
	 *              and the node together, such as `Masochist_basic_spine_005`
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	bool SpendPassivePoint(FName Node, FString& OutReason);

	/**
	 * Take one of a capstone's three options, and the capstone with it.
	 * Permanent until a respec.
	 *
	 * IT SPENDS THE POINT AS WELL AS RECORDING THE CHOICE. Issue #1075. Until
	 * 2026-08-28 it only recorded the choice, and a capstone holding a choice
	 * but no point grants nothing: `UCataclysmPassiveTree::AccumulateInto` skips
	 * a node with no points before it ever reads the chosen option. The project
	 * owner took Water to Blood in play, was told the option was picked, and
	 * still had a mana pool, because the second act was never performed and
	 * nothing on the screen said one was left to perform.
	 *
	 * NOBODY BENEFITED FROM THE STATE IN BETWEEN, which is what decided this
	 * rather than a message on the screen. The choice is already permanent, a
	 * capstone holds exactly one point, and a capstone that is chosen and
	 * unspent grants nothing and cannot be undone.
	 *
	 * BOTH ACTS OR NEITHER. They are applied to a copy of the allocation and the
	 * copy is kept only if both succeed, so a character with no points left is
	 * refused outright rather than left permanently committed to an option it
	 * cannot turn on.
	 *
	 * AND A CAPSTONE THAT ALREADY HOLDS ITS POINT IS NOT CHARGED A SECOND ONE.
	 * Taking the option that is already taken is allowed and changes nothing,
	 * which is what the screen does when a player clicks the marked option
	 * again.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	bool ChoosePassiveOption(FName Node, int32 Option, FString& OutReason);

	/**
	 * Return every passive point, so they can be spent again.
	 *
	 * WHAT THE TRAINER SELLS, at a cost in days that nothing charges yet. The
	 * design gives the whole tree back rather than one node, which is why this
	 * takes no argument.
	 *
	 * THE POINTS AN ALLOCATION WAS HANDED WITH GO TOO. That figure pays for
	 * points of one handed allocation, and this empties it. Afterwards the
	 * character can spend what its level, its bosses and its worn gear give.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	void ResetPassivePoints();

	/**
	 * Put an allocation and a list of defeated bosses on the character, without
	 * checking either.
	 *
	 * NOT VALIDATED, for the reason `SetCreationChoice` is not: refusing a
	 * recorded allocation would leave a character with an empty tree, which is a
	 * different character.
	 *
	 * THE WHOLE HANDED ALLOCATION COUNTS AS PAID FOR. Ruled 2026-10-10. Since
	 * 2026-10-09 points spent above the points earned add nothing, and this
	 * function can be handed more points than the level and `Bosses` have
	 * earned. So it records by how many the handed allocation exceeds them
	 * (`PassivePointsAnAllocationWasHandedWith`), and that figure is added to
	 * the points earned. Gear is not counted when the figure is worked out. The
	 * level read is the character's level when this is called.
	 *
	 * NOTHING IN THE GAME CALLS IT. Its callers are automation tests. Nothing
	 * restores a save (issue #753), and a restore must not call this with a
	 * saved allocation as it stands: see
	 * `PassivePointsAnAllocationWasHandedWith`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Passives")
	void SetPassiveAllocation(const FCataclysmPassiveAllocation& Allocation,
							  const TArray<FName>& Bosses);

	/** Which trees this character can spend in, from the damage types its worn weapons carry. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	TArray<FString> ReachableTrees() const;

	/**
	 * The damage types this character carries: the union, in order, of its worn weapons' `DamageTypes`, Weapon1's
	 * first, a weapon with an empty list counting as the creation type. Issue #857 layer 1, ruled 2026-10-25.
	 *
	 * THE CREATION TYPE ALONE WITH NO WEAPON WORN OR NO PAWN, "so the trees screen is never empty". Asked of the
	 * pawn's equipment, `UCataclysmEquipmentComponent::CarriedDamageTypesOf` doing the work.
	 *
	 * WHAT READS IT: which trees are reachable, the one-class rule, the refusal sentence, the passive tree screen and
	 * the passive console commands. The skills follow the same list since issue #837 layer 2, asked of the equipment
	 * by the pawn when it fills its weapon slots (`ACataclysmPlayerCharacter::FillAbilitySlotsFromWornWeapon`).
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Passives")
	TArray<FName> CarriedDamageTypes() const;

	// ----------------------------------------------------------------------
	// Which pool skill sits on which key. Issue #837 layer 3.
	// ----------------------------------------------------------------------

	/**
	 * The player's chosen skills, one entry per key a choice was made for. Issue #837 layer 3, ruled 2026-10-25.
	 *
	 * HERE AND SAVED, as the creation choice is: a pawn is destroyed on death and the player state is not, and the
	 * character record writes it (`UCataclysmCharacterSave::SkillAssignment`, one field, no version bump, Q6).
	 *
	 * A CHOICE WHOSE WEAPON CAME OFF IS KEPT (Q5, the owner: "the slot shows its default and the choice is kept for
	 * when the weapon returns"): the fill leaves it out and the key shows its default, and wearing the weapon again
	 * puts it back. Only `ClearSkillChoice` forgets one (O5, the owner: "Forget my choice").
	 */
	const TArray<FCataclysmSkillChoice>& GetSkillAssignment() const { return SkillAssignment; }

	/**
	 * Put the pool skill of row `SkillRow` on the key `Slot`, and fill the keys again. Issue #837 layer 3.
	 *
	 * REFUSED, WITH `OutReason`, FOR WHAT `RefusalForSkillChoice` REFUSES: the basic attack's key (O2), a row not in
	 * the character's pool now, a row already chosen for another key (O4). The skill moved keeps every rule of the slot
	 * it was made for (O1). The key it leaves takes its next default, or stays empty (O4).
	 *
	 * @return whether the choice was recorded
	 */
	bool AssignSkill(ECataclysmAbilitySlot Slot, FName SkillRow, FString& OutReason);

	/**
	 * "Clear the slot" (Q5): forget the choice for `Slot`, so the key returns to its default for good (O5), and fill
	 * the keys again. Issue #837 layer 3.
	 *
	 * @return whether there was a choice to forget
	 */
	bool ClearSkillChoice(ECataclysmAbilitySlot Slot);

	/**
	 * Why `SkillRow` may not be chosen for `Slot`, in one sentence, or empty when it may. Issue #837 layer 3. A
	 * static, so the screen of layer 4 can ask it of any pool. EVERY SKILL IS NAMED BY ITS NAME AND NOT ITS ROW (layer
	 * 4, ruled Q5), except a row the table does not have, which has no name and is named as it was typed.
	 *
	 * @param Table    the weapon skill matrix, for "no such row"; null skips that question
	 * @param Pool     the skills the worn weapons make available now
	 * @param Choices  the choices already made
	 */
	static FString RefusalForSkillChoice(ECataclysmAbilitySlot Slot, FName SkillRow, const UDataTable* Table,
										 const TArray<FCataclysmWeaponSkill>& Pool,
										 const TArray<FCataclysmSkillChoice>& Choices);

	/** A key's name as a player reads it, "Heavy" or "Ultimate", from its `Slot.*` tag. Empty for none. */
	static FString SkillSlotName(ECataclysmAbilitySlot Slot);

	/**
	 * Work this character's whole stat line out again and write it.
	 *
	 * WHY THE PASSIVE FUNCTIONS ABOVE ALL CALL THIS. Issue #1054. Changing the
	 * allocation changes nothing a player can feel by itself: the modifiers a
	 * spent point grants only reach a gameplay attribute when the pipeline is
	 * run, and until this nothing ran it. The project owner put ten points into
	 * nodes reading "+1% increased Maximum Health per point" and watched their
	 * health stay at 510.
	 *
	 * HERE, AND NOT IN THE TWO CALLERS. `Cataclysm.SpendPassivePoint` and
	 * `UCataclysmPassiveTreeWidget::SpendInto` both spend through this class, so
	 * one call in each of them would work today and leave the next caller
	 * written to fail silently. That is exactly how the defect arose.
	 *
	 * THE POOLS ARE LEFT WHERE THEY ARE, which matches what spending an
	 * attribute point already does: a point into Vitality raises maximum health
	 * without healing anybody. A character that spends into maximum health goes
	 * from 510 out of 510 to 510 out of 561 and heals the rest back normally.
	 *
	 * NOTHING HAPPENS WITHOUT A POSSESSED CHARACTER, and that is an ordinary
	 * state rather than a fault: a player state exists before it is possessed
	 * and between possessions. Possession writes the whole stat line itself, so
	 * a point spent while unpossessed is picked up then.
	 */
	void RefreshCharacterStats() const;

	/**
	 * What a change to `Cataclysm.PlayerLevel` does to every character in play.
	 * Issue #1540, ruled 2026-10-29, A JUDGEMENT.
	 *
	 * A CHARACTER WHOSE LEVEL IS NOT SETTLED TAKES THE CHANGE AT ONCE. Its
	 * character sheet, its passive points, a minion it summons and a save all
	 * read the variable through `GetCharacterLevel` the moment it changes; only
	 * the stat line waited for the next refresh, so a character showed one level
	 * and had the health of another. It is refreshed here, and one line in the
	 * log says at which level.
	 *
	 * A CHARACTER WHOSE LEVEL IS SETTLED, by a level gained or a save loaded, is
	 * not changed: a level gained overrides the variable. One warning names the
	 * level it is settled at, so typing the variable never does nothing silently.
	 *
	 * Called by the variable's change callback in `CataclysmPlayerClassStats.cpp`.
	 * Only a player state with a possessed character is answered.
	 */
	static void StartingLevelChanged();

	/** Whether a level has been gained or loaded, so `Cataclysm.PlayerLevel` no longer decides it. Issue #1540. */
	bool HasSettledLevel() const { return CharacterLevel > LevelNotYetDecided; }

	/**
	 * Whether an equipped weapon carries a damage type that reaches the tree
	 * this node is in. Issue #1075.
	 *
	 * SHARED BY THE TWO ACTS THAT PUT A POINT IN, which is why it exists at all:
	 * `SpendPassivePoint` and `ChoosePassiveOption` both spend, so both have to
	 * refuse an unreachable tree, and the sentence a player reads has to be the
	 * same one either way.
	 *
	 * A NODE IN NO TREE IS REACHABLE. `TreeOf` answers an empty string for a
	 * node the table does not have, and refusing here would hide the plainer
	 * refusal the tree's own rules give a moment later.
	 *
	 * @param OutReason left untouched when the answer is yes
	 */
	bool ReachesTreeOf(const UDataTable* NodeTable, FName Node,
					   FString& OutReason) const;

protected:
	/**
	 * What `CharacterLevel` holds before anything has decided one.
	 *
	 * ZERO IS NOT A LEVEL, so it cannot be mistaken for one. A character that
	 * has neither gained a level nor loaded a save reads its level from
	 * `Cataclysm.PlayerLevel` instead, which is what every existing automation
	 * test and every existing call site expects.
	 */
	static constexpr int32 LevelNotYetDecided = 0;

	/**
	 * The two writers of the creation choice make the weapon creation gave carry the chosen damage type, through
	 * `UCataclysmEquipmentComponent::CarryCreationDamageType`. Issue #857 layer 1. Nothing without a possessed
	 * character, and nothing is refreshed: both callers refresh the stat line next.
	 */
	void CarryTheChosenTypeOnTheCreationWeapon();

	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Experience")
	int32 CharacterLevel = LevelNotYetDecided;

	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Experience")
	int64 ExperienceIntoLevel = 0;

	/** See `NoteKill`. Replicated so a client's character sheet can show them. */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Kills")
	int32 RunKills = 0;

	/** See `NoteFloorCleared`. Replicated so a client's sheet can show it. */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Dungeon")
	int32 FloorsClearedThisRun = 0;

	/** See `NoteFloorBegan`. -1 until a floor begins. */
	float FloorBeganAtSeconds = -1.0f;

	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Kills")
	int32 LifetimeKills = 0;

	/**
	 * REPLICATED, because a client draws its own character sheet from this and
	 * the server is what decides whether a spend was legal.
	 */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Attributes")
	FCataclysmAttributePoints SpentAttributePoints;

	/**
	 * REPLICATED, because the client's own screens read it: which six skills
	 * exist, which class trees can be opened, and what the gear panel says is
	 * in hand all follow from these two names.
	 */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Creation")
	FCataclysmCreationChoice CreationChoice;

	/**
	 * REPLICATED, because the client draws the tree screen from it and the
	 * server is what decides whether a spend was legal.
	 */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Passives")
	FCataclysmPassiveAllocation PassiveAllocation;

	/**
	 * Which unique Cataclysm bosses this character has already beaten.
	 *
	 * NAMES RATHER THAN A COUNT, and the difference is the whole rule. Ten
	 * points are granted for the FIRST defeat of EACH boss, so a count could be
	 * raised eight times by killing one boss eight times.
	 */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Passives")
	TArray<FName> DefeatedCataclysmBosses;

	/**
	 * See `PassivePointsAnAllocationWasHandedWith`. Written by
	 * `SetPassiveAllocation` and `ResetPassivePoints` and by nothing else.
	 *
	 * REPLICATED, because it is a term of the points earned, and the client's
	 * tree screen prints the points earned beside the allocation above.
	 */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Passives")
	int32 PointsAnAllocationWasHandedWith = 0;

	/**
	 * See `GetSkillAssignment`. Issue #837 layer 3. Written by `AssignSkill` and `ClearSkillChoice` and by nothing
	 * else; nothing loads a save into it yet (#753).
	 *
	 * REPLICATED, because the client's screen of layer 4 draws the keys from it.
	 */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Skills")
	TArray<FCataclysmSkillChoice> SkillAssignment;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	/**
	 * A player carries all five sets. Enemies carry only the three that describe
	 * a combatant: they have no attribute points to spend and no class tree, so
	 * giving them the primary or class resource sets would be dead weight on
	 * every spawn.
	 */
	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmPrimaryAttributeSet> PrimaryAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmCombatAttributeSet> CombatAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmResistanceAttributeSet> ResistanceAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmClassResourceAttributeSet> ClassResourceAttributes;
};
