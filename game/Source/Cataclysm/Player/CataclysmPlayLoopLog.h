// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UCataclysmEmpireRun;
class UCataclysmEquipmentComponent;
struct FCataclysmDayReport;
struct FCataclysmItem;
enum class ECataclysmForgeAffixCraft : uint8;
enum class ECataclysmForgeDamageTypeCraft : uint8;

/**
 * THE PLAY LOOP'S LOG: one plain line at each step of the main loop, so that one press of Play leaves the whole loop in
 * the log, in order. Issue #2733 slice 4. The project owner's checklist, `docs/Play_Loop_Checklist.md`, quotes these
 * lines; the Output Log's filter "PLAY LOOP" shows them and nothing else.
 *
 *   PLAY LOOP: the front end is open: 1 saved character to continue, or New character to make one
 *   PLAY LOOP: new character 'Ash' (Greatsword, War) begun: opening the capital
 *   PLAY LOOP: in the capital on day 0, tier 1: 12 dungeons standing, 0 cities fallen; level 1, 0 experience into it
 *   PLAY LOOP: standing: dungeon 3, the Horde dungeon on Outpost (-3,0): 12 floors, 9 days until it bites, ...
 *   PLAY LOOP: Walk pressed on dungeon 3, the Horde dungeon on Outpost (-3,0): travelling to the dungeon level
 *   PLAY LOOP: entered dungeon 3, floor 1 of 12; the walk owes 1 day
 *   PLAY LOOP: dungeon 3: down to floor 2 of 12; the walk owes 2 days
 *   PLAY LOOP: level 2 reached, from level 1: 3120 experience into it, 46280 more to level 3
 *   PLAY LOOP: cleared dungeon 3 on day 12: +1.0 empire points; level 4: back to the capital
 *   PLAY LOOP: in the capital on day 12, ...; while away, days 0 to 12: dungeons resolved: 5, ...
 *
 * EACH LINE IS BUILT HERE FROM WHAT IT IS GIVEN, AND SAID BY `Say`, so a test can read every line's words without a
 * world, and the place that says one passes only figures the game already holds. Nothing here changes any state.
 *
 * DISPLAY, NOT LOG: a Display line reaches the Output Log and `Saved/Logs` at the editor's default verbosity.
 */
struct CATACLYSM_API FCataclysmPlayLoopLog
{
	/** What every line begins with, which the Output Log's filter finds. */
	static const TCHAR* const Prefix;

	/** Logs `Prefix` and `Line` at Display on LogCataclysm. */
	static void Say(const FString& Line);

	/** The front end, opened: how many saved characters it lists. */
	static FString FrontEndLine(int32 SavedCharacters);

	/** Begin pressed on the new character screen: its name, weapon type and damage type. */
	static FString NewCharacterLine(const FString& Name, FName WeaponType, FName DamageType);

	/** A saved character's row pressed: its name, and whether it resumes a walk in the dungeon level. */
	static FString ContinueLine(const FString& Name, bool bResumesAWalk);

	/**
	 * A dungeon standing on the run, as the line names it: "dungeon 3, the Horde dungeon on Outpost (-3,0): 12 floors,
	 * 9 days until it bites, modifiers Brand of the Aggressor, Hard Mode". The floors and the days are the city
	 * screen's own words; the kind and the modifiers are what no screen shows today. Empty for a dungeon not on the run.
	 */
	static FString DungeonLine(const UCataclysmEmpireRun* Run, int32 DungeonId);

	/** One "standing: <DungeonLine>" per dungeon on the run, in identifier order. */
	static TArray<FString> StandingLines(const UCataclysmEmpireRun* Run);

	/**
	 * THE NAMES OF THE DUNGEONS STANDING, by identifier, read before a walk's days run: what `AwayAccount` names a
	 * dungeon those days resolved by, because a resolved dungeon is no longer on the run to be asked.
	 */
	static TMap<int32, FString> NamesStanding(const UCataclysmEmpireRun* Run);

