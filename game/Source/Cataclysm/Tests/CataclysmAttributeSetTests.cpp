// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/**
 * Tests for the five attribute sets.
 *
 * The rules being checked are design decisions, not implementation details, and
 * several of them are easy to get backwards. The most important is that a SOFT
 * cap must not be enforced as a clamp: clamping resistance at 70 would silently
 * delete over-capping, which the design relies on against enemy penetration.
 */

namespace CataclysmAttributeTest
{
	/** An actor carrying all five attribute sets. */
	struct FScopedFullCharacter
	{
		explicit FScopedFullCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject is a
			// template and deduces T from the argument, so a TObjectPtr would
			// deduce the wrapper rather than the attribute set.
			UCataclysmVitalAttributeSet* NewVitals = NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmPrimaryAttributeSet* NewPrimary = NewObject<UCataclysmPrimaryAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat = NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResist = NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource = NewObject<UCataclysmClassResourceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewPrimary);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResist);
			AbilitySystem->AddAttributeSetSubobject(NewResource);

			Vitals = NewVitals;
			Primary = NewPrimary;
			Combat = NewCombat;
			Resistances = NewResist;
			ClassResource = NewResource;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedFullCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		void Add(const FGameplayAttribute& Attribute, float Magnitude) const
		{
			UGameplayEffect* Effect = NewObject<UGameplayEffect>(
				GetTransientPackage(), FName(TEXT("TestEffect")));
			Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

			const int32 Index = Effect->Modifiers.Num();
			Effect->Modifiers.SetNum(Index + 1);
			FGameplayModifierInfo& Info = Effect->Modifiers[Index];
			Info.Attribute = Attribute;
			Info.ModifierOp = EGameplayModOp::Additive;
			Info.ModifierMagnitude = FScalableFloat(Magnitude);

			AbilitySystem->ApplyGameplayEffectToSelf(
				Effect, 1.0f, AbilitySystem->MakeEffectContext());
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmVitalAttributeSet> Vitals = nullptr;
		TObjectPtr<UCataclysmPrimaryAttributeSet> Primary = nullptr;
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
		TObjectPtr<UCataclysmResistanceAttributeSet> Resistances = nullptr;
		TObjectPtr<UCataclysmClassResourceAttributeSet> ClassResource = nullptr;
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
// The sheet is complete
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmSheetIsCompleteTest,
	"Cataclysm.Attributes.CharacterSheetIsComplete")
{
	// 45 stats on the character sheet, plus the four current values that pair
	// with a maximum, plus the damage meta attribute.
	const int32 Vitals = UCataclysmVitalAttributeSet::GetAllAttributes().Num();
	const int32 Primary = UCataclysmPrimaryAttributeSet::GetAllAttributes().Num();
	const int32 Combat = UCataclysmCombatAttributeSet::GetAllAttributes().Num();
	const int32 Resist = UCataclysmResistanceAttributeSet::GetAllAttributes().Num();
	const int32 Resource = UCataclysmClassResourceAttributeSet::GetAllAttributes().Num();

	/**
	 * Attributes that exist but are NOT on the character sheet.
	 *
	 * ATTACK DAMAGE is one. `sim/cataclysm_sim/affixes.py` names it explicitly as
	 * an off-sheet stat, because it belongs to the equipped weapon rather than to
	 * the character: a Greataxe supplies 144 and a Fist 30, and the character has
	 * no value of their own. It has to be an attribute anyway, because every
	 * skill's damage is a percentage of it and two affixes add to it, but it does
	 * not make the sheet longer.
	 *
	 * MULTIPLICATIVE DAMAGE REDUCTION is the second, added under issue #665. It is
	 * off the sheet for a different reason: it is not a stat at all but the
	 * product of however many "more" sources a character has, and the model treats
	 * it the same way. `sim/cataclysm_sim/damage.py` carries it as a list on the
	 * `Defender` being hit rather than as an entry in `DEFAULT_STAT_LINE`, because
	 * a "more" multiplier in this project is a modifier rather than a stat with a
	 * baseline, affixes and scaling of its own. The additive pool it sits beside,
	 * `DamageReduction`, IS a sheet stat and has all three.
	 *
	 * A CHARACTER'S OWN MAXIMUM CRITICAL STRIKE CHANCE is the third, added under
	 * issue #680. It is a ceiling rather than a stat: it does not measure
	 * anything the character can do, it bounds another figure that does. The
	 * model agrees and keeps it off the sheet too -- `sim/cataclysm_sim/
	 * character.py` carries it as a field on `Gear` rather than an entry in
	 * `DEFAULT_STAT_LINE`, because no affix grants it, nothing scales it, and it
	 * has no baseline of its own beyond the shared cap.
	 *
	 * THE THREE FERVOUR RATES are the fourth group, added under issue #954, and
	 * they are off the sheet for the same reason the maximum critical strike
	 * chance is: no affix grants one, nothing scales one, and none has a
	 * baseline of its own. Every class starts all three at zero and a passive
	 * tree's generator node is the only thing that supplies them, so they are
	 * not a line every character has. `sim/cataclysm_sim/character.py` does not
	 * model Fervour at all, and its `DEFAULT_STAT_LINE` is checked against
	 * `ALL_STATS` there, so putting them on the sheet would need three entries
	 * in a model that has nothing to do with them.
	 *
	 * So the sheet stays at 46. The combat set grew by two and the class
	 * THE ADDED HEALTH COST is the fifth, added under issue #970, and it is off
	 * the sheet for the same reasons again: no affix grants it, nothing scales
	 * it, it has no baseline of its own, and the Masochist's Deeper Cuts node is
	 * the only thing that supplies it. `sim/cataclysm_sim/character.py` does not
	 * model a health cost at all.
	 *
	 * THE COOLDOWN SKIP CHANCE is the sixth, added under issue #973, and it is
	 * off the sheet for the same reasons: no affix grants it, nothing scales it,
	 * it has no baseline of its own, and one passive node is its only source.
	 * Its attribute holds zero even for a character holding that node, because
	 * the node's bonus carries a health condition.
	 *
	 * DAMAGE TAKEN AND DAMAGE OVER TIME TAKEN are the seventh and eighth, added
	 * under issue #1026, and they meet the same rule a fourth time: no affix in
	 * `game/Data/Affixes.csv` grants either, nothing scales either, no class
	 * differs on either, and three Masochist passive nodes are their only source.
	 *
	 * THEY HAVE A BASELINE AND IT IS STILL NOT A CLASS ONE, which is the only way
	 * these two differ from the six above. Both start at 100, because 100 is the
	 * identity for a multiplier and an `increased` row with a base of zero is
	 * worth nothing. That base comes from
	 * `UCataclysmPlayerClassStats::EngineSuppliedBases` rather than from
	 * `game/Data/ClassStats.csv`, so no class line names them and the rule above
	 * still holds. `docs/DECISIONS.md` records that they belong on the sheet the
	 * day an affix grants one or a class differs on one.
	 *
	 * THE CHANCE A MELEE CRITICAL STRIKE APPLIES BLEEDING is the ninth, added
	 * under issue #1032, and it meets the rule for the fifth time: no affix
	 * grants it, nothing scales it, it has no baseline of its own, and the
	 * Masochist's Mutilation Mastery node is its only source.
	 *
	 * AND WHETHER DAMAGE OVER TIME DEALS THE CHARACTER NOTHING AT ALL is the
	 * tenth, added under issue #1039, and it meets the rule for the sixth time:
	 * no affix grants it, nothing scales it, it has no baseline of its own, and
	 * the Masochist's Vessel Unbroken capstone option is its only source. It is
	 * a FLAG and not a reduction, because a Less multiplier is floored at -99
	 * and "no damage at all" is not 99% less.
	 *
	 * HOW FAR RETALIATION REACHES, AND WHETHER IT LEECHES, are the eleventh and
	 * twelfth, added under issues #1047 and #1048, and they meet the rule for the
	 * seventh and eighth times: no affix grants either, nothing scales either,
	 * neither has a baseline of its own, and one capstone option is the only
	 * source of each -- The First Vow's Reprisal Wave and The Second Vow's
	 * Feeding Wound. The first is the ONLY STAT HERE MEASURED IN A DISTANCE and
	 * is in metres, which the header on it explains; the second is a flag.
	 *
	 * WHAT SHARE OF ITS MISSING HEALTH ONE NOVA DEALS is the thirteenth,
	 * added under issue #1050, and it meets the rule for the ninth time: no
	 * affix grants it, nothing scales it, it has no baseline of its own, and
	 * the Masochist's Unstable Aura is its only source. That node was the
	 * last one in its tree that did nothing and was neither blocked on other
	 * work nor waiting on a design answer.
	 *
	 * HOW LONG A LASTING HARMFUL EFFECT ON THE CHARACTER RUNS is the
	 * fourteenth, added under issue #1033, and it meets the rule for the tenth
	 * time: no affix grants it, nothing scales it, no class differs on it, and
	 * two nodes of one tree are its only sources. It is the THIRD stat here
	 * whose base is neither a class line nor a modifier, after the two damage
	 * taken stats, and it arrives the same way they do, from
	 * `UCataclysmPlayerClassStats::EngineSuppliedBases`.
	 *
	 * HOW MUCH LONGER A DEBUFF THE CHARACTER'S AURA APPLIES LASTS, AND THE
	 * CHANCE A NEARBY ENEMY CATCHES ONE, are the fifteenth and sixteenth,
	 * added under issues #1057 and #1058, and they meet the rule for the
	 * eleventh and twelfth times: no affix grants either, nothing scales
	 * either, neither has a baseline of its own, and one node of one tree is
	 * the only source of each -- Beacon of Despair and Contagious Torment.
	 * Both are ALSO how the code knows the character holds the node at all,
	 * because neither can be held at zero points.
	 *
	 * THE CHANCE A DYING ENEMY'S DEBUFFS PASS ON, AND THE INCREASED DAMAGE
	 * DEALT TO AN ENEMY SHARING A DEBUFF, are the seventeenth and eighteenth,
	 * added under issues #1060 and #1061, and they finish the Masochist tree.
	 * They meet the rule for the thirteenth and fourteenth times. The second
	 * is the SECOND STAT IN THE GAME DECIDED BY THE TARGET, after the eight
	 * increased-damage-against-a-damage-type stats, and it could not be a
	 * sheet stat even if an affix granted it: a sheet has no target in hand.
	 *
	 * So the sheet stays at 46. The combat set grew by eleven and the class
	 * resource set by seven, which is what this count exists to keep honest.
	 * A stat a passive node supplies and no player reads is not a sheet stat.
	 */
	constexpr int32 OffSheetCombatStats = 16;

	/**
	 * How far healing may take the character. Issue #988.
	 *
	 * THE FIRST VITAL ATTRIBUTE THAT IS NOT A SHEET STAT, which is why this
	 * constant did not exist before. No player reads a healing ceiling as a
	 * stat and no class line names it; the Masochist's Point of No Return
	 * keystone is its only source, as a flat modifier.
	 */
	constexpr int32 OffSheetVitalStats = 1;

	/**
	 * The three Fervour rates, and the two added health costs. See the note
	 * above.
	 *
	 * FIVE SINCE ISSUE #986 ADDED A SECOND ADDED HEALTH COST, measured against
	 * CURRENT health where the first is measured against MAXIMUM health. The
	 * Masochist's Exsanguinate keystone is its only source. It is off the sheet
	 * for the same reason the first one is: no player reads it as a stat and no
	 * class line names it.
	 *
	 * NINE SINCE THE LAST TWO BLOOD TITHE NODES. Issues #995 and #997 added the
	 * seconds a further payment pushes an outstanding debt out, and the flag for
	 * a debt that is never taken on a timer. Both are off the sheet for the same
	 * reason as everything else in this count: one passive node supplies each,
	 * no class line names either, and no player reads either as a stat. The
	 * sheet total therefore does not move.
	 *
	 * ELEVEN SINCE THE THREE NODES THAT CHANGE HOW FERVOUR MOVES. Issues #1006
	 * and #1008 added the flag for healing that does not remove Fervour, and the
	 * Fervour that arrives every second from nothing having happened. Off the
	 * sheet for the same reasons again, so the sheet total still does not move.
	 *
	 * THIRTEEN SINCE THE BREAKING POINT. Issue #985 added the flag for damage
	 * arriving as Bleeding after a drop below half health, and how many seconds
	 * one turn of that lasts. Off the sheet for the same reasons a third time:
	 * one passive node supplies both, no class line names either, and no player
	 * reads either as a stat on the character sheet. The sheet total still does
	 * not move, which is the point of counting them separately.
	 *
	 * FOURTEEN SINCE THE FINAL VOW'S SECOND OPTION. Issue #1029 added the flag
	 * for the class resource pool having no maximum at all. Off the sheet for the
	 * same reasons a fourth time: one capstone option supplies it, no class line
	 * names it, and no player reads it as a stat. What a player reads is the bar
	 * itself, and `MaxClassResource` beside it is untouched by that option --
	 * this flag stops the pool being CLAMPED to the maximum and does not change
	 * the maximum.
	 *
	 * AND THIRTEEN AGAIN, BECAUSE THAT OPTION NO LONGER EXISTS. Issue #1031. The
	 * project owner judged the twelve Masochist capstone options poor on
	 * 2026-08-27 and had all twelve rewritten as pure upgrades with no
	 * drawbacks, so Apotheosis and its flag went with them. THIS IS THE FIRST
	 * TIME THIS COUNT HAS FALLEN, and the direction matters: every earlier entry
	 * above argues why the sheet total of 46 does not move when a stat is added,
	 * and the same argument is what says it does not move when one is removed.
	 *
	 * FIFTEEN SINCE THE LAST DROP. Issue #1051. The Final Vow's first option
	 * added a flag saying the character's skills cost no health, and the
	 * Fervour every cast grants. Off the sheet for the same reasons a fifth
	 * time: one capstone option supplies both, no class line names either,
	 * and no player reads either as a stat. What a player reads is the cost
	 * on the skill and the bar itself.
	 */
	constexpr int32 OffSheetResourceStats = 15;

	TestEqual(TEXT("Eight primary attributes"), Primary, 8);

	// EXACTLY EIGHT, AND NO NINTH. An enemy's single all-damage resistance is a
	// SEPARATE attribute set, UCataclysmAllResistanceAttributeSet, checked below.
	// Putting it here instead would give every player a resistance no player can
	// have, which is why the project owner refused that shape on 2026-08-12.
	// Issue #486.
	TestEqual(TEXT("Eight resistances, one per damage type"), Resist, 8);
	TestEqual(TEXT("and one all-damage resistance, in a set of its own"),
		UCataclysmAllResistanceAttributeSet::GetAllAttributes().Num(), 1);
	// Twenty-five since the eight increased-damage-against-a-type stats were
	// added for #213. Seventeen before that. Twenty-seven since damage over time
	// damage and damage over time duration joined damage over time frequency
	// for #205. Twenty-eight since armour penetration was added for #520 -- a
	// SECOND penetration stat, cutting into armour where the first cuts into
	// resistance.
	// THE MESSAGE COUNTS THE STATS RATHER THAN SPELLING A NUMBER, since issue
	// #1050. It read "plus four off the sheet" while eleven were off it. A
	// stale message does not fail anything, which is exactly why it stayed
	// wrong: the assertion beside it was right the whole time.
	TestEqual(*FString::Printf(
			  TEXT("Twenty-eight combat and utility stats, plus %d off the "
				   "sheet"), OffSheetCombatStats),
		Combat, 28 + OffSheetCombatStats);
	// Thirteen since mana leech and energy shield leech were added for #214.
	// Fourteen since the healing ceiling reduction joined them for #988, which
	// is off the sheet and counted apart for that reason.
	TestEqual(TEXT("Fourteen vital attributes including the damage meta"),
		Vitals, 13 + OffSheetVitalStats);
	// Five since the three Fervour rates were added for #954: the pool, its
	// maximum, and the three rates that move it. Six since the added health cost
	// joined them for #970, which is not a rate and is counted apart from the
	// three in GetRateAttributes for that reason. Seven since #986 added the
	// second added health cost, the one measured against current health. Nine
	// since #991 added the share of a cost that is taken later and the health
	// a character owes; neither is a sheet stat. Eleven since #995 and #997
	// added the seconds a further payment pushes a debt out and the flag for a
	// debt that is never taken on a timer; neither is a sheet stat either.
	// Thirteen since #1006 and #1008 added the flag for healing that does not
	// remove Fervour and the Fervour that arrives every second; neither is a
	// sheet stat either.
	// COUNTED RATHER THAN SPELLED, for the reason given above the combat set.
	TestEqual(*FString::Printf(
			  TEXT("The pool, its maximum, and %d stats off the sheet"),
			  OffSheetResourceStats),
		Resource, 2 + OffSheetResourceStats);

	// The 46 sheet stats: 3 maxima + 6 recovery from vitals, 28 combat,
	// 8 resistances, 1 class resource maximum. The six recovery stats are the
	// three regenerations and the three leeches. The 28 combat stats include the
	// eight increased-damage-against-a-type figures, which are the offensive
	// mirror of the eight resistances, the three damage over time levers, and the
	// two penetrations -- one for resistance and one for armour.
	//
	// `sim/cataclysm_sim/character.py` states the same count and
	// `tools/tests/test_leech.py` compares the two, which is the check that
	// really runs: continuous integration compiles no C++.
	TestEqual(TEXT("Forty-six stats on the character sheet"),
		(Vitals - 3 - 1 - OffSheetVitalStats)
			+ (Combat - OffSheetCombatStats) + Resist
			+ (Resource - 1 - OffSheetResourceStats), 46);
	return true;
}

