// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
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
		{TEXT("fervour_per_second"),
		 TEXT("the Masochist's Low Life node, as a flat modifier")},

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
	// gets its value. There are 52 entries and most are several lines of
	// explanation, so a stale one costs a reader real time and no test run
	// mentions it.
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

		// IT HAS TO HAVE AN ATTRIBUTE, or `ApplyTo` never resolves it: the loop
		// there is over `StatToAttribute` rather than over these bases, so a stat
		// missing from that map is dropped before this base is ever consulted.
		const FGameplayAttribute* Attribute =
			UCataclysmPlayerClassStats::StatToAttribute().Find(Stat);
		if (!TestNotNull(*FString::Printf(
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
			Character.Read(*Attribute), Pair.Value, 0.001f);
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

#endif // WITH_AUTOMATION_TESTS
