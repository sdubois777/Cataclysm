// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCommand.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmRegeneration.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
// For the modifier a test hands to ApplyTo, and the units of its value. #963.
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * The class stat line reaching the player, which nothing did before issue #806.
 *
 * WHAT THESE GUARD, AND IT IS A FAILURE THAT SHOWED UP AS A PLAY REPORT RATHER
 * THAN AS AN ERROR. `game/Data/ClassStats.csv` held every class's health,
 * regeneration and armour, `UCataclysmClassStats::BaseFor` could read them at a
 * level, and no code outside the test suite ever called it. Nothing failed,
 * nothing logged, and the character simply had the attribute set's placeholder
 * 100 health for the whole life of the project. A floor of creatures killed them
 * in about a second.
 *
 * THE MOST IMPORTANT TEST HERE IS THE FIRST ONE, and it is the only one that
 * compares the code against something the code does not own: it reads every
 * stat name out of the data table and insists each has an attribute. A stat the
 * design adds and the map does not know about is dropped in silence, which is
 * exactly the shape of the original defect.
 */

namespace CataclysmPlayerClassStatsTest
{
	/** An actor carrying the three attribute sets the class table writes into. */
	struct FScopedCharacter
	{
		explicit FScopedCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject is a
			// template and deduces T from the argument, so a TObjectPtr would
			// deduce the wrapper rather than the attribute set.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);

			// THE RESISTANCE SET JOINED THE OTHER THREE IN ISSUE #894, when the
			// eight per-type resistances gained a StatToAttribute entry. ApplyTo
			// skips any attribute whose set the component does not hold, so
			// without this the "every mapped stat was written" check below would
			// be eight short and would fail.
			UCataclysmResistanceAttributeSet* NewResistance =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);

			// AND THE PRIMARY SET JOINED THEM IN ISSUES #50 AND #897, for
			// exactly the reason above: the eight attributes gained a
			// StatToAttribute entry when a character could first spend a point
			// on one, so without this the same check would be eight short.
			UCataclysmPrimaryAttributeSet* NewPrimary =
				NewObject<UCataclysmPrimaryAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResource);
			AbilitySystem->AddAttributeSetSubobject(NewResistance);
			AbilitySystem->AddAttributeSetSubobject(NewPrimary);

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		float Read(const FGameplayAttribute& Attribute) const
		{
			return AbilitySystem->GetNumericAttribute(Attribute);
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
	};

	static UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}
}