CATACLYSM_TEST(FCataclysmNoDuplicateAttributesTest,
	"Cataclysm.Attributes.NoAttributeIsDeclaredTwice")
{
	TArray<FGameplayAttribute> All;
	All.Append(UCataclysmVitalAttributeSet::GetAllAttributes());
	All.Append(UCataclysmPrimaryAttributeSet::GetAllAttributes());
	All.Append(UCataclysmCombatAttributeSet::GetAllAttributes());
	All.Append(UCataclysmResistanceAttributeSet::GetAllAttributes());
	All.Append(UCataclysmClassResourceAttributeSet::GetAllAttributes());

	TSet<FString> Names;
	for (const FGameplayAttribute& Attribute : All)
	{
		Names.Add(Attribute.GetName());
	}
	TestEqual(TEXT("Every attribute name is unique"), Names.Num(), All.Num());
	return true;
}

// --------------------------------------------------------------------------
// Hard caps clamp, soft caps do not
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmCritChanceHardCapTest,
	"Cataclysm.Attributes.CritChanceIsHardCappedAtOneHundred")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);
		Fixture.Add(UCataclysmCombatAttributeSet::GetCritChanceAttribute(), 500.0f);

		TestEqual(TEXT("Crit chance clamps at 100"),
			Fixture.Combat->GetCritChance(), 100.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmEvasionSoftCapTest,
	"Cataclysm.Attributes.EvasionSoftCapIsNotEnforced")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// The design states evasion caps at 60% and that gear enchantments may
		// exceed it. Clamping here would delete the over-cap.
		Fixture.Add(UCataclysmCombatAttributeSet::GetEvasionAttribute(), 85.0f);

		TestTrue(TEXT("Evasion is allowed above its soft cap"),
			Fixture.Combat->GetEvasion() > UCataclysmCombatAttributeSet::EvasionSoftCap);
		TestEqual(TEXT("Evasion keeps the value it was given"),
			Fixture.Combat->GetEvasion(), 85.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmResistanceOverCapTest,
	"Cataclysm.Attributes.ResistanceCanExceedSeventyPercent")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Over-capping is the point: enemy penetration reduces effective
		// resistance, so headroom above the cap is what keeps a character at the
		// cap in practice. Clamping the attribute would remove that entirely.
		Fixture.Add(UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(), 150.0f);

		TestEqual(TEXT("Raw resistance keeps its over-capped value"),
			Fixture.Resistances->GetDemonicResistance(), 150.0f);

		// The 70% figure caps what resistance is WORTH, not what it may be, and
		// it lives in the damage calculation. This used to call a second copy
		// of it on the attribute set, which took no penetration argument and so
		// could not show the property the over-capping is FOR. Issue #232.
		TestEqual(TEXT("Effective resistance is capped at seventy"),
			UCataclysmDamageCalculation::EffectiveResistance(150.0f, 0.0f), 70.0f);
		TestEqual(TEXT("Effective resistance below the cap is unchanged"),
			UCataclysmDamageCalculation::EffectiveResistance(45.0f, 0.0f), 45.0f);

		// What the headroom buys, which is the reason to over-cap at all.
		// Penetration is subtracted before the cap, so 150 resistance still
		// sits at the cap against 30 penetration where exactly 70 would drop
		// to 40.
		TestEqual(TEXT("Over-capped resistance holds the cap against penetration"),
			UCataclysmDamageCalculation::EffectiveResistance(150.0f, 30.0f), 70.0f);
		TestEqual(TEXT("Resistance at exactly the cap does not"),
			UCataclysmDamageCalculation::EffectiveResistance(70.0f, 30.0f), 40.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmNegativeResistanceTest,
	"Cataclysm.Attributes.ResistanceCanGoNegative")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Several enchantments reduce resistance. Taking extra damage from a
		// damage type is a real drawback, not an error state.
		Fixture.Add(UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute(), -40.0f);

		TestEqual(TEXT("Resistance is allowed below zero"),
			Fixture.Resistances->GetVoidResistance(), -40.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmBlockChanceUncappedTest,
	"Cataclysm.Attributes.BlockChanceHasNoCap")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// A block removes 50% of a hit rather than preventing it, so 100% block
		// chance is 50% damage reduction, not immunity. There is nothing to cap.
		Fixture.Add(UCataclysmCombatAttributeSet::GetBlockChanceAttribute(), 100.0f);

		TestEqual(TEXT("Block chance reaches one hundred"),
			Fixture.Combat->GetBlockChance(), 100.0f);
		TestEqual(TEXT("A block removes half a hit, not all of it"),
			UCataclysmCombatAttributeSet::BlockDamageReduction, 50.0f);
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// Vitals
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmManaClampTest,
	"Cataclysm.Attributes.ManaClampsToItsMaximum")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		Fixture.Add(UCataclysmVitalAttributeSet::GetManaAttribute(), 5000.0f);
		TestEqual(TEXT("Mana caps at its maximum"),
			Fixture.Vitals->GetMana(), Fixture.Vitals->GetMaxMana());

		Fixture.Add(UCataclysmVitalAttributeSet::GetManaAttribute(), -5000.0f);
		TestEqual(TEXT("Mana floors at zero"), Fixture.Vitals->GetMana(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmZeroEnergyShieldTest,
	"Cataclysm.Attributes.AClassMayHaveNoEnergyShield")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Two of the three Demonic classes have no energy shield at all, so a
		// maximum of zero is a design position rather than an error. If this
		// floored at one the way MaxHealth does, those classes would gain a
		// phantom point of shield.
		TestEqual(TEXT("Maximum energy shield starts at zero"),
			Fixture.Vitals->GetMaxEnergyShield(), 0.0f);

		// The starting value alone does not test the floor: the constructor's
		// Init call writes the attribute directly and never passes through
		// PreAttributeChange. Only a gameplay effect exercises the clamp, so the
		// maximum is driven up and back down to zero here.
		Fixture.Add(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), 500.0f);
		TestEqual(TEXT("Maximum energy shield can be raised"),
			Fixture.Vitals->GetMaxEnergyShield(), 500.0f);

		Fixture.Add(UCataclysmVitalAttributeSet::GetMaxEnergyShieldAttribute(), -500.0f);
		TestEqual(TEXT("Maximum energy shield returns to exactly zero, not one"),
			Fixture.Vitals->GetMaxEnergyShield(), 0.0f);

		Fixture.Add(UCataclysmVitalAttributeSet::GetEnergyShieldAttribute(), 500.0f);
		TestEqual(TEXT("Energy shield cannot rise above a maximum of zero"),
			Fixture.Vitals->GetEnergyShield(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmMaxHealthFloorsAtOneTest,
	"Cataclysm.Attributes.MaxHealthFloorsAtOneNotZero")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		Fixture.Add(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), -9999.0f);

		// Unlike every other maximum, this one cannot be zero: it would collapse
		// the health clamp to a single point and make every
		// percentage-of-maximum calculation divide by zero.
		TestTrue(TEXT("Maximum health stays at or above one"),
			Fixture.Vitals->GetMaxHealth() >= 1.0f);
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// Cooldown reduction divides rather than subtracting
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmCooldownDivisionTest,
	"Cataclysm.Attributes.CooldownReductionDividesAndNeverReachesZero")
{
	using FCombat = UCataclysmCombatAttributeSet;

	// The design document's worked example: a character shown at 25% reduction
	// turns a four second skill into a three second one.
	TestEqual(TEXT("A third of increases shows as 25 percent"),
		FMath::RoundToInt(FCombat::DisplayedCooldownReduction(1.0f / 3.0f)), 25);
	TestTrue(TEXT("Four seconds becomes three"),
		FMath::IsNearlyEqual(FCombat::FinalCooldown(4.0f, 1.0f / 3.0f), 3.0f, 0.001f));

	// One hundred points of Efficacy at one per cent each halves every cooldown.
	TestTrue(TEXT("One hundred Efficacy halves a cooldown"),
		FMath::IsNearlyEqual(FCombat::FinalCooldown(4.0f, 1.0f), 2.0f, 0.001f));

	// And it can never reach zero, which is why no cap is needed.
	for (const float Increases : { 1.0f, 10.0f, 1000.0f, 100000.0f })
	{
		TestTrue(TEXT("Cooldown stays above zero"),
			FCombat::FinalCooldown(4.0f, Increases) > 0.0f);
		TestTrue(TEXT("Displayed reduction stays below one hundred"),
			FCombat::DisplayedCooldownReduction(Increases) < 100.0f);
	}
	return true;
}

