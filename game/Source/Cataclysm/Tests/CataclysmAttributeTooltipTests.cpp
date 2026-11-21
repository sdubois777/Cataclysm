// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmClassStats.h"
#include "Character/CataclysmPlayerClassStats.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/CataclysmCharacterSheetLayout.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * WHAT HOVERING AN ATTRIBUTE ON THE CHARACTER SHEET SAYS. Issue #2815, the owner, 2026-11-21: "do attributes actually do
 * anything? I've forgotten what each one is supposed to do so I can't tell in game since they don't have descriptions".
 *
 * THE CHECKS: the per-point line names every row of `DT_Attributes` and `DT_MinionScaling` for its attribute, with the
 * row's figure; each "Now" line's two figures are the sheet's figure for this character and for the same character
 * without the attribute; a stat with nothing to increase is said to gain nothing; and, the control, an attribute at
 * nought states the per-point line and no "Now" figure.
 *
 * THE CHARACTERS ARE BUILT AS `Cataclysm.Attributes.` BUILDS THEM: a bare actor with the five attribute sets, given a
 * class line, a level and points through `UCataclysmPlayerClassStats::MergeAttributeBases` and `ApplyTo`.
 */
namespace CataclysmAttributeTooltipTest
{
	struct FScopedCharacter
	{
		explicit FScopedCharacter(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);
			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmVitalAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmCombatAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmClassResourceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmResistanceAttributeSet>(Actor));
			AbilitySystem->AddAttributeSetSubobject(NewObject<UCataclysmPrimaryAttributeSet>(Actor));
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
		}

		~FScopedCharacter()
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		AActor* Actor = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;
	};

	using FGear = TMap<FName, TArray<FCataclysmStatModifier>>;

	void Apply(const FScopedCharacter& Character, const TCHAR* ClassName, const FCataclysmAttributePoints& Points,
			   const FGear& Gear = FGear())
	{
		TMap<FName, float> Bases;
		UCataclysmPlayerClassStats::MergeAttributeBases(Points, Bases);
		UCataclysmPlayerClassStats::ApplyTo(Character.AbilitySystem, UCataclysmPlayerClassStats::LoadTable(), ClassName,
											/*Level=*/10, Gear.IsEmpty() ? nullptr : &Gear,
											ECataclysmPoolFill::LeaveAsTheyAre, &Bases);
	}

	FCataclysmStatModifier Modifier(ECataclysmStatBucket Bucket, float Value)
	{
		FCataclysmStatModifier Out;
		Out.Bucket = Bucket;
		Out.Source = ECataclysmModifierSource::GearAffix;
		Out.Value = Value;
		return Out;
	}

	TArray<FString> LinesOf(const FString& Text)
	{
		TArray<FString> Lines;
		Text.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);
		return Lines;
	}

	/** The line opening with `Start`, or empty. */
	FString LineOpening(const TArray<FString>& Lines, const FString& Start)
	{
		const FString* Found = Lines.FindByPredicate([&Start](const FString& Line) { return Line.StartsWith(Start); });
		return Found ? *Found : FString();
	}

	/** "Energy shield: <without> to <with> (<increase>% increased from Spirit)", both figures as the sheet writes them. */
	FString ExpectedNowLine(const FString& Stat, const UAbilitySystemComponent* Without,
							const UAbilitySystemComponent* With, const TCHAR* Increase, const TCHAR* Attribute)
	{
		return FString::Printf(TEXT("%s: %s to %s (%s increased from %s)"),
							   *UCataclysmCharacterSheetLayout::NameFor(Stat),
							   *UCataclysmCharacterSheetLayout::FigureText(
								   Stat, UCataclysmCharacterSheetLayout::FigureFor(Stat, Without)),
							   *UCataclysmCharacterSheetLayout::FigureText(
								   Stat, UCataclysmCharacterSheetLayout::FigureFor(Stat, With)),
							   Increase, Attribute);
	}

	/** A row's figure as the tooltip writes it, for the row check: "2", "0.5", "0.01". */
	FString FigureWords(float Percent)
	{
		FString Text = FString::Printf(TEXT("%.2f"), Percent);
		while (Text.Contains(TEXT(".")) && (Text.EndsWith(TEXT("0")) || Text.EndsWith(TEXT("."))))
		{
			Text.LeftChopInline(1);
		}
		return Text + TEXT("%");
	}
}