	/**
	 * WHAT THE DAYS AWAY DID, from the days' own reports and not from the summary the map reads
	 * (`UCataclysmEmpireRun::NoteDaysAway`), so the log is a second account to check the map against: "while away,
	 * days 0 to 12: dungeons resolved: 5 (the dungeon on Outpost (-2,-1)); cities hit: Outpost (-2,-1) lost 750 defence
	 * and 1200 people; cities fallen: none; 1 surge brought 8 dungeons". "No day passed" when none did. AND THE CITY
	 * UPGRADES THOSE DAYS FINISHED, since issue #2825, added only when there are any: "; city upgrades finished:
	 * Outpost (-3,0)".
	 *
	 * @param NamesBefore `NamesStanding` read before the days ran; a dungeon the days both brought and resolved is
	 *                    named by its identifier alone.
	 */
	static FString AwayAccount(int32 FromDay, int32 ToDay, const TArray<FCataclysmDayReport>& Days,
							   const TMap<int32, FString>& NamesBefore, const UCataclysmEmpireRun* Run);

	/** The capital, its map opened: the day, the board and the character, and the days away when a walk just ended. */
	static FString CapitalLine(const UCataclysmEmpireRun* Run, int32 Level, int64 ExperienceIntoLevel,
							   const FString& Away);

	/** Walk pressed in the capital: the dungeon, named as `DungeonLine` names it. */
	static FString WalkLine(const UCataclysmEmpireRun* Run, int32 DungeonId);

	/** A dungeon entered: its floor 1 of all of them, and the days the walk owes. */
	static FString EnteredLine(int32 DungeonId, int32 Floors, int32 DaysOwed);

	/** The stairs taken to the next floor. */
	static FString FloorLine(int32 DungeonId, int32 Floor, int32 Floors, int32 DaysOwed);

	/** The last floor's stairs, which clear the dungeon. */
	static FString ClearLine(int32 DungeonId, int32 DayOfTheClear, double PointsAwarded, int32 Level);

	/** A death that ended a walk. */
	static FString DeathLine(int32 DungeonId, int32 Floor, int32 Floors, int32 DaysTheDeathCost);

	/**
	 * The gold a walk picked up and the balance it left, said at the walk's end after the clear's or the death's line:
	 * "dungeon 3's walk picked up 1,234 gold; 5,678 gold held". Issue #2742, slice 1: what a walk earns, measured, so
	 * slice 2's city upgrade price can be checked against it (the coordinating session's G4 ruling).
	 *
	 * @param Held  the balance after the walk, or below zero when there is none to name -- no character playing, or an
	 *              account record that would not read -- and the line says so.
	 */
	static FString GoldLine(int32 DungeonId, int64 PickedUp, int64 Held);

	/**
	 * `GoldLine` for the character playing in a world: the game instance's `GoldPickedUpThisWalk`, and the balance of
	 * its partition's account. A world with no game instance of this project's picked up nothing and holds no balance.
	 */
	static FString GoldLineIn(const UObject* WorldContext, int32 DungeonId);

	/**
	 * A level gained, from a kill or `Cataclysm.GrantExperience`: the level reached, the level it came from, the
	 * experience into the new level and what the next one still costs. At the maximum level, that it is the maximum.
	 * Said by `ACataclysmPlayerState::GrantExperience`. Issue #2744.
	 */
	static FString LevelGainedLine(int32 From, int32 To, int64 ExperienceIntoLevel);

	/**
	 * A city upgrade bought on the city screen, what it cost and the balance it left: "bought 'Increase max defense
	 * by 20%' on Outpost (-3,0) for 10,000 gold; 2,345 gold held". Issue #2742's second slice: the owner's check that
	 * a purchase took the price it showed.
	 *
	 * @param Held      the balance after the purchase, or below zero when there is none to name, and the line says so.
	 * @param ReadyDay  the day the upgrade will be finished, since issue #2825: "; being built, ready on day 57". At
	 *                  INDEX_NONE, an upgrade finished at once, the line says nothing more.
	 * @param bRushed   bought with Rapid Funding's Rush, since issue #2832: "; rushed" before the ready day.
	 */
	/**
	 * Economic Bailout at a city whose defence reached 0 (issue #2741 slice 9, V9). Paid: "Economic Bailout saved Outpost
	 * (-3,0) from falling for 50,000 gold; 12,345 gold held". Not paid: "Economic Bailout did not save Outpost (-3,0):
	 * its fee is 50,000 gold and 40,000 is held", or "and nothing holds a balance to pay it from" when `Held` is below
	 * zero.
	 */
	static FString BailoutLine(const UCataclysmEmpireRun* Run, int32 CityId, int64 Fee, int64 Held, bool bPaid);