#define CATACLYSM_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// --------------------------------------------------------------------------
// The map against the data, which is the check the original defect needed
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmEveryClassStatDrivesAnAttribute,
	"Cataclysm.PlayerStats.EveryClassStatDrivesAnAttribute")
{
	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(FString::Printf(TEXT("DT_ClassStats does not exist at %s."),
			UCataclysmPlayerClassStats::ClassStatsAssetPath));
		return false;
	}

	const TMap<FString, FGameplayAttribute>& Map =
		UCataclysmPlayerClassStats::StatToAttribute();

	// READ OUT OF THE TABLE RATHER THAN LISTED HERE. A second copy of the stat
	// names in this file would agree with the map by construction and would
	// notice nothing.
	TSet<FString> NamedByTheDesign;
	for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
	{
		const auto* Line = reinterpret_cast<const FCataclysmClassStatRow*>(Row.Value);
		if (Line && !Line->Stat.IsEmpty())
		{
			NamedByTheDesign.Add(Line->Stat);
		}
	}

	// Without this the loop below passes on an empty table, which is what a
	// stale or unbuilt asset actually looks like.
	TestTrue(TEXT("the class table names some stats at all"),
		NamedByTheDesign.Num() >= 20);

	for (const FString& Stat : NamedByTheDesign)
	{
		TestTrue(FString::Printf(
			TEXT("the class table's stat '%s' drives a gameplay attribute. "
				 "Without an entry in StatToAttribute it is dropped in silence, "
				 "which is how the player kept 100 health for the whole life of "
				 "the project."), *Stat),
			Map.Contains(Stat));
	}

	// AND EVERY MAPPED STAT HAS A SOURCE, which for most of them is a class
	// line. A stat written from a line that does not exist, with nothing else
	// supplying it either, is a stat whose base is always zero.
	//
	// EACH EXEMPTION STATES WHERE ITS VALUE COMES FROM INSTEAD, and that is the
	// point of holding them in a map rather than a set. A stat added to
	// StatToAttribute with no class line and no entry here still fails, which is
	// the case this guard exists for.
	//
	// A CLASS WITH NO BASE FOR A STAT IS THE DESIGN AND NOT A GAP.
	// docs/Cataclysm_GDD_v2.md: "A class does not need a base above zero for
	// every stat. It needs one for every stat it wants its attributes to
	// scale... that is the system working rather than failing -- it is how a
	// class declines to care about a stat." So the honest question is not
	// whether a class line exists, it is whether SOMETHING supplies the stat.
	const TMap<FString, FString> SuppliedFromElsewhere = {
		// Issue #845. A weapon's damage is an implicit on its base, so it
		// arrives as an ordinary flat modifier from GatherModifiers.
		{TEXT("attack_damage"), TEXT("the worn weapons, as a flat modifier")},

		// Issue #845. A swing rate is a column rather than an implicit, and two
		// weapons average theirs, so a base has to be supplied.
		{TEXT("attack_speed"),
		 TEXT("the worn weapons, as a base override from StatBasesFromWeapons")},

		// Issue #894. The design gives critical strike chance to the skill being
		// used rather than to the character, so like a swing rate it is a base
		// no class line can state.
		{TEXT("crit_chance"),
		 TEXT("the skill in hand, as a base override from StatBasesFromWeapons")},

		// Issue #896. Magic find's baseline is zero because the design makes it
		// "an added percentage rather than a percentage of something", so gear
		// is its only source. Loot quantity is the opposite and IS named by a
		// class line, because its baseline is 100.
		{TEXT("magic_find"), TEXT("gear alone")},

		// Issue #895. Only the Ravager states a life leech, and no class states
		// a mana or energy shield leech at all, so gear is the only source of
		// those two. A character with none of it leeches nothing, which is the
		// ordinary case rather than a gap.
		{TEXT("mana_leech"), TEXT("gear alone")},
		{TEXT("energy_shield_leech"), TEXT("gear alone")},

		// Issue #895. No class line states a cooldown reduction, so gear and
		// the Efficacy attribute are its only sources. A base of zero is right:
		// the design's formula divides by one plus the increases, so no
		// increases leaves every cooldown at its stated length.
		{TEXT("cooldown_reduction"), TEXT("gear and the Efficacy attribute")},

		// Issue #895. No class line names any of the eight, so gear is their
		// only source. Each applies only when the target is that damage type,
		// so a base of zero is right: a character with none of it deals its
		// ordinary damage to everything.
		{TEXT("damage_vs_war"), TEXT("gear alone")},
		{TEXT("damage_vs_demonic"), TEXT("gear alone")},
		{TEXT("damage_vs_death"), TEXT("gear alone")},
		{TEXT("damage_vs_pestilence"), TEXT("gear alone")},
		{TEXT("damage_vs_famine"), TEXT("gear alone")},
		{TEXT("damage_vs_celestial"), TEXT("gear alone")},
		{TEXT("damage_vs_chaos"), TEXT("gear alone")},
		{TEXT("damage_vs_void"), TEXT("gear alone")},

		// Issue #894. Gear is the only source of these twelve. Their base is
		// zero on every class, which is a class declining to care about them,
		// and an increased affix on one therefore grants nothing until a flat
		// one is also worn.
		{TEXT("evasion"), TEXT("gear alone")},
		{TEXT("block_chance"), TEXT("gear alone")},
		{TEXT("penetration"), TEXT("gear alone")},
		{TEXT("resistance_war"), TEXT("gear alone")},
		{TEXT("resistance_demonic"), TEXT("gear alone")},
		{TEXT("resistance_death"), TEXT("gear alone")},
		{TEXT("resistance_pestilence"), TEXT("gear alone")},
		{TEXT("resistance_famine"), TEXT("gear alone")},
		{TEXT("resistance_celestial"), TEXT("gear alone")},
		{TEXT("resistance_chaos"), TEXT("gear alone")},
		{TEXT("resistance_void"), TEXT("gear alone")},

		// Issue #1252. Armour penetration, added to `StatToAttribute` on
		// 2026-09-05 while building the character sheet, which is what noticed
		// it was the only one of the sheet's 46 stats with no entry there. No
		// class line names it and none should: `docs/Cataclysm_GDD_v2.md` puts
		// its sources on gear.
		//
		// A SEPARATE ENTRY RATHER THAN ONE MORE "gear alone", because that is
		// not yet true and saying so would be a claim this project has been
		// bitten by before. Nothing in `game/Data/` grants the stat at all: no
		// affix in `Affixes.csv`, no implicit, no passive effect. The three
		// sources the design intends are enchantments in
		// `EnchantmentsPositive.csv` -- "Your skills ignore 10%-25% of enemy
		// armor" and two others -- and no enchantment is modelled, which is
		// issue #45. So every character sits at zero and the sheet's row says
		// so.
		{TEXT("armor_penetration"),
		 TEXT("gear, once enchantments exist. Nothing grants it today")},

		// Issues #50 and #897. The eight primary attributes are the points a
		// particular character spent, which no class line can state: every
		// class starts every attribute at nothing and the character decides.
		// docs/Cataclysm_GDD_v2.md: "Players gain 1 attribute point per level."
		{TEXT("agility"), TEXT("the points the character has spent")},
		{TEXT("ferocity"), TEXT("the points the character has spent")},
		{TEXT("constitution"), TEXT("the points the character has spent")},
		{TEXT("vitality"), TEXT("the points the character has spent")},
		{TEXT("mind"), TEXT("the points the character has spent")},
		{TEXT("spirit"), TEXT("the points the character has spent")},
		{TEXT("efficacy"), TEXT("the points the character has spent")},
		{TEXT("luck"), TEXT("the points the character has spent")},

		// Issue #954. The three rates that move Fervour, and a passive tree's
		// generator node is their only source. They are zero for every class on
		// purpose: a character that has spent no point on a generator gains no
		// Fervour and loses none, which is what makes that node worth a point.
		//
		// A CLASS STAT ROW WOULD BE WRONG AND WAS TRIED. Three rows of zeroes
		// were added to the Class Stats sheet first;
		// tools/tests/test_class_sheets_match_the_model.py refused them, because
		// a class stat row that is zero in both columns says nothing and every
		// stat a line does not name already resolves to zero. That rule is
		// right. What supplies these is a `flat` row in
		// game/Data/PassiveEffects.csv on the Masochist's starting node.
		{TEXT("fervour_from_damage"),
		 TEXT("a passive tree's generator node, as a flat modifier")},
		{TEXT("fervour_from_cost"),
		 TEXT("a passive tree's generator node, as a flat modifier")},
		{TEXT("fervour_lost_to_healing"),
		 TEXT("a passive tree's generator node, as a flat modifier")},

		// Issue #970. What a character adds to every skill's health cost. Zero
		// for every class on purpose, for the same reason the three rates above
		// are: the Masochist's Deeper Cuts node is its only source, and a
		// character with no point in it pays only whatever a skill states for
		// itself. A class stat row of zeroes would be refused by
		// tools/tests/test_class_sheets_match_the_model.py, exactly as it was
		// for those three.
		{TEXT("added_health_cost"),
		 TEXT("the Masochist's Deeper Cuts node, as a flat modifier")},

		// Issue #986. The same thing measured against CURRENT health, which
		// the design keeps as a separate stat because a share of current
		// health cannot kill and a share of maximum health can. Zero for
		// every class, and the Masochist's Exsanguinate keystone is its only
		// source.
		{TEXT("added_health_cost_of_current"),
		 TEXT("the Masochist's Exsanguinate node, as a flat modifier")},

		// Issue #988. How far healing may take the character, written as a
		// reduction of the ceiling so that zero means no cap. Zero for every
		// class, and the Masochist's Point of No Return keystone is its only
		// source.
		{TEXT("healing_ceiling_reduction"),
		 TEXT("the Masochist's Point of No Return node, as a flat modifier")},

		// Issue #41, slice 5. How much of each amount of healing arrives, as a
		// reduction so that zero means no change. NOT THE CEILING ABOVE: that
		// caps how HIGH healing may take a character, this cuts how much of
		// each amount ARRIVES. Zero for every class, and no class line may name
		// it -- its sources are the dungeon modifier Death's Embrace and the
		// enchantment rows that reduce healing received, none of which is a
		// class trait.
		{TEXT("healing_received_reduction"),
		 TEXT("the dungeon modifier Death's Embrace and the healing "
			  "enchantment rows, as a flat modifier")},

		// Issue #991. What share of a health cost is taken later rather than
		// now. Zero for every class, and the Masochist's Deferred Payment node
		// is its only source.
		{TEXT("deferred_health_cost_share"),
		 TEXT("the Masochist's Deferred Payment node, as a flat modifier")},

		// Issue #995. How many seconds a health cost paid while something is
		// already owed pushes that debt further out. Zero for every class, and
		// the Masochist's Rolling Debt node is its only source. It is the one
		// stat here measured in seconds rather than as a share of something.
		{TEXT("health_debt_delay_extension"),
		 TEXT("the Masochist's Rolling Debt node, as a flat modifier")},

		// Issue #997. Whether a debt is never taken on a timer, is cleared by
		// killing an enemy, and kills the character if it passes their current
		// health. Zero for every class, and the Masochist's The Reckoning
		// keystone is its only source. Read as a yes or no rather than as a
		// quantity, so any value above zero turns it on.
		{TEXT("health_debt_cleared_only_by_a_kill"),
		 TEXT("the Masochist's The Reckoning node, as a flat modifier")},

		// Issue #1006. Whether healing stops removing Fervour. Zero for every
		// class, and two Masochist keystones set it: Sanguine Ledger for
		// regeneration healing and Wounds That Feed for leech. The row's
		// required tags are what tell the two apart, so the attribute itself
		// stays at zero even for a character holding one of them --
		// `UCataclysmFervour::LossIsSuppressed` asks for the stat with the
		// healing's tags rather than reading it.
		{TEXT("fervour_loss_suppressed"),
		 TEXT("the Masochist's Sanguine Ledger and Wounds That Feed nodes, as "
			  "flat modifiers")},

		// Issue #1008. How much Fervour arrives every second from nothing
		// having happened. Zero for every class, and the Masochist's Low Life
		// keystone is its only source. The attribute stays at zero even for a
		// character holding it, because its bonus carries a health condition;
		// `GainPerSecondStep` asks for the stat rather than reading it.
		// Issue #1515. The Ravager's own generator and its decay. Zero for
		// every class, and the Ravager tree's starting node is the only source
		// of any of the three: "Enemies in reach generate Fervour ... Fervour
		// decays at 5 per second after 3 seconds with no enemy within 4
		// metres." One keystone adds to the radius; nothing adds to the delay,
		// which is a constant rather than a stat for that reason.
		//
		// NO CLASS LINE MAY NAME ANY OF THEM. A class whose Fervour filled
		// itself from standing near anything would hand out the Ravager tree's
		// defining rule for free, and one whose pool drained out of contact
		// would take a cost with no node paying for it.
		{TEXT("fervour_per_enemy_in_reach"),
		 TEXT("the Ravager's starting node, as a flat rate scaled by how many "
			  "enemies stand within its four metres")},
		{TEXT("fervour_decay_per_second"),
		 TEXT("the Ravager's starting node, as a flat rate read once the grace "
			  "has lapsed")},
		// Issue #1515. How much of maximum health a kill restores, paid for in
		// Fervour. Zero for every class; the Ravager's Wrung Out is its only
		// source. NO CLASS LINE MAY NAME IT: a class whose every kill restored
		// health would hand out the node's whole effect without its cost.
		{TEXT("health_restored_on_kill"),
		 TEXT("the Ravager's Wrung Out node, as a flat percentage of maximum "
			  "health per point, bought with Fervour on a kill")},
		// Issue #1515. The increased damage a melee attack buys with Fervour for
		// each enemy it strikes beyond the first. Zero for every class; the
		// Ravager's Bought With Ruin is its only source. NO CLASS LINE MAY NAME
		// IT, for the reason the row above gives: it would hand out the node's
		// effect without its cost.
		{TEXT("increased_damage_bought_per_extra_enemy_hit"),
		 TEXT("the Ravager's Bought With Ruin node, as a flat percentage of "
			  "increased damage per point for each enemy a melee attack strikes "
			  "beyond the first, bought with Fervour")},
		// Issue #1515. Fervour for each enemy an attack lands on. Zero for every
		// class; the Ravager's starting node is its only source.
		{TEXT("fervour_per_enemy_hit"),
		 TEXT("the Ravager's starting node, as a flat count of Fervour for each "
			  "enemy an attack lands on")},
		// Issue #1515. The two death rules that cost nothing. Zero for every
		// class; one capstone option supplies each.
		{TEXT("health_restored_on_kill_at_no_cost"),
		 TEXT("the Ravager's Long Hold capstone option, as a flat percentage of "
			  "maximum health a kill restores without spending Fervour")},
		{TEXT("fervour_on_enemy_death_nearby"),
		 TEXT("the Ritualist's Fed by the Fallen capstone option, as a flat "
			  "count of Fervour for an enemy dying within ten metres")},
		{TEXT("fervour_decay_grace_metres"),
		 TEXT("the Ravager's starting node and its No Ground Given keystone, "
			  "as flat distances that sum to the eight that keystone names")},

		{TEXT("fervour_per_second"),
		 TEXT("the Masochist's Low Life node, as a flat modifier")},

		// Issue #1518. Both halves of the Ritualist's generator. Zero for every
		// class, and that tree's starting node is the only source of either:
		// "1 per second for each minion you have, and 5 when one of them dies".
		//
		// THE RATE'S ATTRIBUTE STAYS AT ZERO EVEN FOR A CHARACTER HOLDING THE
		// NODE, like `fervour_per_second` above but for a different reason. That
		// one carries a health CONDITION; this one carries the SCALE
		// `minions_held`, and a scaled bonus is worked out against the minions
		// actually out at the moment it is asked for -- it would be stale the
		// moment one was summoned or died. `GainPerSecondStep` asks for it.
		//
		// THE DEATH BONUS IS FOLDED IN, unlike the rate beside it, because its
		// row carries neither a condition nor a scale. That is why
		// `GainOnMinionDeath` passes the attribute's own value as the fallback
		// where the rate passes zero, and it is what makes the Fervour bar
		// appear for a Ritualist that has not summoned anything yet.
		{TEXT("fervour_from_minions"),
		 TEXT("the Ritualist's Fervour node, as a flat modifier scaled by the "
			  "minions held")},
		{TEXT("fervour_on_minion_death"),
		 TEXT("the Ritualist's Fervour node, as a flat modifier")},

		// Issue #985. Whether dropping below half health turns damage taken into
		// Bleeding. Zero for every class, and the Masochist's The Breaking Point
		// is its only source. Unlike its neighbours above, this row carries no
		// required tags and no condition, so it IS folded into the attribute --
		// which is why `UCataclysmDamageConversion` passes the attribute's own
		// value as the fallback when it asks for the stat, where the two above
		// pass zero.
		{TEXT("damage_to_bleeding_on_low_health"),
		 TEXT("the Masochist's The Breaking Point node, as a flat modifier")},

		// Issue #985. How many seconds one turn of that conversion lasts. THE
		// ONLY STAT IN THIS MAP WHOSE BASE IS NEITHER A CLASS LINE NOR A
		// MODIFIER. The node grants an `increased` of 5 per point, and 3 seconds
		// is the base that multiplies: a constant the engine states.
		// `ENGINE_SUPPLIED_BASES` in tools/generate_datatables.py names it, so
		// the check refusing an increase with no base under it can see it too.
		//
		// AND UNTIL ISSUE #1025 THAT CONSTANT REACHED NOTHING. This exemption
		// existed, the tool's exemption existed, and no code put the base on a
		// character, so the stat resolved to zero and the node converted nothing.
		// `UCataclysmPlayerClassStats::EngineSuppliedBases` is what supplies it
		// now, and `EveryEngineSuppliedBaseReachesACharacter` below is what
		// checks that it still does.
		{TEXT("damage_to_bleeding_window"),
		 TEXT("UCataclysmDamageConversion::BaseWindowSeconds, with the "
			  "Masochist's The Breaking Point node increasing it")},

		// Issue #973. The chance a skill does not go on cooldown. Zero for every
		// class, and the Masochist's The Catalyst node is its only source. The
		// attribute stays at zero even for a character holding that node, because
		// its bonus carries a health condition; `ApplyCooldown` asks for the stat
		// rather than reading it.
		{TEXT("cooldown_skip_chance"),
		 TEXT("the Masochist's The Catalyst node, as a flat modifier")},

		// Issue #1026. What share of a hit the character takes, at 100 for
		// normal, and the same again for a hit that is damage over time. The
		// SECOND AND THIRD STATS WHOSE BASE IS NEITHER A CLASS LINE NOR A
		// MODIFIER, after the conversion window above, and they arrive by the
		// same route: `UCataclysmPlayerClassStats::EngineSuppliedBases`.
		//
		// NO CLASS LINE NAMES EITHER AND NONE SHOULD. No affix grants them,
		// nothing scales them and no class differs on them, which is the rule
		// `Cataclysm.Attributes.CharacterSheetIsComplete` uses to decide a stat
		// is off the character sheet. Both attributes also hold 100 for every
		// character, because all three nodes that move them carry a condition.
		{TEXT("damage_taken"),
		 TEXT("UCataclysmDamageCalculation::NormalDamageTaken, with three "
			  "Masochist nodes moving it")},
		{TEXT("damage_over_time_taken"),
		 TEXT("UCataclysmDamageCalculation::NormalDamageTaken, with the "
			  "Masochist's Echoes of Agony node reducing it")},

		// Issue #1032. The chance a melee critical strike applies Bleeding to
		// what it hit. Zero for every class, and the Masochist's Mutilation
		// Mastery is its only source. Its row carries no condition and no
		// scale, so it IS folded into the attribute, which is what lets
		// `UCataclysmVitalAttributeSet` read the attribute directly where the
		// blow lands rather than asking for the stat.
		{TEXT("bleed_on_crit_chance"),
		 TEXT("the Masochist's Mutilation Mastery node, as a flat modifier")},

		// Issue #899. The chance to apply each ailment on a hit. Zero for every
		// class, and no class line names one. The eleven Ailment affixes of
		// `game/Data/Affixes.csv` grant them as flat modifiers, and a passive
		// node or an enchantment row naming one adds to the same stat.
		{TEXT("bleed_chance"), TEXT("the Chance to bleed affix, as a flat modifier")},
		{TEXT("poison_chance"),
		 TEXT("the Chance to poison affix, as a flat modifier")},
		{TEXT("disease_chance"),
		 TEXT("the Chance to disease affix, as a flat modifier")},
		{TEXT("void_splinter_chance"),
		 TEXT("the Chance to apply void splinter affix, as a flat modifier")},
		{TEXT("necrosis_chance"),
		 TEXT("the Chance to necrose affix, as a flat modifier")},
		{TEXT("burn_chance"), TEXT("the Chance to burn affix, as a flat modifier")},
		{TEXT("madness_chance"),
		 TEXT("the Chance to madden affix, as a flat modifier")},
		{TEXT("cripple_chance"),
		 TEXT("the Chance to cripple affix, as a flat modifier")},
		{TEXT("weaken_chance"),
		 TEXT("the Chance to weaken affix, as a flat modifier")},
		{TEXT("shred_chance"), TEXT("the Chance to shred affix, as a flat modifier")},
		{TEXT("stun_chance"), TEXT("the Chance to stun affix, as a flat modifier")},

		// Issue #1767. How LARGE the Cripple and the Weaken this character
		// applies are, where the eleven above are how OFTEN they land. No class
		// line names either and no affix grants either; two Ravager passive
		// nodes are their only sources.
		//
		// A HUNDRED RATHER THAN ZERO FOR A CHARACTER WITH NEITHER NODE, unlike
		// every chance above. These multiply a magnitude instead of adding to a
		// chance, so the neutral value is one times, and it arrives from
		// `UCataclysmPlayerClassStats::EngineSuppliedBases` rather than from a
		// class line. `Cataclysm.PlayerStats.EveryEngineSuppliedBaseReachesACharacter`
		// is what holds that promise.
		// THE TWO ROWS ARE NOT AUTHORED YET, and this says so rather than
		// describing a source that does not exist. The base arrives regardless,
		// so the stat is 100 and not zero for every character today; what is
		// missing is anything that raises it. Issue #1767 carries the rows.
		{TEXT("cripple_magnitude"),
		 TEXT("a base of 100 from EngineSuppliedBases, to be raised by the "
			  "Ravager's Dragging Weight node once that row is authored")},
		{TEXT("weaken_magnitude"),
		 TEXT("a base of 100 from EngineSuppliedBases, to be raised by the "
			  "Ravager's Sapped node once that row is authored")},

		// Issue #1718. Percentage points ADDED to the health threshold a blow
		// must leave a target under for Subjugate to take it as a thrall.
		//
		// NO ENGINE-SUPPLIED BASE, UNLIKE THE TWO ABOVE, and that is the design
		// rather than an omission. The Subjugate skill's own row states the
		// threshold as 50 and is the only place it appears; a base here would
		// state it a second time and win silently, so a re-tune of the row would
		// do nothing. Zero is the right starting value for a bonus, which also
		// means a row moving it takes `flat` and never `increased`.
		{TEXT("possession_threshold_bonus"),
		 TEXT("the Ritualist's Dominion keystone, as a flat modifier, added to "
			  "the threshold the Subjugate skill row states")},

		// Issue #1718. The same shape twice more: a bonus of zero added to a
		// figure a skill's own row states, reaching only the subject its node
		// names. Neither has an engine-supplied base, for the reason the entry
		// above gives.
		{TEXT("minion_reserve_reduction"),
		 TEXT("the Ritualist's Crowned keystone, as a flat modifier, taken off "
			  "the reserve each summoning or deploying skill row states")},
		{TEXT("minion_cap_bonus"),
		 TEXT("the Ritualist's The Swarm keystone, as a flat modifier, added to "
			  "the cap a summoning or deploying skill row states")},

		// Issue #1515. The three energy-shield keystones, and all three are
		// FLAGS rather than bonuses: zero or above zero, with nothing in between
		// meaning anything. Each node states a RULE -- "absorbs damage over time
		// as well as hits", "recharges while you are taking damage", "also
		// restores your Energy Shield" -- and a rule has no magnitude to scale,
		// so a class base would be a number none of them has.
		//
		// THE HALVED RATES TWO OF THEM STATE ARE CONSTANTS AT THEIR READ SITES
		// rather than stats, because the design rows fix them and nothing else
		// grants them. A stat would be a second place to write the same figure.
		{TEXT("shield_absorbs_damage_over_time"),
		 TEXT("the Ritualist's Warded keystone, as a flat flag, read where the "
			  "damage calculation decides whether the shield applies to a hit")},
		{TEXT("shield_recharges_while_damaged"),
		 TEXT("the Ritualist's Ablative keystone, as a flat flag, read where the "
			  "regeneration step scales the shield's recharge")},
		{TEXT("mana_regen_restores_shield"),
		 TEXT("the Ritualist's The Long Game keystone, as a flat flag, read where "
			  "the regeneration step adds a second source to the shield")},

		// Issue #1515. Two Ravager keystones that forbid a defence working, and
		// both are flags for the same reason as the three above: each node states
		// a RULE rather than a magnitude.
		//
		// THEY SIT ON OPPOSITE SIDES OF A BLOW, which is worth saying because
		// every other flag in this list is read on the character holding it. The
		// first protects the holder's own armour; the second is held by an
		// attacker and refuses the DEFENDER's evasion.
		{TEXT("armor_penetration_suppressed"),
		 TEXT("the Ravager's Ironhide keystone, as a flat flag read on the "
			  "defender where an attacker's penetration would be applied")},
		{TEXT("melee_evasion_suppressed"),
		 TEXT("the Ravager's Every Swing Lands keystone, as a flat flag read on "
			  "the attacker where a blow is assembled")},

		// Issue #1515. Whether anything may lower this character's movement
		// speed. TWO KEYSTONES ARE ITS SOURCE, which no other entry in this
		// list has: the Ravager's Relentless grants it always and the third
		// clause of the Ravager's Unstoppable grants it while an enemy is
		// within four metres. They differ only in their row's condition, so
		// they share one stat.
		//
		// THE CONDITIONED ONE IS WHY IT IS READ THROUGH THE PIPELINE. A
		// conditioned row is never folded into the attribute, so a read off
		// the attribute would report zero for ever and the Unstoppable clause
		// would silently do nothing.
		{TEXT("movement_speed_reduction_suppressed"),
		 TEXT("the Ravager's Relentless keystone and the slow clause of its "
			  "Unstoppable keystone, as a flat flag read where the player's "
			  "movement speed resolves")},

		// Issue #1515. Whether a stun on this character ends outright when the
		// character kills the enemy that applied it. Zero for every class, and
		// the second clause of the Ravager's Nothing Moves You is its only
		// source.
		//
		// A STAT OF ITS OWN RATHER THAN THE NODE'S OTHER ONE, and that is
		// measured rather than preferred. The obvious gate is crowd control
		// resistance, which the node's FIRST clause grants -- but ten data rows
		// grant that stat, including two class lines, so reusing it would hand
		// this clause to two whole classes and anyone wearing one affix.
		{TEXT("crowd_control_ends_when_its_applier_dies"),
		 TEXT("the applier-death clause of the Ravager's Nothing Moves You "
			  "keystone, as a flat flag read when a death is announced")},

		// Issue #1039. Whether damage over time deals this character nothing at
		// all. Zero for every class, and the Masochist's Vessel Unbroken
		// capstone option is its only source. A FLAG rather than a reduction,
		// because a Less multiplier is floored at -99 and "no damage at all" is
		// not 99% less; `fervour_loss_suppressed` above is the same shape for
		// the same reason. Its row carries no condition and no scale, so it IS
		// folded into the attribute.
		{TEXT("debuff_damage_suppressed"),
		 TEXT("the Masochist's The Final Vow node, third option, as a flat "
			  "modifier")},

		// Issue #1047. How far this character's retaliation reaches, in METRES.
		// Zero for every class, and the Masochist's Reprisal Wave capstone
		// option is its only source. Zero means it reaches only whatever hit the
		// character, which is what retaliation did for everybody before that
		// option existed.
		//
		// THE ONLY STAT IN THIS MAP MEASURED IN A DISTANCE. Its row carries no
		// condition and no scale, so it IS folded into the attribute.
		{TEXT("retaliation_radius_metres"),
		 TEXT("the Masochist's The First Vow node, second option, as a flat "
			  "modifier")},

		// Issue #1048. Whether this character's life leech applies to its
		// retaliation. Zero for every class, and the Masochist's Feeding Wound
		// capstone option is its only source. A FLAG, and there is nothing else
		// it could be: the node states no quantity at all, because how much is
		// leeched is whatever life leech the character already has.
		{TEXT("retaliation_leeches"),
		 TEXT("the Masochist's The Second Vow node, second option, as a flat "
			  "modifier")},

		// Issue #1050. What share of its MISSING health one nova deals, as a
		// percentage. Zero for every class, and the Masochist's Unstable Aura
		// is its only source. Its row carries a health condition, so the
		// attribute stays at zero even for a character holding the node and
		// `UCataclysmNova` asks for the stat rather than reading it.
		{TEXT("nova_damage_of_missing_health"),
		 TEXT("the Masochist's Unstable Aura node, as a flat modifier")},

		// Issues #1057 and #1058. How much longer a debuff this character's aura
		// applies lasts, and the chance one nearby enemy catches a debuff when a
		// debuff on this character deals damage. Zero for every class, and one
		// node of one tree is the only source of each.
		//
		// NEITHER ROW CARRIES A CONDITION, so both attributes really do hold the
		// figure for a character holding the node. Both are still asked for
		// through `StatForSkill` rather than read, so that a later row carrying
		// one is not dropped in silence.
		//
		// ZERO IS ALSO HOW THE CODE KNOWS THE CHARACTER HAS NO POINTS IN THE
		// NODE, which is why neither may ever gain a class line stating one:
		// `UCataclysmContagion::AuraStep` would then pulse for every character
		// in the game.
		{TEXT("aura_debuff_duration"),
		 TEXT("the Masochist's Beacon of Despair node, as a flat modifier")},
		{TEXT("debuff_spread_chance"),
		 TEXT("the Masochist's Contagious Torment node, as a flat modifier")},

		// Issues #1060 and #1061, the last two nodes of that tree. The chance
		// a dying creature's debuffs pass on, and the increased damage dealt
		// to an enemy carrying a debuff the attacker also carries. Zero for
		// every class, and neither may ever gain a class line: the first is
		// how the code knows the character holds the node, and the second is
		// meaningless without a target in hand, which a class line has not.
		{TEXT("debuff_spread_on_death_chance"),
		 TEXT("the Masochist's Empathic Link node, as a flat modifier")},
		{TEXT("damage_to_enemies_sharing_a_debuff"),
		 TEXT("the Masochist's Wound Channeling node, as a flat modifier")},

		// Issue #1051. How much Fervour each cast grants, and whether skills
		// cost health at all. Zero for every class, and the first option of
		// the Masochist's The Final Vow is the only source of either. Both
		// rows carry a health condition -- the FIRST condition in the game
		// that is a STRICTLY-below health threshold rather than an at-or-below
		// one -- so both attributes stay at zero even for a character holding
		// the option, and both are asked for rather than read.
		{TEXT("fervour_per_cast"),
		 TEXT("the Masochist's The Final Vow node, first option, as a flat "
			  "modifier")},
		{TEXT("health_cost_suppressed"),
		 TEXT("the Masochist's The Final Vow node, first option, as a flat "
			  "modifier")},

		// Issue #1067. Whether this character has traded its mana pool for
		// health. Zero for every class, and Water to Blood -- the first
		// option of the Masochist's first capstone -- is its only source. A
		// class line stating it would give every character of that class no
		// mana pool, which is a capstone decision and not a class trait.
		{TEXT("mana_pool_becomes_health"),
		 TEXT("the Masochist's The First Vow node, first option, as a flat "
			  "modifier")},

		// Issue #1033. How long a lasting harmful effect on this character runs,
		// at 100 for normal. The THIRD stat whose base is neither a class line
		// nor a modifier, after the two damage-taken stats above, and it arrives
		// the same way: `UCataclysmPlayerClassStats::EngineSuppliedBases`.
		//
		// BOTH OF ITS SOURCES LENGTHEN RATHER THAN SHORTEN, which reads backwards
		// until you know the class: eleven Masochist nodes pay the character for
		// each harmful effect it is carrying, so carrying them longer is a
		// benefit. Neither row carries a condition, so the attribute really holds
		// the figure for a character holding those nodes.
		{TEXT("debuff_duration_taken"),
		 TEXT("UCataclysmDebuffs::NormalDuration, with the Masochist's Symphony "
			  "of Pain and Vessel of Plagues nodes lengthening it")},

		// Issue #1069, Rock Bottom, the first option of the Masochist's third
		// capstone. Three stats: whether a health cost this character cannot
		// pay becomes debt, whether dropping to low health clears that debt,
		// and how much Fervour the same drop grants. Zero for every class.
		//
		// NONE OF THE THREE ROWS CARRIES A CONDITION, so all three attributes
		// really do hold the figure for a character holding the option, and all
		// three are asked for through `StatForSkill` with the ATTRIBUTE as the
		// fallback rather than zero. The Final Vow's two rows above pass zero
		// instead, and the difference is exactly that: theirs carry a health
		// condition and a conditional bonus is never folded into an attribute.
		//
		// NO CLASS LINE MAY EVER NAME ONE. A class that floored its costs at 1
		// health would hand every character of that class a capstone decision,
		// and the Fervour stat reading zero is also how
		// `UCataclysmLowHealthRelief` knows a character does not hold the
		// option at all.
		{TEXT("unpayable_health_cost_becomes_debt"),
		 TEXT("the Masochist's The Second Vow node, first option, as a flat "
			  "modifier")},
		{TEXT("debt_cleared_on_dropping_low"),
		 TEXT("the Masochist's The Second Vow node, first option, as a flat "
			  "modifier")},
		{TEXT("fervour_on_dropping_low"),
		 TEXT("the Masochist's The Second Vow node, first option, as a flat "
			  "modifier")},

		// Issue #1070, Ceaseless Penance, the third option of that same
		// capstone. Whether the debuffs on this character stop counting down.
		// Zero for every class AND zero for a character holding the option,
		// because its row carries `health_above 50` -- so this one is asked for
		// with a fallback of zero, unlike its three neighbours above.
		{TEXT("debuffs_do_not_expire"),
		 TEXT("the Masochist's The Second Vow node, third option, as a flat "
			  "modifier")},

		// Issue #41, slice 3. Whether this character's skills may be used at
		// all. Zero for every class, and NO CLASS LINE MAY EVER NAME IT: a
		// class whose every member could not use their skills is not a class.
		// Its sources are an enchantment scoped to the Ultimate slot and the
		// dungeon modifier Edict of Silence unscoped.
		{TEXT("skill_locked"),
		 TEXT("the enchantment that disables the Ultimate slot, and the dungeon "
			  "modifier Edict of Silence, as a flat modifier")},

		// Issue #1071, Carnivore, the second option of the Masochist's fourth
		// capstone. Whether taking a hit grants a stack of Carnage, and whether
		// Carnage has no maximum. Zero for every class, and neither row carries
		// a condition.
		//
		// NO CLASS LINE MAY EVER NAME EITHER. A class whose every member earned
		// Carnage from being hit would make the option's first clause grant
		// nothing, and one whose every member held Carnage without limit would
		// hand a capstone's second clause to a character that never took it.
		{TEXT("carnage_from_damage_taken"),
		 TEXT("the Masochist's The Final Vow node, second option, as a flat "
			  "modifier")},
		{TEXT("carnage_has_no_maximum"),
		 TEXT("the Masochist's The Final Vow node, second option, as a flat "
			  "modifier")},

		// Issue #45. The two stats a STAGGERING character carries: how long a
		// stagger it applies runs, at 100 for normal, and how far the health
		// ceiling above which it cannot stagger is lowered, at 0 for no ceiling.
		//
		// NO CLASS LINE MAY NAME EITHER. A class whose every member staggered
		// for longer would fold an enchantment's whole effect into the class,
		// and one whose every member could not stagger a healthy enemy would
		// hand out a drawback nobody chose.
		//
		// THE FIRST HAS A BASE OF 100 ALL THE SAME, from `EngineSuppliedBases`
		// and not from a class line. Without it the stat resolves to zero, every
		// stagger is scaled to nothing and refused, and no player staggers
		// anything -- which is what happened before that entry was added.
		{TEXT("stagger_duration"),
		 TEXT("the enchantment that lengthens the stagger effects this character "
			  "applies, as an increased modifier over a base of 100 supplied by "
			  "EngineSuppliedBases")},
		{TEXT("stagger_health_ceiling_reduction"),
		 TEXT("the enchantment that refuses to stagger an enemy above half "
			  "health, as a flat modifier")},

		// Issue #45. How long a skill this character lands knocks its target
		// down for. Zero for every class, and NO CLASS LINE MAY NAME IT: a class
		// whose every skill knocked its target down would hand out an
		// enchantment's whole effect for free, and the row scopes itself to
		// charge skills which a class line cannot express.
		{TEXT("knockdown_seconds"),
		 TEXT("the enchantment that makes charge skills knock down, as a flat "
			  "modifier scoped by RequiredTags to Keyword.Charge")},
	};

	for (const TPair<FString, FGameplayAttribute>& Pair : Map)
	{
		if (const FString* Source = SuppliedFromElsewhere.Find(Pair.Key))
		{
			// The exemption is only honest if the stat really is absent from the
			// table. One that quietly gained a class line would sit here
			// unchecked, so say so rather than skipping in silence.
			TestFalse(FString::Printf(
				TEXT("'%s' is supplied by %s, so no class line should name it. "
					 "One does now, so remove it from the exemption list in "
					 "this test."), *Pair.Key, **Source),
				NamedByTheDesign.Contains(Pair.Key));
			continue;
		}

		TestTrue(FString::Printf(
			TEXT("the mapped stat '%s' is one the class table actually names"),
			*Pair.Key),
			NamedByTheDesign.Contains(Pair.Key));
	}

	// AND EVERY EXEMPTION STILL DESCRIBES A STAT THAT EXISTS. Issue #1032.
	//
	// THE THIRD DIRECTION, AND IT WAS MISSING. The two loops above read the
	// class table and the map; nothing read this list, so an entry whose stat
	// had been removed from `StatToAttribute` -- or renamed -- became
	// unreachable and stayed here stating where a stat that no longer exists
	// gets its value. There are 90 entries and most are several lines of
	// explanation, so a stale one costs a reader real time and no test run
	// mentions it.
	//
	// NINETY COUNTED ON 2026-09-13, when this line said 72. Counted rather than
	// incremented, for the reason the paragraph below gives: an increment
	// carries a wrong figure forward, and this one had drifted by sixteen in
	// four days. The two added that day were the Cripple and Weaken magnitudes.
	//
	// THAT FIGURE SAID 52 UNTIL 2026-09-09 AND WAS COUNTED RATHER THAN GUESSED
	// WHEN IT WAS CORRECTED. It had drifted by eighteen before issue #1518
	// added the last two, so most of the gap is not that issue's. Nothing
	// checks this number, which is why it could drift at all; it is prose in a
	// comment and the loop below counts the list itself.
	//
	// FOUND BY A GUARD PROOF RATHER THAN BY READING. Deleting
	// `bleed_on_crit_chance` from `StatToAttribute` was predicted to fail this
	// test and did not. It failed
	// `Cataclysm.Passives.EveryStatAPassiveNodeGrantsHasAnAttributeBehindIt`
	// instead, which reads the passive effect table -- so that one covers a
	// stat a NODE grants, and would not have covered a stat supplied by a
	// weapon, by the skill in hand or by an engine constant. Most of the
	// entries below are one of those.
	for (const TPair<FString, FString>& Exemption : SuppliedFromElsewhere)
	{
		TestTrue(FString::Printf(
			TEXT("'%s' is exempted here as being supplied by %s, and it is "
				 "still a stat StatToAttribute maps. An exemption for a stat "
				 "nothing maps any more describes nothing; delete it."),
			*Exemption.Key, *Exemption.Value),
			Map.Contains(Exemption.Key));
	}

	return true;
}

