// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAllResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "Interface/CataclysmCharacterSheetLayout.h"

/**
 * Tests for the character sheet, issues #1233 and #50.
 *
 * WHAT THEY CANNOT CHECK, AND IT IS THE THING THAT MATTERS MOST. The automation
 * test command in `tools/unreal_build.py` passes `-nullrhi`, so nothing is drawn
 * and no test here can say whether the sheet is legible, whether 51 rows fit the
 * window, or whether a player can find the row they came for. **Somebody has to
 * look.** These cover what the sheet says: which stats are on it, what each row
 * reads, and the two figures that are wrong if they are copied straight out of
 * the attribute -- resistance and cooldown reduction.
 *
 * WHICH IS WHY THEY ARE ABOUT `UCataclysmCharacterSheetLayout` AND NOT THE
 * WIDGET. A widget in a headless test has no Widget Blueprint, so every one of
 * its `BindWidget` properties is null and it builds no rows at all --
 * `UCataclysmEmpireMapWidget` and `UCataclysmPassiveTreeWidget` are in exactly
 * the same position. Everything decidable without a screen is in the layout
 * class so that it can be covered.
 */

namespace CataclysmCharacterSheetTest
{
	/** An actor carrying all six attribute sets a sheet reads. */
	struct FScopedSheetCharacter
	{
		explicit FScopedSheetCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();

			// Raw pointers, not TObjectPtr: AddAttributeSetSubobject is a
			// template and deduces T from the argument, so a TObjectPtr would
			// deduce the wrapper rather than the attribute set. The same note is
			// on the fixture in CataclysmAttributeSetTests.cpp.
			UCataclysmVitalAttributeSet* NewVitals =
				NewObject<UCataclysmVitalAttributeSet>(Actor);
			UCataclysmPrimaryAttributeSet* NewPrimary =
				NewObject<UCataclysmPrimaryAttributeSet>(Actor);
			UCataclysmCombatAttributeSet* NewCombat =
				NewObject<UCataclysmCombatAttributeSet>(Actor);
			UCataclysmResistanceAttributeSet* NewResistances =
				NewObject<UCataclysmResistanceAttributeSet>(Actor);
			UCataclysmAllResistanceAttributeSet* NewAllResistance =
				NewObject<UCataclysmAllResistanceAttributeSet>(Actor);
			UCataclysmClassResourceAttributeSet* NewResource =
				NewObject<UCataclysmClassResourceAttributeSet>(Actor);

			AbilitySystem->AddAttributeSetSubobject(NewVitals);
			AbilitySystem->AddAttributeSetSubobject(NewPrimary);
			AbilitySystem->AddAttributeSetSubobject(NewCombat);
			AbilitySystem->AddAttributeSetSubobject(NewResistances);
			AbilitySystem->AddAttributeSetSubobject(NewAllResistance);
			AbilitySystem->AddAttributeSetSubobject(NewResource);

			Vitals = NewVitals;
			Combat = NewCombat;

			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedSheetCharacter()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Add to one attribute, the way a gear modifier would. */
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
		TObjectPtr<UCataclysmCombatAttributeSet> Combat = nullptr;
	};

	UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game,
								   /*bInformEngineOfWorld=*/false);
	}

	/** One row, found by the stat that produced it. */
	FCataclysmStatLine Row(const FString& Stat,
						   const UAbilitySystemComponent* ASC, int32 Tier)
	{
		return UCataclysmCharacterSheetLayout::LineFor(Stat, ASC, Tier);
	}
}

#define CATACLYSM_SHEET_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Which stats are on it
// ---------------------------------------------------------------------------