	/**
	 * Bribery reset a dungeon's timer (issue #2741 slice 9, V12 to V14): "Bribery reset dungeon 7's timer for 20,000
	 * gold; 342 gold held".
	 */
	static FString BribeLine(int32 DungeonId, int64 Fee, int64 Held);

	/** Bribery refused, with the refusal the player is shown: "Bribery refused for dungeon 7: <why>". Slice 9. */
	static FString BribeRefusedLine(int32 DungeonId, const FString& Why);

	static FString UpgradeLine(const FString& Upgrade, const UCataclysmEmpireRun* Run, int32 CityId, int64 Price,
							   int64 Held, int32 ReadyDay = INDEX_NONE, bool bRushed = false);

	/**
	 * A point bought in the empire upgrade tree: "bought a point in Structural Integrity (Architect): 3 of 10; 9.5
	 * empire points banked". Issue #2741 slice 2; said by `UCataclysmGameInstance::BuyEmpireTreePoint`, after the point
	 * is in the tree and paid for, so the figures are the ones after it. The bank is rounded down to a tenth, as the
	 * empire map shows it (`UCataclysmEmpireMapLayout::EmpirePointsText`).
	 */
	static FString TreePointLine(const FString& Node, const FString& Branch, int32 Points, int32 MaxPoints,
								 double Banked);

	/**
	 * One of a centre capstone's options chosen: "chose The Delver for Foundations of the Empire". Issue #2741 slice 2;
	 * said by `UCataclysmGameInstance::ChooseEmpireTreeOption`, after the choice is made.
	 */
	static FString TreeOptionLine(const FString& Option, const FString& Capstone);

	/**
	 * The empire upgrade tree respecced: "respecced the empire upgrade tree: 26 points back, 26.5 points banked; it cost
	 * 5 days, day 12 to day 17". Issue #2741 slice 2; said by `UCataclysmGameInstance::RespecEmpireTree`, after
	 * the points are back and the days have passed.
	 */
	static FString TreeRespecLine(int32 PointsBack, double Banked, int32 Days, int32 DayFrom, int32 DayTo);

	// THE CHARACTER'S BUILD, issue #2763: one line for each change the player makes to it, each said where the
	// change is made and beside the save it raises (`ECataclysmSaveTrigger::CharacterBuildChanged`). Until these the
	// log said nothing when a point was spent, so the owner's log could not show when their lost points were spent.

	/**
	 * A passive point spent: "spent a passive point in Fervour (Masochist): 1 of 1; 1 of 2 passive points spent".
	 * Said by `ACataclysmPlayerState::SpendPassivePoint`, after the point is in the node.
	 */
	static FString PassivePointLine(const FString& Node, const FString& Tree, int32 Points, int32 MaxPoints,
									int32 Spent, int32 Available);

	/**
	 * A capstone option taken, which spends its point: "took the option Water to Blood at the capstone The First
	 * Vow; 25 of 25 passive points spent". Said by `ACataclysmPlayerState::ChoosePassiveOption`.
	 */
	static FString PassiveOptionLine(const FString& Option, const FString& Capstone, int32 Spent, int32 Available);

	/**
	 * The passive tree reset: "reset the passive tree: 3 points back; 3 to spend". Said by
	 * `ACataclysmPlayerState::ResetPassivePoints`, after the points are back.
	 */
	static FString PassiveResetLine(int32 PointsBack, int32 ToSpend);

	/**
	 * Attribute points spent: "spent 1 attribute point on Ferocity: 2 in it; 0 unspent". Said by
	 * `ACataclysmPlayerState::SpendAttributePoints`.
	 */
	static FString AttributePointLine(int32 Count, const FString& Attribute, int32 InIt, int32 Unspent);

	/**
	 * The attribute points reset: "reset the attribute points: 2 points back; 2 unspent". Said by
	 * `ACataclysmPlayerState::ResetAttributePoints`.
	 */
	static FString AttributeResetLine(int32 PointsBack, int32 Unspent);