// --------------------------------------------------------------------------
// Applying it
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmClassLineReachesTheCharacter,
	"Cataclysm.PlayerStats.ApplyingTheClassLineReplacesThePlaceholderHealth")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedCharacter Character(World);

	// THE PLACEHOLDER, ASSERTED BEFORE IT IS REPLACED. Without this the test
	// below would pass just as well if the attribute set already happened to
	// start at the class value, and would then say nothing about whether
	// applying did anything.
	const float Placeholder =
		Character.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute());
	TestEqual(TEXT("a character with no class line starts on the placeholder"),
		Placeholder, 100.0f);

	const int32 Level = 20;
	const int32 Written = UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, Level);

	TestEqual(TEXT("every mapped stat was written"),
		Written, UCataclysmPlayerClassStats::StatToAttribute().Num());

	// READ BACK OFF THE TABLE, not written here as a number. What this asks is
	// whether the character got what the design says, and a second copy of 385
	// in this file would answer a different and easier question.
	const float Expected = UCataclysmClassStats::BaseFor(
		Table, UCataclysmClassStats::DefaultClassName, TEXT("max_health"), Level);

	TestEqual(TEXT("maximum health is what the class table says at that level"),
		Character.Read(UCataclysmVitalAttributeSet::GetMaxHealthAttribute()),
		Expected);

	// AND IT IS MEANINGFULLY MORE THAN THE PLACEHOLDER, which is the point of
	// the whole change. A per-level column that resolved to nothing would still
	// satisfy the check above.
	TestTrue(FString::Printf(
		TEXT("and is well above the placeholder: %.0f against %.0f"),
		Expected, Placeholder),
		Expected > Placeholder * 3.0f);

	// THE POOLS ARE FILLED, AND THIS IS THE ORDERING TRAP. The vital attribute
	// set clamps current health to maximum health, so filling before raising
	// leaves the character on 100 with a maximum of 385 and a health bar that
	// starts a quarter full.
	TestEqual(TEXT("current health is filled to the new maximum"),
		Character.Read(UCataclysmVitalAttributeSet::GetHealthAttribute()),
		Expected);
	TestEqual(TEXT("and so is mana"),
		Character.Read(UCataclysmVitalAttributeSet::GetManaAttribute()),
		Character.Read(UCataclysmVitalAttributeSet::GetMaxManaAttribute()));

	return true;
}