CATACLYSM_SHEET_TEST(FCataclysmSheetHoldsTheModelStatsTest,
	"Cataclysm.CharacterSheet.TheSheetIsTheModelsFortySixStats")
{
	using namespace CataclysmCharacterSheetTest;

	// FORTY-SIX IS NOT A NUMBER THIS SCREEN CHOSE. It is what
	// `Cataclysm.Attributes.CharacterSheetIsComplete` derives by counting the
	// attribute sets and subtracting the ones deliberately kept off, and what
	// `STAT_GROUPS` in `sim/cataclysm_sim/character.py` holds. A sheet showing a
	// different number of stats from the model that balances the game would be
	// showing the player a game nobody tuned.
	TestEqual(TEXT("the sheet holds 46 stats"),
			  UCataclysmCharacterSheetLayout::SheetStatCount(), 46);

	TestEqual(TEXT("in five groups"),
			  UCataclysmCharacterSheetLayout::Groups().Num(), 5);

	// NO STAT IN TWO GROUPS, which would make the count right and the sheet
	// wrong: a stat listed twice and one missing add up to 46.
	TSet<FString> Seen;
	for (const FString& Stat : UCataclysmCharacterSheetLayout::SheetStats())
	{
		TestFalse(FString::Printf(TEXT("%s appears once"), *Stat),
				  Seen.Contains(Stat));
		Seen.Add(Stat);
	}

	// EVERY ONE HAS AN ATTRIBUTE BEHIND IT. A stat name the class tables do not
	// know reads as zero for ever and nothing says so, which is the failure this
	// catches: the row would appear, look plausible, and never move.
	const TMap<FString, FGameplayAttribute>& Map =
		UCataclysmPlayerClassStats::StatToAttribute();

	for (const FString& Stat : UCataclysmCharacterSheetLayout::SheetStats())
	{
		const FGameplayAttribute* Found = Map.Find(Stat);
		if (!TestNotNull(
				*FString::Printf(TEXT("%s is a stat the game supplies"), *Stat),
				Found))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s names a real attribute"), *Stat),
				 Found->IsValid());
	}

	// AND EVERY ONE HAS A NAME A PLAYER CAN READ. `NameFor` answers the raw stat
	// name when it has nothing better, so a stat added to the list and not named
	// shows up here rather than on the screen as "energy_shield_leech".
	//
	// CASE SENSITIVELY, AND `TestNotEqual` IS NOT. `FString`'s own comparison
	// ignores case, so "Evasion" and "evasion" are equal to it and the two stats
	// whose player-facing name is just the capitalised word would read as
	// unnamed. That is not a hypothetical: this test was written with
	// `TestNotEqual` and failed on exactly `evasion` and `retaliation`.
	for (const FString& Stat : UCataclysmCharacterSheetLayout::SheetStats())
	{
		const FString Name = UCataclysmCharacterSheetLayout::NameFor(Stat);

		TestFalse(
			*FString::Printf(TEXT("%s is named for a player"), *Stat),
			Name.Equals(Stat, ESearchCase::CaseSensitive));

		// AND THE NAME IS WORDS RATHER THAN AN IDENTIFIER. A fall-through would
		// also be caught by this, and so would a name copied from the stat and
		// merely capitalised.
		TestFalse(
			*FString::Printf(TEXT("%s is named in words, not in code"), *Stat),
			Name.Contains(TEXT("_")));
	}

	return true;
}

// ---------------------------------------------------------------------------
// The rows that would be wrong if the attribute were copied straight out
// ---------------------------------------------------------------------------