// --------------------------------------------------------------------------
// Class resource
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmClassResourceClampTest,
	"Cataclysm.Attributes.ClassResourceClampsToItsPool")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Most class resources build from nothing during a fight.
		TestEqual(TEXT("Class resource starts empty"),
			Fixture.ClassResource->GetClassResource(), 0.0f);

		Fixture.Add(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 500.0f);
		TestEqual(TEXT("Class resource caps at its pool"),
			Fixture.ClassResource->GetClassResource(),
			Fixture.ClassResource->GetMaxClassResource());
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// Attributes that scale a skill or a weapon start empty
// --------------------------------------------------------------------------

CATACLYSM_TEST(FCataclysmExternallyBasedStatsTest,
	"Cataclysm.Attributes.SkillAndWeaponBasedStatsStartAtZero")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// A character has no critical strike chance in the abstract: the base
		// comes from the skill being used. Attack speed's comes from the weapon.
		TestEqual(TEXT("Crit chance starts at zero, supplied by the skill"),
			Fixture.Combat->GetCritChance(), 0.0f);
		TestEqual(TEXT("Attack speed starts at zero, supplied by the weapon"),
			Fixture.Combat->GetAttackSpeed(), 0.0f);

		// Area of effect and the three damage over time levers are percentages
		// of what the skill or the effect does, so their baseline is 100 rather
		// than zero. A zero here would leave Efficacy nothing to scale.
		TestEqual(TEXT("Area of effect baselines at one hundred per cent"),
			Fixture.Combat->GetAreaOfEffect(), 100.0f);

		// The three levers multiply each other, so a zero on any one of them
		// takes a damage over time build's whole output to zero rather than
		// only its own third of it. Issue #205.
		TestEqual(TEXT("Damage over time damage baselines at one hundred"),
			Fixture.Combat->GetDotDamage(), 100.0f);
		TestEqual(TEXT("Damage over time frequency baselines at one hundred"),
			Fixture.Combat->GetDotFrequency(), 100.0f);
		TestEqual(TEXT("Damage over time duration baselines at one hundred"),
			Fixture.Combat->GetDotDuration(), 100.0f);

		// Loot quantity is a percentage of what the dungeon would otherwise
		// drop, so it is the same shape and needs the same baseline. Issue
		// #243: it started at zero, and because every source of loot quantity
		// is an increase rather than a flat grant, it stayed at zero however
		// much a player spent on it. Nothing errored, which is why it survived.
		TestEqual(TEXT("Loot quantity baselines at one hundred per cent"),
			Fixture.Combat->GetLootQuantity(), 100.0f);

		// Magic find is deliberately NOT the same shape. It is an added
		// percentage rather than a percentage of something, and it has a flat
		// source in the Flat Magic Find affix, so zero is correct here.
		TestEqual(TEXT("Magic find starts at zero, supplied flat by an affix"),
			Fixture.Combat->GetMagicFind(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

CATACLYSM_TEST(FCataclysmPrimaryAttributesStartEmptyTest,
	"Cataclysm.Attributes.PrimaryAttributesStartAtZeroAndCannotGoNegative")
{
	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		for (const FGameplayAttribute& Attribute :
			 UCataclysmPrimaryAttributeSet::GetAllAttributes())
		{
			TestEqual(*FString::Printf(TEXT("%s starts at zero"), *Attribute.GetName()),
				Attribute.GetNumericValue(Fixture.Primary), 0.0f);
		}

		Fixture.Add(UCataclysmPrimaryAttributeSet::GetVitalityAttribute(), -50.0f);
		TestEqual(TEXT("An attribute cannot be spent below zero"),
			Fixture.Primary->GetVitality(), 0.0f);
	}
	World->DestroyWorld(false);
	return true;
}

// --------------------------------------------------------------------------
// An attribute is a whole number of points
// --------------------------------------------------------------------------
//
// Each of the eight attributes has one affix and it is a percentage increase, so
// 33 Spirit with a top-tier +12% affix reaches 36.96. The project owner decided
// on 2026-08-05 that the value rounds to the nearest whole number, and that it
// rounds in the maths rather than only on the character screen: a screen reading
// 37 while the calculation keeps 36.96 is what makes a player report a bug.
// Issue #225. `attribute_points` in `sim/cataclysm_sim/character.py` is the
// matching function in the simulation.

CATACLYSM_TEST(FCataclysmAttributeRoundingTest,
	"Cataclysm.Attributes.AnAttributeIsAWholeNumberOfPoints")
{
	// Pure arithmetic first, so a failure here says the rule is wrong rather
	// than that the ability system did not call it.
	TestEqual(TEXT("36.96 points round to 37"),
		UCataclysmPrimaryAttributeSet::RoundedPoints(36.96f), 37.0f);
	TestEqual(TEXT("36.4 points round to 36"),
		UCataclysmPrimaryAttributeSet::RoundedPoints(36.4f), 36.0f);
	TestEqual(TEXT("A half rounds up, so 36.5 points become 37"),
		UCataclysmPrimaryAttributeSet::RoundedPoints(36.5f), 37.0f);
	TestEqual(TEXT("4.48 points round to 4"),
		UCataclysmPrimaryAttributeSet::RoundedPoints(4.48f), 4.0f);
	TestEqual(TEXT("A whole number is left alone"),
		UCataclysmPrimaryAttributeSet::RoundedPoints(33.0f), 33.0f);

	UWorld* World = CataclysmAttributeTest::MakeWorld();
	{
		const CataclysmAttributeTest::FScopedFullCharacter Fixture(World);

		// Then through the ability system, because a rule nothing calls is not a
		// rule. 36.96 is the worked example from the issue: 33 Spirit raised 12%.
		Fixture.Add(UCataclysmPrimaryAttributeSet::GetSpiritAttribute(), 36.96f);
		TestEqual(TEXT("A fractional Spirit becomes a whole Spirit"),
			Fixture.Primary->GetSpirit(), 37.0f);
	}
	World->DestroyWorld(false);
	return true;
}

#undef CATACLYSM_TEST

#endif // WITH_AUTOMATION_TESTS