CATACLYSM_TEST(FCataclysmEveryEngineSuppliedBaseReachesACharacter,
	"Cataclysm.PlayerStats.EveryEngineSuppliedBaseReachesACharacter")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const TMap<FName, float>& Stated =
		UCataclysmPlayerClassStats::EngineSuppliedBases();

	// THE MAP IS NOT EMPTY, so this cannot pass by having nothing to check. A
	// guard over an empty list is a guard that cannot fail.
	if (!TestTrue(TEXT("the engine states at least one base"),
				  Stated.Num() > 0))
	{
		return false;
	}

	const FScopedCharacter Character(World);
	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, 20);

	for (const TPair<FName, float>& Pair : Stated)
	{
		const FString Stat = Pair.Key.ToString();

		// TWO ROUTES REACH A CHARACTER, AND EACH BASE HAS TO BE ON ONE. `ApplyTo`
		// resolves every stat in `StatToAttribute` and writes it to its attribute,
		// and resolves every stat in `StatsWithNoAttribute` onto the character's
		// stat line; both read this base by name. A stat on neither list is never
		// resolved, so its base is never consulted. Until issue #1686 every base
		// here had an attribute, and this test required one; `non_critical_damage`
		// is the first with none, asked per blow through the stat pipeline.
		const bool bNoAttribute =
			UCataclysmPlayerClassStats::StatsWithNoAttribute().Contains(Stat);
		const FGameplayAttribute* Attribute =
			UCataclysmPlayerClassStats::StatToAttribute().Find(Stat);
		if (!bNoAttribute
			&& !TestNotNull(*FString::Printf(
				TEXT("'%s' has an attribute to be written to"), *Stat),
				Attribute))
		{
			continue;
		}

		// AND NO CLASS LINE MAY NAME IT. This base replaces the class line
		// unconditionally, so a class line naming one of these would be read by
		// `BaseFor`, thrown away, and nothing would say so.
		TestEqual(*FString::Printf(
			TEXT("and no class line states '%s', because this base replaces one"),
			*Stat),
			UCataclysmClassStats::BaseFor(
				Table, UCataclysmClassStats::DefaultClassName, Stat, 20),
			0.0f);

		// AND THE VALUE REALLY ARRIVES. This is the assertion issue #1025 was
		// about. `damage_to_bleeding_window` was named by `ENGINE_SUPPLIED_BASES`
		// in `tools/generate_datatables.py`, which exempted it from the check
		// refusing an increase with no base under it, and nothing anywhere put
		// the base on a character -- so it resolved to zero, The Breaking Point
		// opened a conversion window of zero seconds, and it converted nothing.
		TestEqual(*FString::Printf(
			TEXT("and a character built from the class table holds '%s' at %.2f"),
			*Stat, Pair.Value),
			bNoAttribute
				// ON THE STAT LINE for a stat with no attribute. The fallback of -1
				// is answered only when the line holds no entry, and no stated base
				// is -1, so it cannot be mistaken for the base arriving.
				? Character.AbilitySystem->StatForSkill(
					Pair.Key, FGameplayTagContainer(), -1.0f)
				: Character.Read(*Attribute),
			Pair.Value, 0.001f);
	}

	return true;
}