CATACLYSM_SHEET_TEST(FCataclysmSheetResistanceRowTest,
	"Cataclysm.CharacterSheet.AResistanceRowSaysWhatAHitMeets")
{
	using namespace CataclysmCharacterSheetTest;

	UWorld* World = MakeWorld();
	{
		FScopedSheetCharacter Character(World);

		// 45 OF ITS OWN AND 10 THAT APPLIES TO EVERY TYPE, so the row has to add
		// them: All Resistance is not one of the eight and a per-type row that
		// left it out would disagree with what the player takes.
		Character.Add(UCataclysmResistanceAttributeSet::GetWarResistanceAttribute(),
					  45.0f);
		Character.Add(
			UCataclysmAllResistanceAttributeSet::GetAllResistanceAttribute(),
			10.0f);

		const UAbilitySystemComponent* ASC = Character.AbilitySystem;

		// BELOW THE FIRST PENALISED TIER THE PENALTY IS NOTHING, and the row
		// still has to state all three figures rather than only the first.
		const FCataclysmStatLine Early = Row(TEXT("resistance_war"), ASC, 2);

		TestEqual(TEXT("the figure is what the character holds, both sources"),
				  Early.Value, FString(TEXT("55%")));
		TestTrue(TEXT("and the row says what a hit meets"),
				 Early.Note.Contains(TEXT("A hit meets 55%")));

		// AT TIER 8 THE PENALTY IS 75, so 55 held becomes -20 met. This is the
		// case issue #1233 exists for: a sheet showing only the 55 would tell a
		// player they were nearly at the cap while every hit landed in full.
		const FCataclysmStatLine Late = Row(TEXT("resistance_war"), ASC, 8);

		TestEqual(TEXT("what the character holds does not change with the tier"),
				  Late.Value, FString(TEXT("55%")));
		TestTrue(TEXT("the tier's penalty is named"),
				 Late.Note.Contains(TEXT("takes 75% off")));
		TestTrue(TEXT("and so is what is left"),
				 Late.Note.Contains(TEXT("leaving -20%")));
		TestTrue(TEXT("and so is what a hit meets"),
				 Late.Note.Contains(TEXT("A hit meets -20%")));

		// THE TWO ROWS DIFFER, which is the whole point of showing the tier.
		TestNotEqual(TEXT("the row changes with the difficulty tier"),
					 Early.Note, Late.Note);

		// OVER THE CAP, A HIT STILL MEETS ONLY THE CAP. 100 held at tier 2 is 70
		// met, and a sheet that printed 100 would be promising 30 points of
		// mitigation the game does not give.
		Character.Add(
			UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute(),
			120.0f);
		const FCataclysmStatLine Overcapped = Row(TEXT("resistance_death"), ASC, 2);

		TestEqual(TEXT("the figure is still what the character holds"),
				  Overcapped.Value, FString(TEXT("130%")));
		TestTrue(TEXT("but a hit meets the cap"),
				 Overcapped.Note.Contains(
					 FString::Printf(TEXT("A hit meets %.0f%%"),
									 UCataclysmDamageCalculation::ResistanceCap)));
	}
	World->DestroyWorld(false);

	return true;
}

CATACLYSM_SHEET_TEST(FCataclysmSheetCooldownRowTest,
	"Cataclysm.CharacterSheet.CooldownReductionIsTheShownPercentageNotTheSum")
{
	using namespace CataclysmCharacterSheetTest;

	UWorld* World = MakeWorld();
	{
		FScopedSheetCharacter Character(World);

		// THE ATTRIBUTE HOLDS THE ACCUMULATED SUM OF INCREASES, not a
		// percentage. Its own header says a skill's cooldown is its base divided
		// by one plus this. A sum of 1.0 halves every cooldown, which is a 50%
		// reduction; printing the attribute would say "1%" and printing it times
		// a hundred would say "100%", which is a cooldown of zero and cannot
		// happen at all.
		Character.Add(UCataclysmCombatAttributeSet::GetCooldownReductionAttribute(),
					  1.0f);

		const FCataclysmStatLine Line =
			Row(TEXT("cooldown_reduction"), Character.AbilitySystem, 1);

		TestEqual(TEXT("a sum of one is shown as a 50% reduction"),
				  Line.Value, FString(TEXT("50%")));
		TestNotEqual(TEXT("and not as the raw sum"),
					 Line.Value, FString(TEXT("1%")));
		TestNotEqual(TEXT("and not as the sum times a hundred"),
					 Line.Value, FString(TEXT("100%")));
		TestTrue(TEXT("the row says it can never reach a hundred"),
				 Line.Note.Contains(TEXT("never reaches 100%")));
	}
	World->DestroyWorld(false);

	return true;
}