	/**
	 * A skill put on a key: "put Molten Cleave in the Heavy slot". Said by `ACataclysmPlayerState::ChooseSkill`, which
	 * the skill screen and `Cataclysm.AssignSkill` call; never by a load.
	 */
	static FString SkillChosenLine(const FString& Skill, const FString& Slot);

	/**
	 * A key's choice forgotten: "forgot the Heavy slot's skill choice: it shows its default again". Said by
	 * `ACataclysmPlayerState::ForgetSkillChoice`.
	 */
	static FString SkillForgottenLine(const FString& Slot);

	/**
	 * An item raised a gear level at the forge: "upgraded Quality Torc of Warding from +2 to +3 with two +3 stones, 4
	 * left, and 2,055 gold; residue 87 to 112; it cost 3 days, day 20 to day 23". The owner's ruling of 2026-11-19,
	 * "build it next"; the gold since #2830. Said by `UCataclysmForgeCraft::Upgrade`, after the stones and the gold are
	 * taken, the item raised and the days passed.
	 */
	static FString ForgeUpgradeLine(const FString& ItemName, int32 FromLevel, int32 ToLevel, int32 StonesLeft, int64 Gold,
									int32 ResidueBefore, int32 ResidueAfter, int32 Days, int32 DayFrom, int32 DayTo);

	/**
	 * Two upgrade stones combined into one of the next level at the forge: "combined two +2 upgrade stones into one +3
	 * for 100 gold: 17 +2 and 6 +3 held". Said by `UCataclysmForgeCraft::Combine`, after the stones have changed.
	 */
	static FString ForgeCombineLine(int32 FromLevel, int32 IntoLevel, int32 FromHeld, int32 IntoHeld, int64 Gold);

	/**
	 * Two gems of a kind and rarity combined into one of the next rarity at the forge: "combined two Of Life (Superb)
	 * into one Of Life (Masterful) for 200 gold: 1 Superb and 1 Masterful held". Issue #46, slice G6, the gold #2830.
	 * Said by `UCataclysmForgeCraft::CombineGems`, after the gems have changed.
	 */
	static FString ForgeGemCombineLine(const FString& GemName, const FString& FromRarity, const FString& IntoRarity,
									   int32 FromHeld, int32 IntoHeld, int64 Gold);

	/**
	 * A gem socketed or unsocketed at the forge: "socketed Of The Viper (Superb) into the Attack socket of Quality Torc
	 * of Warding; residue 87 to 92; it cost 1 day, day 20 to day 21", or "unsocketed Of The Viper (Superb) from Quality
	 * Torc of Warding, back to the bag; residue 87 to 90; it cost 1 day, day 20 to day 21". Issue #46, slice G4. Said by
	 * `UCataclysmForgeCraft::SocketGem` and `UnsocketGem`, after the craft and the days.
	 */
	static FString ForgeSocketLine(bool bUnsocket, const FString& GemName, const FString& Rarity,
								   const FString& SocketType, const FString& ItemName, int64 Gold, int32 ResidueBefore,
								   int32 ResidueAfter, int32 Days, int32 DayFrom, int32 DayTo);

	/**
	 * A socket added or a socket's type changed at the forge: "added socket 3 to Quality Torc of Warding for 750 gold,
	 * rolled Defense; residue 40 to 55; it cost 3 days, day 20 to day 23", or "made socket 2 of Quality Torc of Warding
	 * Utility, from Attack, for 500 gold; residue 40 to 50; it cost 2 days, day 20 to day 22". Socket numbers count from
	 * 1, as the screen's. Issue #46, slice G9. Said by `UCataclysmForgeCraft::AddSocket` and `ChangeSocketType`, after
	 * the craft and the days.
	 */
	static FString ForgeSocketWorkLine(bool bAdd, int32 SocketNumber, const FString& FromType, const FString& ToType,
									   const FString& ItemName, int64 Gold, int32 ResidueBefore, int32 ResidueAfter,
									   int32 Days, int32 DayFrom, int32 DayTo);