CATACLYSM_TEST(FCataclysmClassLinesDifferFromEachOther,
	"Cataclysm.PlayerStats.TheThreeDemonicClassesGetDifferentStatLines")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FGameplayAttribute Armour =
		UCataclysmCombatAttributeSet::GetArmorAttribute();

	TMap<FString, float> HealthOf;
	TMap<FString, float> ArmourOf;

	for (const TCHAR* ClassName :
			{TEXT("Ravager"), TEXT("Ritualist"), TEXT("Masochist")})
	{
		const FScopedCharacter Character(World);
		UCataclysmPlayerClassStats::ApplyTo(Character.AbilitySystem, Table,
											FString(ClassName), /*Level=*/20);
		HealthOf.Add(ClassName, Character.Read(MaxHealth));
		ArmourOf.Add(ClassName, Character.Read(Armour));
	}

	// THE DESIGN'S OWN ORDERING, which is independent of anything this code
	// computes: the Masochist is written at 150 base health, the Ravager at 130
	// and the Ritualist at 70. If applying resolved every class to the same
	// shared line, all three would be equal and every other test here would
	// still pass.
	TestTrue(FString::Printf(
		TEXT("the Masochist has more health than the Ravager: %.0f against %.0f"),
		HealthOf[TEXT("Masochist")], HealthOf[TEXT("Ravager")]),
		HealthOf[TEXT("Masochist")] > HealthOf[TEXT("Ravager")]);
	TestTrue(FString::Printf(
		TEXT("and the Ravager more than the Ritualist: %.0f against %.0f"),
		HealthOf[TEXT("Ravager")], HealthOf[TEXT("Ritualist")]),
		HealthOf[TEXT("Ravager")] > HealthOf[TEXT("Ritualist")]);

	// A CLASS THAT DECLINES A STAT GETS ZERO, which is the design working rather
	// than failing. The Ritualist takes no armour at all.
	TestEqual(TEXT("the Ritualist has no armour, because its line names none and "
				   "the shared line names none either"),
		ArmourOf[TEXT("Ritualist")], 0.0f);
	TestTrue(FString::Printf(
		TEXT("while the Ravager has some: %.1f"), ArmourOf[TEXT("Ravager")]),
		ArmourOf[TEXT("Ravager")] > 0.0f);

	return true;
}