CATACLYSM_SHEET_TEST(FCataclysmSheetArmourRowTest,
	"Cataclysm.CharacterSheet.TheArmourRowSaysWhatItStopsAtThisTier")
{
	using namespace CataclysmCharacterSheetTest;

	UWorld* World = MakeWorld();
	{
		FScopedSheetCharacter Character(World);

		// 800 ARMOUR IS THE CONSTANT ITSELF, so at tier 1 it stops exactly half
		// a hit: armour / (armour + 800 x tier). Hand-worked so the figure is
		// not read back out of the code that produced it.
		Character.Add(UCataclysmCombatAttributeSet::GetArmorAttribute(),
					  UCataclysmDamageCalculation::ArmorConstantPerTier);

		const UAbilitySystemComponent* ASC = Character.AbilitySystem;

		const FCataclysmStatLine First = Row(TEXT("armor"), ASC, 1);

		TestEqual(TEXT("the figure is the armour itself, not a percentage"),
				  First.Value, FString(TEXT("800")));
		TestTrue(TEXT("and the row says it stops half a hit at tier 1"),
				 First.Note.Contains(TEXT("Stops 50% of a hit at difficulty tier 1")));

		// THE SAME ARMOUR IS WORTH LESS DEEPER IN, which is the fact a player
		// cannot read off an armour figure and can read off this row.
		const FCataclysmStatLine Third = Row(TEXT("armor"), ASC, 3);

		TestTrue(TEXT("at tier 3 the same armour stops a quarter"),
				 Third.Note.Contains(TEXT("Stops 25% of a hit at difficulty tier 3")));
		TestNotEqual(TEXT("so the row changes with the tier"),
					 First.Note, Third.Note);

		// AND THE CAP IS NAMED WHEN IT BINDS. Armour never removes more than
		// 75%, so a test asserting a heavily armoured character takes almost
		// nothing would be asserting a state this game cannot reach.
		Character.Add(UCataclysmCombatAttributeSet::GetArmorAttribute(),
					  40000.0f);
		const FCataclysmStatLine Capped = Row(TEXT("armor"), ASC, 1);

		TestTrue(TEXT("the row names the cap once armour reaches it"),
				 Capped.Note.Contains(TEXT("cap")));
	}
	World->DestroyWorld(false);

	return true;
}

CATACLYSM_SHEET_TEST(FCataclysmSheetPoolRowTest,
	"Cataclysm.CharacterSheet.APoolRowShowsWhatIsInItAsWellAsItsSize")
{
	using namespace CataclysmCharacterSheetTest;

	UWorld* World = MakeWorld();
	{
		FScopedSheetCharacter Character(World);
		const UAbilitySystemComponent* ASC = Character.AbilitySystem;

		Character.Add(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(),
					  600.0f);

		const FCataclysmStatLine Full = Row(TEXT("max_health"), ASC, 1);

		TestTrue(TEXT("the row pairs two figures"),
				 Full.Value.Contains(TEXT(" of ")));
		TestTrue(TEXT("and one of them is the maximum"),
				 Full.Value.EndsWith(FString::Printf(
					 TEXT(" of %.0f"), Character.Vitals->GetMaxHealth())));

		// LOSING HEALTH MOVES THE ROW. `max_health` is the sheet stat and its
		// attribute is the MAXIMUM, so a row built from the stat map alone would
		// read the same after a hit as before it -- which is the one thing a
		// player looks at a health row to find out.
		Character.Add(UCataclysmVitalAttributeSet::GetHealthAttribute(), -50.0f);

		const FCataclysmStatLine Hurt = Row(TEXT("max_health"), ASC, 1);

		TestNotEqual(TEXT("the row changes when health is lost"),
					 Full.Value, Hurt.Value);
		TestTrue(TEXT("and the maximum is unchanged"),
				 Hurt.Value.EndsWith(FString::Printf(
					 TEXT(" of %.0f"), Character.Vitals->GetMaxHealth())));
	}
	World->DestroyWorld(false);

	return true;
}

// ---------------------------------------------------------------------------
// The words around the figures
// ---------------------------------------------------------------------------