// --------------------------------------------------------------------------------------------------------------------
// A1. Each attribute's per-point line names every row the game applies for it, with the row's figure.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributeTooltipNamesEveryRowTest,
	"Cataclysm.AttributeTooltip.EachAttributeNamesEveryRowItApplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributeTooltipNamesEveryRowTest::RunTest(const FString&)
{
	using namespace CataclysmAttributeTooltipTest;
	const UDataTable* Attributes = UCataclysmPlayerClassStats::LoadAttributeTable();
	const UDataTable* Minions = UCataclysmPlayerClassStats::LoadMinionScalingTable();
	if (!TestNotNull(TEXT("set-up: DT_Attributes"), Attributes) || !TestNotNull(TEXT("set-up: DT_MinionScaling"), Minions))
	{
		return false;
	}

	int32 RowsChecked = 0;
	for (const TPair<FName, uint8*>& Pair : Attributes->GetRowMap())
	{
		const auto* Row = reinterpret_cast<const FCataclysmAttributeEffectRow*>(Pair.Value);
		const FString Text = UCataclysmCharacterSheetLayout::AttributePerPointText(Row->Attribute);
		const FString Part = FString::Printf(TEXT("%s increased %s"), *FigureWords(Row->PercentPerPoint),
											 *UCataclysmCharacterSheetLayout::AttributeStatWords(Row->Stat));
		TestTrue(FString::Printf(TEXT("%s's line names row %s: \"%s\" in \"%s\""), *Row->Attribute,
								 *Pair.Key.ToString(), *Part, *Text),
				 Text.Contains(Part));
		++RowsChecked;
	}
	for (const TPair<FName, uint8*>& Pair : Minions->GetRowMap())
	{
		const auto* Row = reinterpret_cast<const FCataclysmMinionScalingRow*>(Pair.Value);
		const FString Text = UCataclysmCharacterSheetLayout::AttributePerPointText(Row->Attribute);
		const FString Part = FString::Printf(TEXT("%s increased damage"), *FigureWords(Row->PercentPerPoint));
		TestTrue(FString::Printf(TEXT("%s's line names minion row %s: \"%s\" in \"%s\""), *Row->Attribute,
								 *Pair.Key.ToString(), *Part, *Text),
				 Text.Contains(Part));
		++RowsChecked;
	}
	TestTrue(TEXT("CONTROL: rows were read from both tables"), RowsChecked >= 17 + 2);

	// THE WORDS THE OWNER WAS SHOWN, for three attributes: a minion family, the smallest figure and three stats.
	TestEqual(TEXT("Spirit, as proposed"), UCataclysmCharacterSheetLayout::AttributePerPointText(TEXT("spirit")),
			  FString(TEXT("Each point: 2% increased energy shield and 1% increased energy shield regeneration. Your "
						   "summoned creatures deal 1% increased damage.")));
	TestEqual(TEXT("Luck, as proposed"), UCataclysmCharacterSheetLayout::AttributePerPointText(TEXT("luck")),
			  FString(TEXT("Each point: 0.01% increased magic find and 1% increased loot quantity.")));
	TestEqual(TEXT("Efficacy, as proposed"), UCataclysmCharacterSheetLayout::AttributePerPointText(TEXT("efficacy")),
			  FString(TEXT("Each point: 1% increased cooldown reduction, 2% increased area of effect and 1% increased "
						   "damage over time tick rate.")));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A2. "Now": each stat's two figures are the sheet's, for this character and for the same character without the
// attribute; a stat with nothing to increase is said to gain nothing; items raising the attribute are named.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributeTooltipNowTest,
	"Cataclysm.AttributeTooltip.TheNowFiguresAreTheSheetsWithAndWithoutTheAttribute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributeTooltipNowTest::RunTest(const FString&)
{
	using namespace CataclysmAttributeTooltipTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// A RITUALIST WITH 8 SPIRIT, AND ITS ITEMS' 30% INCREASED ENERGY SHIELD, so the "from" figure is not the base: it is
	// the same character with the items and without the Spirit.
	FGear Shield;
	Shield.Add(TEXT("max_energy_shield"), {Modifier(ECataclysmStatBucket::Increased, 30.0f)});
	FCataclysmAttributePoints Eight;
	Eight.Spirit = 8;
	FScopedCharacter Ritualist(World);
	FScopedCharacter Plain(World);
	Apply(Ritualist, TEXT("Ritualist"), Eight, Shield);
	Apply(Plain, TEXT("Ritualist"), FCataclysmAttributePoints(), Shield);
	const TArray<FString> Spirit = LinesOf(
		UCataclysmCharacterSheetLayout::AttributeTooltipFor(TEXT("spirit"), Ritualist.AbilitySystem, 8));
	TestEqual(TEXT("the heading names the 8 Spirit"), LineOpening(Spirit, TEXT("Now")), FString(TEXT("Now, at 8 Spirit:")));
	TestEqual(TEXT("energy shield: the sheet's figure without Spirit, to the sheet's figure with it"),
			  LineOpening(Spirit, TEXT("Energy shield:")),
			  ExpectedNowLine(TEXT("max_energy_shield"), Plain.AbilitySystem, Ritualist.AbilitySystem, TEXT("16%"),
							  TEXT("Spirit")));
	TestEqual(TEXT("energy shield regeneration, the same"), LineOpening(Spirit, TEXT("Energy shield regeneration:")),
			  ExpectedNowLine(TEXT("energy_shield_regen"), Plain.AbilitySystem, Ritualist.AbilitySystem, TEXT("8%"),
							  TEXT("Spirit")));
	TestEqual(TEXT("and the summoned creatures' increase"), LineOpening(Spirit, TEXT("Summoned creatures:")),
			  FString(TEXT("Summoned creatures: 8% increased damage from Spirit")));
	TestTrue(TEXT("CONTROL: the two energy shield figures differ"),
			 UCataclysmCharacterSheetLayout::FigureFor(TEXT("max_energy_shield"), Ritualist.AbilitySystem)
				 > UCataclysmCharacterSheetLayout::FigureFor(TEXT("max_energy_shield"), Plain.AbilitySystem));

	// A RAVAGER WITH 8 SPIRIT AND 8 CONSTITUTION: no energy shield and no block chance to increase, armour to increase.
	FCataclysmAttributePoints Ravaging;
	Ravaging.Spirit = 8;
	Ravaging.Constitution = 8;
	FCataclysmAttributePoints RavagingWithoutConstitution;
	RavagingWithoutConstitution.Spirit = 8;
	FScopedCharacter Ravager(World);
	FScopedCharacter RavagerWithout(World);
	Apply(Ravager, TEXT("Ravager"), Ravaging);
	Apply(RavagerWithout, TEXT("Ravager"), RavagingWithoutConstitution);
	const TArray<FString> RavagerSpirit = LinesOf(
		UCataclysmCharacterSheetLayout::AttributeTooltipFor(TEXT("spirit"), Ravager.AbilitySystem, 8));
	TestEqual(TEXT("a Ravager's energy shield: Spirit does nothing to it yet"), LineOpening(RavagerSpirit, TEXT("Energy shield:")),
			  FString(TEXT("Energy shield: you have none, from your class or your items, so Spirit does nothing to it yet.")));
	const TArray<FString> RavagerConstitution = LinesOf(
		UCataclysmCharacterSheetLayout::AttributeTooltipFor(TEXT("constitution"), Ravager.AbilitySystem, 8));
	TestEqual(TEXT("a Ravager's armour: the sheet's figures without and with Constitution"),
			  LineOpening(RavagerConstitution, TEXT("Armour:")),
			  ExpectedNowLine(TEXT("armor"), RavagerWithout.AbilitySystem, Ravager.AbilitySystem, TEXT("16%"),
							  TEXT("Constitution")));
	TestEqual(TEXT("a Ravager's block chance: Constitution does nothing to it yet"),
			  LineOpening(RavagerConstitution, TEXT("Block chance:")),
			  FString(TEXT("Block chance: you have none, from your class or your items, so Constitution does nothing to it yet.")));

	// COOLDOWN REDUCTION, which the sheet shows as a share and not as the sum it holds: 12 flat from items, 8 Efficacy.
	FGear Haste;
	Haste.Add(TEXT("cooldown_reduction"), {Modifier(ECataclysmStatBucket::Flat, 12.0f)});
	FCataclysmAttributePoints Effective;
	Effective.Efficacy = 8;
	FScopedCharacter Quick(World);
	FScopedCharacter QuickWithout(World);
	Apply(Quick, TEXT("Ravager"), Effective, Haste);
	Apply(QuickWithout, TEXT("Ravager"), FCataclysmAttributePoints(), Haste);
	TestEqual(TEXT("cooldown reduction: the sheet's shares without and with Efficacy"),
			  LineOpening(LinesOf(UCataclysmCharacterSheetLayout::AttributeTooltipFor(TEXT("efficacy"),
																						Quick.AbilitySystem, 8)),
						  TEXT("Cooldown reduction:")),
			  ExpectedNowLine(TEXT("cooldown_reduction"), QuickWithout.AbilitySystem, Quick.AbilitySystem, TEXT("8%"),
							  TEXT("Efficacy")));

	// ITEMS RAISING THE ATTRIBUTE ITSELF: 12.5% increased Spirit on 8 points is 9.
	FGear MoreSpirit;
	MoreSpirit.Add(TEXT("spirit"), {Modifier(ECataclysmStatBucket::Increased, 12.5f)});
	FScopedCharacter Raised(World);
	Apply(Raised, TEXT("Ritualist"), Eight, MoreSpirit);
	TestEqual(TEXT("the heading names the points spent and the points from items"),
			  LineOpening(LinesOf(UCataclysmCharacterSheetLayout::AttributeTooltipFor(TEXT("spirit"),
																						Raised.AbilitySystem, 8)),
						  TEXT("Now")),
			  FString(TEXT("Now, at 9 Spirit (8 spent, 1 from items):")));

	AddInfo(FString::Printf(TEXT("ATTRIBUTE TOOLTIP: %s"),
							*FString::Join(Spirit, TEXT(" | "))));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// A3. CONTROL: an attribute at nought states what a point gives, and no "Now" figure.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmAttributeTooltipAtNoughtTest,
	"Cataclysm.AttributeTooltip.AnAttributeAtNoughtStatesThePerPointLineAndNoNowFigure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmAttributeTooltipAtNoughtTest::RunTest(const FString&)
{
	using namespace CataclysmAttributeTooltipTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCharacter Ritualist(World);
	Apply(Ritualist, TEXT("Ritualist"), FCataclysmAttributePoints());
	const TArray<FString> Lines =
		LinesOf(UCataclysmCharacterSheetLayout::AttributeTooltipFor(TEXT("spirit"), Ritualist.AbilitySystem, 0));
	if (!TestEqual(TEXT("four lines: the name, per point, the sentence, and Now"), Lines.Num(), 4))
	{
		return false;
	}
	TestEqual(TEXT("the name"), Lines[0], FString(TEXT("Spirit")));
	TestEqual(TEXT("what a point gives"), Lines[1], UCataclysmCharacterSheetLayout::AttributePerPointText(TEXT("spirit")));
	TestEqual(TEXT("how an increase works"), Lines[2],
			  FString(UCataclysmCharacterSheetLayout::AttributeIncreaseSentence));
	TestEqual(TEXT("and no figure now"), Lines[3], FString(TEXT("Now: no Spirit.")));
	TestEqual(TEXT("CONTROL: with no ability system, the same"),
			  LinesOf(UCataclysmCharacterSheetLayout::AttributeTooltipFor(TEXT("spirit"), nullptr, 0)).Last(),
			  FString(TEXT("Now: no Spirit.")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