CATACLYSM_TEST(FCataclysmChosenLevelIsClamped,
	"Cataclysm.PlayerStats.TheChosenLevelStaysInsideTheDesignedRange")
{
	// A CONSOLE VARIABLE IS TYPED AT BY A PERSON, so it can hold anything. Level
	// zero would resolve every per-level term to one level below the base, which
	// gives a character less than the written base rather than more.
	IConsoleVariable* Level =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.PlayerLevel"));
	if (!Level)
	{
		AddError(TEXT("Cataclysm.PlayerLevel does not exist."));
		return false;
	}

	const int32 Restore = Level->GetInt();
	ON_SCOPE_EXIT { Level->Set(Restore, ECVF_SetByCode); };

	// WRITTEN THROUGH THE CONSOLE VARIABLE AND NOT THROUGH THE C++ VARIABLE,
	// because a console variable keeps a copy of the value beside whatever it
	// references and assigning to the other one leaves the two disagreeing.
	Level->Set(0, ECVF_SetByCode);
	TestEqual(TEXT("level zero is raised to one"),
		UCataclysmPlayerClassStats::ChosenLevel(), 1);

	Level->Set(-50, ECVF_SetByCode);
	TestEqual(TEXT("and so is a negative level"),
		UCataclysmPlayerClassStats::ChosenLevel(), 1);

	Level->Set(9999, ECVF_SetByCode);
	TestEqual(TEXT("and a level past the end is lowered to the maximum"),
		UCataclysmPlayerClassStats::ChosenLevel(), UCataclysmClassStats::MaxLevel);

	Level->Set(20, ECVF_SetByCode);
	TestEqual(TEXT("and a level inside the range is left alone"),
		UCataclysmPlayerClassStats::ChosenLevel(), 20);

	return true;
}

// --------------------------------------------------------------------------
// The seam: a real player character, not a bare ability system
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmPlayerCharacterGetsItsClassLine,
	"Cataclysm.PlayerStats.APlayerCharacterLeavesThePlaceholderBehind")
{
	using namespace CataclysmPlayerClassStatsTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("player character"), Character))
	{
		return false;
	}

	// The client path, which is the one a test world can reach: it has no
	// controller to possess with, so PossessedBy itself is out of reach. This
	// wires the ability system up exactly as possession would.
	Character->SetPlayerState(PlayerState);
	Character->OnRep_PlayerState();

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("ability system"), ASC))
	{
		return false;
	}

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	// WIRING THE ABILITY SYSTEM UP MUST NOT BY ITSELF APPLY A STAT LINE. This is
	// the assertion that pins where the call lives: it belongs on possession,
	// which happens once, and not on InitAbilityActorInfo, which is documented
	// as safe to run twice and does.
	TestEqual(TEXT("initialising the ability system leaves the placeholder alone"),
		ASC->GetNumericAttribute(MaxHealth), 100.0f);

	Character->ApplyChosenClassStats();

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	const float Expected = UCataclysmClassStats::BaseFor(
		Table, UCataclysmPlayerClassStats::ChosenClass(), TEXT("max_health"),
		UCataclysmPlayerClassStats::ChosenLevel());

	TestEqual(TEXT("and the character then has the class table's health"),
		ASC->GetNumericAttribute(MaxHealth), Expected);

	// THE FIGURE THAT DECIDES WHETHER A FLOOR CAN BE FINISHED. A Brute deals 35
	// a hit -- BruteAttackDamage in CataclysmGameMode.h -- and at 100 health
	// three of them killed the character. This says the default level leaves
	// enough health for at least the eight to ten hits
	// sim/cataclysm_sim/enemy_stats.py fitted the enemy damage constants around.
	const float BruteHitsSurvived = Expected / 35.0f;
	TestTrue(FString::Printf(
		TEXT("which is %.0f health, or %.1f hits from a Brute"),
		Expected, BruteHitsSurvived),
		BruteHitsSurvived >= 8.0f);

	return true;
}

CATACLYSM_TEST(FCataclysmPossessionAppliesTheClassLine,
	"Cataclysm.PlayerStats.BeingPossessedIsWhatAppliesTheClassLine")
{
	using namespace CataclysmPlayerClassStatsTest;

	// WITHOUT THIS TEST NOTHING WOULD NOTICE THE CALL BEING DELETED, and that is
	// precisely the defect this whole change exists to repair: a function that
	// worked, that had tests, and that no code path reached. The test above
	// drives ApplyChosenClassStats by hand, so it would pass just as well with
	// PossessedBy calling nothing at all.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();

	// A PLAYER CONTROLLER AND NOT A BARE AController, WHICH IS ABSTRACT and
	// fails to spawn with "class Controller is abstract" in the log rather than
	// with an error the test would otherwise attribute to something else.
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(
			FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player state"), PlayerState)
		|| !TestNotNull(TEXT("controller"), Controller)
		|| !TestNotNull(TEXT("player character"), Character))
	{
		return false;
	}

	// A PLAYER STATE ATTACHED BY HAND. A test world has no game mode, and a game
	// mode is what normally creates a player state, so it is put on the
	// controller directly. APawn::PossessedBy copies it onto the pawn only when
	// the controller has one, which is why this line is what makes the rest
	// work.
	Controller->SetPlayerState(PlayerState);

	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	// THE PAWN'S HALF OF POSSESSION, AND NOT AController::Possess. That would
	// also run the controller's own half -- view targets, restarting the client
	// -- which a test world with no player has no business doing, and none of it
	// is what this test is about.
	Character->PossessedBy(Controller);

	UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("possession wired the ability system up"), ASC))
	{
		return false;
	}

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	const float Expected = UCataclysmClassStats::BaseFor(
		Table, UCataclysmPlayerClassStats::ChosenClass(), TEXT("max_health"),
		UCataclysmPlayerClassStats::ChosenLevel());

	TestEqual(TEXT("being possessed is what gives the character its health"),
		ASC->GetNumericAttribute(MaxHealth), Expected);
	TestNotEqual(TEXT("and it is not the placeholder the attribute set writes"),
		ASC->GetNumericAttribute(MaxHealth), 100.0f);

	return true;
}