CATACLYSM_SHEET_TEST(FCataclysmSheetFiguresTest,
	"Cataclysm.CharacterSheet.AFigureCarriesADecimalOnlyWhenItNeedsOne")
{
	// A HEALTH POOL OF 606 READS WORSE AS "606.0", and a regeneration of 1.5
	// reads as "1" without a decimal place, so one number of decimal places
	// cannot serve both.
	TestEqual(TEXT("a whole number carries none"),
			  UCataclysmCharacterSheetLayout::Number(606.0f),
			  FString(TEXT("606")));
	TestEqual(TEXT("a fraction carries one"),
			  UCataclysmCharacterSheetLayout::Number(1.5f),
			  FString(TEXT("1.5")));
	TestEqual(TEXT("and zero is zero"),
			  UCataclysmCharacterSheetLayout::Number(0.0f),
			  FString(TEXT("0")));
	TestEqual(TEXT("a percentage carries its sign"),
			  UCataclysmCharacterSheetLayout::Percent(20.0f),
			  FString(TEXT("20%")));

	return true;
}

CATACLYSM_SHEET_TEST(FCataclysmSheetUnspentPointsTest,
	"Cataclysm.CharacterSheet.PointsToSpendReadAsASentence")
{
	TestEqual(TEXT("none"),
			  UCataclysmCharacterSheetLayout::UnspentPointsText(0),
			  FString(TEXT("No points to spend.")));
	TestEqual(TEXT("one is singular"),
			  UCataclysmCharacterSheetLayout::UnspentPointsText(1),
			  FString(TEXT("1 point to spend.")));
	TestEqual(TEXT("more than one is plural"),
			  UCataclysmCharacterSheetLayout::UnspentPointsText(3),
			  FString(TEXT("3 points to spend.")));

	// A NEGATIVE COUNT CANNOT HAPPEN AND IS STILL NOT A CRASH. `AttributePointsUnspent`
	// subtracts spent from available, and a save written before a level was lost
	// is the shape that would produce one.
	TestEqual(TEXT("a negative count reads as none"),
			  UCataclysmCharacterSheetLayout::UnspentPointsText(-2),
			  FString(TEXT("No points to spend.")));

	return true;
}

CATACLYSM_SHEET_TEST(FCataclysmSheetGroupsTest,
	"Cataclysm.CharacterSheet.EveryGroupIsHeadedAndHoldsItsOwnStats")
{
	using namespace CataclysmCharacterSheetTest;

	int32 Counted = 0;

	for (const ECataclysmSheetGroup Group :
		 UCataclysmCharacterSheetLayout::Groups())
	{
		const FString Heading =
			UCataclysmCharacterSheetLayout::HeadingFor(Group);

		TestFalse(TEXT("every group has a heading"), Heading.IsEmpty());
		TestFalse(TEXT("and a name the model would recognise"),
				  UCataclysmCharacterSheetLayout::ModelGroupName(Group).IsEmpty());

		const TArray<FString>& Stats =
			UCataclysmCharacterSheetLayout::StatsIn(Group);

		TestTrue(*FString::Printf(TEXT("%s holds at least one stat"), *Heading),
				 Stats.Num() > 0);

		Counted += Stats.Num();

		// EVERY ROW IN THE GROUP IS NAMED AND CARRIES A FIGURE, with no
		// character at all. A screen opened before a character exists shows the
		// shape of the sheet rather than an empty panel, and that is the case
		// this checks.
		for (const FCataclysmStatLine& Line :
			 UCataclysmCharacterSheetLayout::LinesIn(Group, nullptr, 1))
		{
			TestFalse(TEXT("a row with no character is still named"),
					  Line.Name.IsEmpty());
			TestFalse(TEXT("and still carries a figure"), Line.Value.IsEmpty());
		}
	}

	TestEqual(TEXT("the five groups hold every sheet stat between them"),
			  Counted, UCataclysmCharacterSheetLayout::SheetStatCount());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