	/**
	 * An affix rerolled, perfected or raised a tier at the forge: "rerolled affix 1 of Quality Torc of Warding for 250
	 * gold, its roll 50 to 73 percent of tier 2; residue 40 to 42; it cost 0 days, day 20 to day 20", "perfected affix 1
	 * of ... for 1,250 gold, its roll 50 to 100 percent of tier 2; ...", or "raised affix 1 of ... for 250 gold from tier
	 * 2 to tier 3, its roll kept at 50 percent; ...". Affix numbers count from 1, as the screen's. Issue #47, slice F1.
	 * Said by `UCataclysmForgeCraft::CraftAffix`, after the craft and the days.
	 */
	static FString ForgeAffixLine(ECataclysmForgeAffixCraft Craft, int32 AffixNumber, const FString& ItemName, int64 Gold,
								  int32 FromTier, int32 ToTier, int32 FromPercent, int32 ToPercent, int32 ResidueBefore,
								  int32 ResidueAfter, int32 Days, int32 DayFrom, int32 DayTo);

	/**
	 * An affix added or shifted at the forge: "added Flat armor at tier 3 to Everyday Helm for 450 gold as affix 2,
	 * making it Quality; residue 40 to 55; it cost 3 days, day 20 to day 23", or "shifted affix 1 of Quality Helm for 450
	 * gold from Flat evasion to Flat armor, tier 2 kept; residue 40 to 55; it cost 1 day, day 20 to day 21". Affix numbers
	 * count from 1, as the screen's. Issue #47, slice F2. Said by `UCataclysmForgeCraft::AddAffix` and `ShiftAffix`,
	 * after the craft and the days.
	 */
	static FString ForgeAffixChangeLine(bool bShift, int32 AffixNumber, const FString& FromName, const FString& ToName,
										const FString& ItemName, int64 Gold, int32 Tier, const FString& Rarity,
										int32 ResidueBefore, int32 ResidueAfter, int32 Days, int32 DayFrom, int32 DayTo);

	/**
	 * The residue tools used with a craft, said after the craft's own line. At 250 residue, tier 1: an added socket with
	 * both, "used a Chaos Stabilizer and 1 Tainted Shard on Quality Helm for 3,000 gold: 7.5 residue and 1 day saved"; one
	 * with the Stabilizer only, "used a Chaos Stabilizer on Quality Helm for 1,500 gold: 7.5 residue saved"; one with two
	 * Shards, "used 2 Tainted Shards on Quality Helm for 3,000 gold: 2 days saved". Issue #47, slice F5. Said by
	 * `UCataclysmForgeCraft::DoItemCraft`, only when an option was used.
	 */
	static FString ForgeOptionsLine(const FString& ItemName, bool bStabilizer, int32 Shards, float ResidueSaved,
									int32 DaysSaved, int64 Gold);

	/**
	 * A cleanse at the forge, at tier 1: "cleansed Quality Helm for 6,250 gold: residue 1200 to 600; it cost 3 days, day
	 * 20 to day 23". Issue #47, slice F5. Said by `UCataclysmForgeCraft::Cleanse`, after the craft and the days.
	 */
	static FString ForgeCleanseLine(const FString& ItemName, int64 Gold, int32 ResidueBefore, int32 ResidueAfter,
									int32 Days, int32 DayFrom, int32 DayTo);

	/**
	 * The Soul Forge's second level, said after the upgrade's own line: "The Soul Forge raised Quality Helm two levels,
	 * +3 to +5". Issue #47, slice F6. Said by `UCataclysmForgeCraft::Upgrade`, only when it rose two.
	 */
	static FString ForgeSoulForgeLine(const FString& ItemName, int32 FromLevel, int32 ToLevel);

	/**
	 * The Master Tinkerer's critical success, said after the enchantment craft's own line: "a critical success on
	 * Quality Helm: the rolls were drawn twice with the better kept, and the craft took no days". Issue #47, slice F6.
	 * Said by `UCataclysmForgeCraft::DoEnchantmentWork`, only on a critical success.
	 */
	static FString ForgeCriticalLine(const FString& ItemName);