// --------------------------------------------------------------------------
// The units of the remembered attack damage bracket. Issue #963.
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmAttackDamageBracketIsAFraction,
	"Cataclysm.PlayerStats.TheAttackDamageBracketIsRememberedAsAFraction")
{
	using namespace CataclysmPlayerClassStatsTest;

	// WHAT WENT WRONG AND WHY NOTHING SAW IT. `ApplyTo` passed
	// `Breakdown.SumOfIncreases`, which is in percentage points, into
	// `SetAttackDamageIncreases`, which every reader treats as a fraction, so
	// the stored figure was a hundred times too large.
	//
	// THE ERROR CANCELLED WHENEVER THE CONDITIONAL PART WAS ZERO, which is why
	// it survived. A hit is `weapon x percent x (1 + I + C) / (1 + I)`, and with
	// C at zero that last factor is one for any I at all, right or wrong. It
	// shows only when something else joins the bracket: one of the eight
	// damage-against-a-type affixes, or a passive node that increases damage
	// below a health threshold.
	//
	// AND THE TEST THAT EXISTED COULD NOT SEE IT.
	// `Cataclysm.ConditionalDamage.ItAddsIntoTheSameBracketRatherThanMultiplying`
	// sets the bracket by hand with a fraction, so it agrees with the reader and
	// never runs the writer. This one runs the writer.
	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedCharacter Character(World);

	// A WEAPON AND ONE ROLLED INCREASE, which is how attack damage really
	// arrives: no class line names the stat, deliberately, because the damage
	// comes from what the character is holding.
	FCataclysmStatModifier Weapon;
	Weapon.Bucket = ECataclysmStatBucket::Flat;
	Weapon.Source = ECataclysmModifierSource::GearImplicit;
	Weapon.Value = 1'000.0f;

	FCataclysmStatModifier Increase;
	Increase.Bucket = ECataclysmStatBucket::Increased;
	Increase.Source = ECataclysmModifierSource::GearAffix;
	Increase.Value = 125.0f;    // PERCENTAGE POINTS, which is what a row holds.

	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	Modifiers.Add(FName(TEXT("attack_damage")), {Weapon, Increase});

	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, /*Level=*/20, &Modifiers);

	// THE ATTRIBUTE IS THE FINISHED FIGURE: 1000 flat multiplied by 2.25.
	const float Finished =
		Character.Read(UCataclysmCombatAttributeSet::GetAttackDamageAttribute());
	TestEqual(TEXT("the attribute carries the finished figure"),
		Finished, 2'250.0f, 0.5f);

	// AND THE REMEMBERED BRACKET IS THE FRACTION THAT PRODUCED IT. 125 here,
	// which is what it held before this issue, is the failure.
	const float Remembered =
		Character.AbilitySystem->GetAttackDamageIncreases();
	TestEqual(FString::Printf(
		TEXT("the bracket is remembered as a fraction, and was %.2f"),
		Remembered),
		Remembered, 1.25f, 0.001f);

	// AND IT REALLY REOPENS THE ATTRIBUTE, which is the only thing the figure is
	// for. `UCataclysmSkillEffects::ApplyHit` divides by one plus this on every
	// hit and has to get the flat bucket back.
	TestEqual(TEXT("so dividing the attribute by it recovers the weapon"),
		Finished / (1.0f + Remembered), 1'000.0f, 0.5f);

	return true;
}

CATACLYSM_TEST(FCataclysmApplyingRefusesNothing,
	"Cataclysm.PlayerStats.ApplyingWithNothingToApplyToWritesNothing")
{
	// Both of these are reachable: the table is missing on a checkout whose
	// data assets have not been built, and a caller can hold no ability system
	// before possession.
	TestEqual(TEXT("no ability system writes nothing"),
		UCataclysmPlayerClassStats::ApplyTo(
			nullptr, UCataclysmPlayerClassStats::LoadTable(),
			UCataclysmClassStats::DefaultClassName, 20), 0);

	return true;
}

// --------------------------------------------------------------------------
// Moving a stat from "read off the attribute" to "asked for through the
// pipeline" must not change what a character without such a row already had.
// Issue #947.
//
// WHY THIS CONTROL EXISTS AND WHAT IT CAN CATCH. `StatForSkill` does not return
// the attribute when a stat has inputs recorded -- it recomputes the stat from
// its base through the whole modifier list in one pipeline pass. So the claim
// "nothing without a scoped row is changed" is a real claim about two
// arithmetics agreeing, not a tautology, and it can fail: `ApplyTo` and the
// pipeline have to sum the same increases over the same base. The header of
// `StatForSkill` gives the case that makes the difference visible -- a base of
// 100 carrying an unscoped +50% and a scoped +50% is 200 through one pass and
// 225 through two.
//
// AND WHY IT IS WRITTEN ON A CHARACTER THAT HAS RUN `ApplyTo`. A character with
// no recorded inputs takes `StatForSkill`'s fallback and answers whatever it was
// handed, so the same assertions would pass with the pipeline entirely broken.
// The sentinel below is what refuses that: if the answer IS the fallback, the
// control measured nothing and says so instead of passing.
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmAskedStatsMatchTheAttributeWithoutAScopedRow,
	"Cataclysm.PlayerStats.AskingForAStatAnswersItsAttributeWhenNoRowIsScoped")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	struct FCase
	{
		const TCHAR* Stat;
		FGameplayAttribute Attribute;
	};
	const FCase Cases[] = {
		// ONE ENTRY PER STAT MOVED FROM A PLAIN ATTRIBUTE READ TO AN ASK, added
		// as each is wired. Issue #947.
		{TEXT("crit_multiplier"),
		 UCataclysmCombatAttributeSet::GetCritMultiplierAttribute()},
		{TEXT("penetration"),
		 UCataclysmCombatAttributeSet::GetPenetrationAttribute()},
		{TEXT("armor_penetration"),
		 UCataclysmCombatAttributeSet::GetArmorPenetrationAttribute()},
		{TEXT("armor"), UCataclysmCombatAttributeSet::GetArmorAttribute()},
		{TEXT("evasion"), UCataclysmCombatAttributeSet::GetEvasionAttribute()},
		{TEXT("block_chance"),
		 UCataclysmCombatAttributeSet::GetBlockChanceAttribute()},

		// ALL THREE, THOUGH ONLY dot_damage IS ASKED FOR BY A ROW. The other two
		// were changed for consistency inside one function, so no row-level test
		// exercises them and this control is the only thing behind them.
		{TEXT("dot_damage"),
		 UCataclysmCombatAttributeSet::GetDotDamageAttribute()},
		{TEXT("dot_duration"),
		 UCataclysmCombatAttributeSet::GetDotDurationAttribute()},
		{TEXT("dot_frequency"),
		 UCataclysmCombatAttributeSet::GetDotFrequencyAttribute()},

		// ALL THREE LEECH STATS, THOUGH ONLY life_leech IS ASKED FOR BY A ROW,
		// for the same reason the three above are all here.
		{TEXT("life_leech"),
		 UCataclysmVitalAttributeSet::GetLifeLeechAttribute()},
		{TEXT("mana_leech"),
		 UCataclysmVitalAttributeSet::GetManaLeechAttribute()},
		{TEXT("energy_shield_leech"),
		 UCataclysmVitalAttributeSet::GetEnergyShieldLeechAttribute()},

		// THE TWO HEALTH COST STATS, which are two separate read sites and not
		// one -- a share of MAXIMUM health and a share of CURRENT health.
		{TEXT("added_health_cost"),
		 UCataclysmClassResourceAttributeSet::GetAddedHealthCostAttribute()},
		{TEXT("added_health_cost_of_current"),
		 UCataclysmClassResourceAttributeSet::GetAddedHealthCostOfCurrentAttribute()},

		// THE ONE ALREADY WIRED, AS A POSITIVE CONTROL ON THE CONTROL. Critical
		// strike chance was moved to an ask under issue #959 and nothing has
		// complained since, so if this row ever fails the fault is in this test
		// or in the pipeline rather than in the change being made beside it.
		{TEXT("crit_chance"),
		 UCataclysmCombatAttributeSet::GetCritChanceAttribute()},
	};

	const FScopedCharacter Character(World);

	// BUILT TWICE ON PURPOSE: ONCE BARE, THEN AGAIN CARRYING A MODIFIER.
	// `ApplyTo` replaces the recorded stat line every time it runs, so the first
	// pass leaves a character with nothing on it and the second leaves the one
	// the comparison at the end is made against.
	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName,
		UCataclysmPlayerClassStats::DefaultLevel);

	const UCataclysmAbilitySystemComponent* Asking =
		Cast<UCataclysmAbilitySystemComponent>(Character.AbilitySystem);
	if (!TestNotNull(TEXT("the character's ability system is this project's"),
					 Asking))
	{
		return false;
	}

	// A VALUE NO STAT CAN HOLD, SO THE FALLBACK IS RECOGNISABLE. `StatForSkill`
	// returns what it is handed when a stat has no recorded inputs. Handing it a
	// number the pipeline cannot produce turns "this stat was never recorded"
	// from a silent pass into a named failure.
	//
	// IT IS THE REASON THIS TEST'S OWN FAULT WAS FOUND RATHER THAN LIVED WITH.
	// Nine stats were reaching the fallback and the sentinel named every one of
	// them. Without it they would have compared the fallback against the
	// attribute and the test would have reported fifteen stats checked.
	constexpr float Sentinel = -98'765.0f;

	// THE FIRST OF TWO CLAIMS: A BARE CHARACTER RECORDS NOTHING FOR SOME OF
	// THESE STATS. `UCataclysmPlayerClassStats::ApplyTo` records a stat's inputs
	// only when the character carries at least one modifier for it, and says so
	// at the line that does it: "ONLY A STAT THAT HAS MODIFIERS IS RECORDED.
	// With none the pipeline returns the base, which is exactly what the
	// attribute below ends up holding."
	//
	// IT IS HERE TO STOP THE SETUP BELOW BEING DELETED AS POINTLESS. On
	// 2026-09-13 this test ran for the first time and failed on nine of its
	// fifteen stats, because the character was bare and nine were never
	// recorded: it was checking six and stepping over nine while reporting
	// itself as covering fifteen. Giving each stat a modifier is what fixes
	// that, and without this assertion nothing tells the next reader that the
	// setup is load-bearing rather than clutter.
	//
	// A COUNT AND THE NAMES, NOT A FIXED NINE. Which stats a bare character
	// happens to carry modifiers for is not this test's business and will
	// change. That SOME are unrecorded is the property being pinned. If it ever
	// reaches zero the setup below has genuinely become unnecessary, and that is
	// worth a failure rather than silence.
	TArray<FString> Unrecorded;
	for (const FCase& Case : Cases)
	{
		if (FMath::IsNearlyEqual(
				Asking->StatForSkill(FName(Case.Stat), FGameplayTagContainer(),
									 Sentinel),
				Sentinel))
		{
			Unrecorded.Add(Case.Stat);
		}
	}
	TestTrue(
		*FString::Printf(
			TEXT("a bare character records nothing for at least one of the %d "
				 "stats, so giving each a modifier below is what makes this "
				 "test check them at all; it recorded nothing for %d: %s"),
			static_cast<int32>(UE_ARRAY_COUNT(Cases)), Unrecorded.Num(),
			*FString::Join(Unrecorded, TEXT(", "))),
		Unrecorded.Num() > 0);

	// NOW ONE MODIFIER PER STAT, SO EVERY ONE IS RECORDED AND EVERY ONE IS
	// ACTUALLY COMPARED BY THE LOOP BELOW.
	//
	// FLAT, AND SMALL ON PURPOSE. Flat because several of these stats have no
	// base, and an increase multiplies nothing by a percentage. Small because
	// several are percentages with a cap, and a large figure would test whether
	// a clamp binds rather than whether the two arithmetics agree.
	//
	// UNSCOPED, WHICH IS THE WHOLE POINT. The claim is that a character with no
	// row scoped to a skill gets the same answer from the pipeline as from the
	// attribute. A modifier carrying a required tag would be a different test,
	// and the scoped case is covered by the per-stat tests instead.
	FCataclysmStatModifier Carried;
	Carried.Bucket = ECataclysmStatBucket::Flat;
	Carried.Source = ECataclysmModifierSource::GearAffix;
	Carried.Value = 3.0f;

	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	for (const FCase& Case : Cases)
	{
		Modifiers.Add(FName(Case.Stat), {Carried});
	}

	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName,
		UCataclysmPlayerClassStats::DefaultLevel, &Modifiers);

	// THE SECOND CLAIM, AND THE ONE THIS BRANCH RESTS ON: with every stat
	// recorded, asking the pipeline answers exactly what the attribute holds.

	for (const FCase& Case : Cases)
	{
		const float FromAttribute = Character.Read(Case.Attribute);
		const float Asked = Asking->StatForSkill(
			FName(Case.Stat), FGameplayTagContainer(), Sentinel);

		if (!TestNotEqual(
				*FString::Printf(
					TEXT("%s has recorded inputs, so this is not the fallback"),
					Case.Stat),
				Asked, Sentinel))
		{
			continue;
		}

		TestEqual(
			*FString::Printf(
				TEXT("%s asked for through the pipeline answers exactly what "
					 "its attribute holds, for a character with no scoped row"),
				Case.Stat),
			Asked, FromAttribute, 0.001f);
	}

	// WITHOUT THIS THE LOOP ABOVE PASSES HAVING CHECKED NOTHING, which is the
	// fault it exists to rule out in the first place.
	//
	// THE NUMBER RISES AS EACH STAT IS WIRED, and it is written out rather than
	// left to `UE_ARRAY_COUNT` alone so that emptying the list is a failure
	// rather than a silent pass. Counted from the list above, not incremented.
	TestEqual(TEXT("every stat listed was checked"),
			  static_cast<int32>(UE_ARRAY_COUNT(Cases)), 15);
	return true;
}

// --------------------------------------------------------------------------
// Maximum health read live. Issue #1815: "Each active minion reduces your
// maximum HP by 3%-6%".
//
// WHY THESE GO THROUGH `ApplyTo` AND REAL MINIONS. `ApplyTo` folds each stat
// with no character state, so a row sized by the minions held is worth nothing
// in the fold. What makes it reach the attribute is the refresh, called from
// `ApplyTo` and from the regeneration step, and a test that wrote the attribute
// by hand would pass with either call deleted.
// --------------------------------------------------------------------------

namespace CataclysmPlayerClassStatsTest
{
	/** "Each active minion reduces your maximum HP by 5%", as a stat row. */
	static FCataclysmStatModifier FivePercentPerMinion()
	{
		FCataclysmStatModifier PerMinion;
		PerMinion.Bucket = ECataclysmStatBucket::More;
		PerMinion.Source = ECataclysmModifierSource::Enchantment;
		PerMinion.Value = -5.0f;
		PerMinion.Scale = ECataclysmStatScale::PerMinionHeld;
		PerMinion.ScaleStep = 1.0f;
		return PerMinion;
	}

	/** A real imp, named, commanded by `Summoner`. */
	static ACataclysmMinion* SpawnImp(AActor* Summoner, float Metres)
	{
		return ACataclysmMinion::Spawn(
			Summoner, FVector(Metres * 100.0f, 0.0f, 0.0f), /*Lifetime=*/60.0f,
			/*bBurns=*/false, /*TypeName=*/TEXT("Imp"));
	}
}

CATACLYSM_TEST(FCataclysmMinionsLowerLiveMaximumHealth,
	"Cataclysm.PlayerStats.EachMinionLowersTheLiveMaximumHealthAndHealthIsLeftAlone")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	// A WORLD THAT HAS BEGUN PLAY, because a named minion writes its own health
	// when it spawns.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedCharacter Character(World);
	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	Modifiers.Add(FName(TEXT("max_health")), {FivePercentPerMinion()});
	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, /*Level=*/20, &Modifiers);

	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const FGameplayAttribute Health = UCataclysmVitalAttributeSet::GetHealthAttribute();
	const float Unreduced = Character.Read(MaxHealth);
	if (!TestTrue(TEXT("the character has a maximum to reduce"), Unreduced > 1.0f))
	{
		return false;
	}

	ACataclysmMinion* First = SpawnImp(Character.Actor, 3.0f);
	ACataclysmMinion* Second = SpawnImp(Character.Actor, 4.0f);
	if (!TestNotNull(TEXT("a first imp"), First) || !TestNotNull(TEXT("and a second"), Second)
		|| !TestEqual(TEXT("both are commanded by the character"),
					  UCataclysmCommand::ThingsCommandedBy(Character.Actor).Num(), 2))
	{
		return false;
	}

	// HALF HEALTH, SO A CLAMP OR A REFILL WOULD EACH SHOW.
	Character.AbilitySystem->SetNumericAttributeBase(Health, Unreduced * 0.5f);

	Character.AbilitySystem->RefreshLiveMaximumHealth();
	TestEqual(TEXT("two minions take 10% off the maximum"),
		Character.Read(MaxHealth), Unreduced * 0.90f, 0.5f);

	First->Destroy();
	if (!TestEqual(TEXT("one imp is left"),
				   UCataclysmCommand::ThingsCommandedBy(Character.Actor).Num(), 1))
	{
		return false;
	}
	Character.AbilitySystem->RefreshLiveMaximumHealth();
	TestEqual(TEXT("one minion takes 5% off"),
		Character.Read(MaxHealth), Unreduced * 0.95f, 0.5f);
	TestEqual(TEXT("and health moved with neither change"),
		Character.Read(Health), Unreduced * 0.5f, 0.5f);

	return true;
}

CATACLYSM_TEST(FCataclysmStatRefreshKeepsLiveMaximumHealth,
	"Cataclysm.PlayerStats.AStatRefreshWithMinionsOutKeepsTheLiveMaximumHealth")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedCharacter Character(World);
	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	Modifiers.Add(FName(TEXT("max_health")), {FivePercentPerMinion()});
	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, /*Level=*/20, &Modifiers);

	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Unreduced = Character.Read(MaxHealth);

	if (!TestNotNull(TEXT("a first imp"), SpawnImp(Character.Actor, 3.0f))
		|| !TestNotNull(TEXT("and a second"), SpawnImp(Character.Actor, 4.0f)))
	{
		return false;
	}

	// A HELMET SWAPPED WITH THE MINIONS OUT, and no refresh called by hand. The
	// fold alone would put the unreduced maximum back.
	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, /*Level=*/20, &Modifiers,
		ECataclysmPoolFill::LeaveAsTheyAre);
	TestEqual(TEXT("the stat refresh leaves the two minions' reduction on"),
		Character.Read(MaxHealth), Unreduced * 0.90f, 0.5f);

	return true;
}

CATACLYSM_TEST(FCataclysmWaterToBloodSurvivesLiveMaximumHealth,
	"Cataclysm.PlayerStats.WaterToBloodSurvivesTheLiveMaximumHealthRefresh")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedCharacter Character(World);
	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();

	// THE MAXIMUM WITH NOTHING, THEN WITH THE MANA CONVERTED, so the converted
	// amount is measured rather than assumed.
	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table, UCataclysmClassStats::DefaultClassName, 20);
	const float Plain = Character.Read(MaxHealth);

	FCataclysmStatModifier Traded;
	Traded.Bucket = ECataclysmStatBucket::Flat;
	Traded.Source = ECataclysmModifierSource::PassiveKeystone;
	Traded.Value = 1.0f;

	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	Modifiers.Add(FName(UCataclysmSkillTemplate::ManaPoolBecomesHealthStat), {Traded});
	Modifiers.Add(FName(TEXT("max_health")), {FivePercentPerMinion()});
	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, /*Level=*/20, &Modifiers);
	const float WithConverted = Character.Read(MaxHealth);
	if (!TestTrue(TEXT("the mana was converted into health"), WithConverted > Plain + 1.0f))
	{
		return false;
	}

	// THE REFRESH RAN AT THE END OF `ApplyTo` WITH NO MINION OUT, and left the
	// converted health where it was. Losing it is the failure this exists for.
	Character.AbilitySystem->RefreshLiveMaximumHealth();
	TestEqual(TEXT("with no minion, the refresh keeps the converted health"),
		Character.Read(MaxHealth), WithConverted, 0.5f);

	if (!TestNotNull(TEXT("a first imp"), SpawnImp(Character.Actor, 3.0f))
		|| !TestNotNull(TEXT("and a second"), SpawnImp(Character.Actor, 4.0f)))
	{
		return false;
	}
	Character.AbilitySystem->RefreshLiveMaximumHealth();
	TestEqual(TEXT("and two minions reduce the stat, not the converted mana"),
		Character.Read(MaxHealth), Plain * 0.90f + (WithConverted - Plain), 0.5f);

	return true;
}

CATACLYSM_TEST(FCataclysmRegenerationStepRefreshesMaximumHealth,
	"Cataclysm.PlayerStats.TheRegenerationStepAppliesTheLiveMaximumHealth")
{
	using namespace CataclysmPlayerClassStatsTest;

	const UDataTable* Table = UCataclysmPlayerClassStats::LoadTable();
	if (!Table)
	{
		AddError(TEXT("DT_ClassStats does not exist."));
		return false;
	}

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	const FScopedCharacter Character(World);
	TMap<FName, TArray<FCataclysmStatModifier>> Modifiers;
	Modifiers.Add(FName(TEXT("max_health")), {FivePercentPerMinion()});
	UCataclysmPlayerClassStats::ApplyTo(
		Character.AbilitySystem, Table,
		UCataclysmClassStats::DefaultClassName, /*Level=*/20, &Modifiers);

	const FGameplayAttribute MaxHealth = UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	const float Unreduced = Character.Read(MaxHealth);

	if (!TestNotNull(TEXT("an imp"), SpawnImp(Character.Actor, 3.0f)))
	{
		return false;
	}
	TestEqual(TEXT("nothing has asked yet, so the maximum has not moved"),
		Character.Read(MaxHealth), Unreduced, 0.5f);

	// ONE STEP, CALLED AS `ACataclysmCharacterBase::RegenerationStep` CALLS IT.
	UCataclysmRegeneration::ApplyStep(Character.Actor, UCataclysmRegeneration::StepSeconds,
									  /*SecondsSinceLastDamage=*/100.0f);
	TestEqual(TEXT("one regeneration step applies the minion's reduction"),
		Character.Read(MaxHealth), Unreduced * 0.95f, 0.5f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