	/**
	 * A weapon's damage type infused or changed at the forge: "infused Quality Sword with Death, drawn, for 450 gold;
	 * residue 40 to 50; it cost 1 day, day 20 to day 21", "infused Quality Sword with Death, chosen, for 2,250 gold;
	 * ...", or "changed War on Quality Sword to Death for 450 gold; ...". Issue #47, slice F4. Said by
	 * `UCataclysmForgeCraft::CraftDamageType`, after the craft and the days.
	 */
	static FString ForgeDamageTypeLine(ECataclysmForgeDamageTypeCraft Craft, const FString& ItemName,
									   const FString& FromType, const FString& ToType, int64 Gold, int32 ResidueBefore,
									   int32 ResidueAfter, int32 Days, int32 DayFrom, int32 DayTo);

	/**
	 * An enchantment imprinted or an enchantment's drawback changed at the forge, the item named as it was before and
	 * each half as the tool tip reads it, the drawback without its "Drawback: " word: "imprinted Gain 20% increased
	 * health, drawback You have no armor, on Masterful Helm of Warding, giving up +42 to maximum health, for 2,300 gold;
	 * residue 180 to 185; it cost 3 days, day 20 to day 23", or "changed the drawback of Gain 20% increased health on
	 * Legendary Helm of Warding from You have no armor to 80% of your health is reserved for 8,225 gold; residue 185 to
	 * 285; it cost 8 days, day 23 to day 31". Issue #47, slice F3. Said by `UCataclysmForgeCraft::DoEnchantmentWork`
	 * for `Imprint` and `ChangeNegative`, after the craft and the days.
	 */
	static FString ForgeEnchantmentWorkLine(bool bImprint, const FString& ItemName, const FString& Positive,
											const FString& Drawback, const FString& GivenUpOrReplaced, int64 Gold,
											int32 ResidueBefore, int32 ResidueAfter, int32 Days, int32 DayFrom,
											int32 DayTo);

	/**
	 * A piece of gear picked up off a floor: "picked up Quality Torc of Warding, +0". The coordinating session's ruling
	 * of 2026-11-19: the owner's play log said nothing about gear, so a walk could not be read for what was taken. Said
	 * by `UCataclysmDropPickup::TakeInto`, for a piece of gear only, once it is in the bag. A crafting material is not
	 * announced: it is collected without a click, several to a kill, and is not a choice. Gold has `GoldLine`.
	 *
	 * NO HIT IN IT, because picking a piece up changes nothing worn; `WoreLine` says what wearing one did.
	 *
	 * @param ItemName  the name the floor showed for it, `ACataclysmDroppedItem::DisplayName`
	 */
	static FString PickedUpLine(const FString& ItemName, int32 GearLevel);

	/**
	 * The basic attack's hits for the gear a character wears: the plain one and one on each of the run's families,
	 * as the item comparison prices them (`UCataclysmItemComparison::BasicHitOf` and `BasicHitOnAFamilyOf`).
	 */
	struct FHits
	{
		float Plain = 0.0f;
		TArray<FName> Families;
		TArray<float> OnFamilies;
	};

	/**
	 * `FHits` for what a character wears now, read off the ability system of the actor that owns the equipment, which
	 * `ACataclysmPlayerCharacter::OnEquipmentChanged` refreshes as the gear changes. Nought, and no family, for
	 * equipment no actor with an ability system owns; the families are the run's
	 * (`UCataclysmItemComparison::RunFamiliesIn`).
	 */
	static FHits HitsOf(const UCataclysmEquipmentComponent& Worn);

	/**
	 * A piece of gear put on, and what it did to the basic attack: "wore Quality Torc of Warding in Necklace: basic
	 * attack hit 57.6 to 57.6; on a Demonic creature's health 63.4 to 97.8". The coordinating session's ruling of
	 * 2026-11-19, with `PickedUpLine`. Said by `UCataclysmWearing::WearFromCarried`, which every wear the player
	 * chooses comes through, after the equip. The figures are written as the item comparison writes them.
	 */
	static FString WoreLine(const FString& ItemName, const FString& SlotName, const FHits& Before, const FHits& After);

	/**
	 * A skill level gained, from a kill's share or from a character level that let a skill's kept experience through:
	 * "skill Molten Cleave reached level 3, from level 2". Issue #2764. Said by `ACataclysmPlayerState::
	 * SaySkillLevelsGained`, after the experience is held, so the level is the new one.
	 */
	static FString SkillLevelLine(const FString& Skill, int32 From, int32 To);
};
