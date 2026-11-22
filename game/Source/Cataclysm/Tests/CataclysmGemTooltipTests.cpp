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
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interface/CataclysmItemComparison.h"
#include "Interface/CataclysmItemTooltip.h"
#include "Items/CataclysmGems.h"
#include "Items/CataclysmInventoryComponent.h"
#include "Misc/ScopeExit.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * A GEM'S TOOLTIP, READ FOR THE CHARACTER LOOKING AT IT. Issue #2835, the coordinating session's plan of 2026-11-22.
 *
 * Four gems increase a stat no class has a base of (block chance, evasion, cooldown reduction, magic find); an increase
 * multiplies the base plus the flat amount, so with neither it does nothing. Slice G2 of #46 gave their tooltip a line
 * true for every character ("No class has a base evasion, ..."), `Cataclysm.GemStacks.` holds it. Read for a character,
 * the line says whether the gem does anything for THAT character: "You have no evasion, ..." when its base plus flat
 * amount is nothing, and no line at all when its gear gives some (ruling Q1: the effect line already says what it does).
 *
 * WRITTEN BEFORE THE CHANGE, WITHOUT A BUILD. Every figure below is reasoned from the code.
 *
 * THE CHARACTERS ARE BUILT AS `Cataclysm.AttributeTooltip.` BUILDS THEM: a bare actor with the five attribute sets,
 * given a class line, a level and, for gear, a map of modifiers, through `UCataclysmPlayerClassStats::ApplyTo`. A
 * Ravager, because no class has a base of any of the four stats and the Ravager is the class that test reads for them.
 */
namespace CataclysmGemTooltipTest
{
	const FName Wraith(TEXT("Gem_Of_The_Wraith"));		// increases evasion
	const FName Bulwark(TEXT("Gem_Of_The_Bulwark"));	// increases block chance

	const TCHAR* const ForEveryone = TEXT("An increase multiplies what you already have. No class has a base evasion, so "
										  "this gem does nothing until your gear gives some.");
	const TCHAR* const ForYou = TEXT("An increase multiplies what you already have. You have no evasion, from your class "
									 "or your items, so this gem does nothing for you until your gear gives some.");

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

	/** A Ravager at level 10 with no attribute points spent, and `Gear` as its items' modifiers. */
	void Apply(const FScopedCharacter& Character, const FGear& Gear = FGear())
	{
		TMap<FName, float> Bases;
		UCataclysmPlayerClassStats::MergeAttributeBases(FCataclysmAttributePoints(), Bases);
		UCataclysmPlayerClassStats::ApplyTo(Character.AbilitySystem, UCataclysmPlayerClassStats::LoadTable(),
											TEXT("Ravager"), /*Level=*/10, Gear.IsEmpty() ? nullptr : &Gear,
											ECataclysmPoolFill::LeaveAsTheyAre, &Bases);
	}

	/** A flat amount from an item: the Jerkin's 8 evasion, its implicit in `game/Data/ItemBases.csv`. */
	FCataclysmStatModifier Flat(float Value)
	{
		FCataclysmStatModifier Out;
		Out.Bucket = ECataclysmStatBucket::Flat;
		Out.Source = ECataclysmModifierSource::GearAffix;
		Out.Value = Value;
		return Out;
	}

	FCataclysmCarriedSlot GemSlot(FName Gem)
	{
		FCataclysmCarriedSlot Slot;
		Slot.Gem = Gem;
		Slot.GemRarity = ECataclysmRarity::Everyday;
		Slot.Quantity = 1;
		return Slot;
	}

	TArray<FString> LinesFor(FName Gem, const UAbilitySystemComponent* Character)
	{
		return UCataclysmItemTooltip::LinesFor(GemSlot(Gem), nullptr, nullptr, nullptr, nullptr, nullptr, Character);
	}

	bool SaysItDoesNothing(const TArray<FString>& Lines)
	{
		return Lines.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("does nothing")); });
	}
}

// --------------------------------------------------------------------------------------------------------------------
// GT1. A character with none of the stat is told the gem does nothing for them.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemTooltipNoneTest,
	"Cataclysm.GemTooltip.ACharacterWithNoneOfTheStatIsToldTheGemDoesNothingForThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Ravager with no gear: no base and no flat evasion. Of The Wraith's tooltip, read for it, has the line about the
 * character and not the line for everyone, in the fixed line's place after the effect. CONTROL: read with no
 * character, the same gem has the line for everyone. Prints "GEM TOOLTIP, NONE".
 */
bool FCataclysmGemTooltipNoneTest::RunTest(const FString&)
{
	using namespace CataclysmGemTooltipTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World)
		|| !TestNotNull(TEXT("set-up: the generated gem table"), UCataclysmGems::LoadGeneratedTable()))
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCharacter Ravager(World);
	Apply(Ravager);
	const float Amount = UCataclysmGems::AmountToIncreaseFor(Ravager.AbilitySystem, FName(TEXT("evasion")));
	const TArray<FString> Read = LinesFor(Wraith, Ravager.AbilitySystem);
	const TArray<FString> Unread = LinesFor(Wraith, nullptr);
	AddInfo(FString::Printf(TEXT("GEM TOOLTIP, NONE: evasion to increase %.2f | read for it: %s | with no character: %s"),
							Amount, *FString::Join(Read, TEXT(" / ")), *FString::Join(Unread, TEXT(" / "))));

	TestEqual(TEXT("set-up: the Ravager has no evasion to increase"), Amount, 0.0f);
	TestTrue(TEXT("**read for it, the fourth line says the gem does nothing for it**"),
			 Read.Num() >= 4 && Read[3] == ForYou);
	TestFalse(TEXT("and not the line for everyone"), Read.Contains(FString(ForEveryone)));
	TestTrue(TEXT("CONTROL: with no character, the line for everyone, as before"),
			 Unread.Num() >= 4 && Unread[3] == ForEveryone);
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GT2. A character whose gear gives some of the stat is not told the gem does nothing.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemTooltipSomeTest,
	"Cataclysm.GemTooltip.ACharacterWhoseGearGivesSomeIsNotTold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Ravager whose items give 8 flat evasion: an evasion base of 0 and a flat amount of 8, so 8 to increase. Of The
 * Wraith's tooltip, read for it, says nothing of doing nothing, and has one line fewer than with no gear (the effect is
 * followed at once by the socket it fits). CONTROL: Of The Bulwark, block chance, which the same items do not give, still
 * says it does nothing. Prints "GEM TOOLTIP, SOME".
 */
bool FCataclysmGemTooltipSomeTest::RunTest(const FString&)
{
	using namespace CataclysmGemTooltipTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World)
		|| !TestNotNull(TEXT("set-up: the generated gem table"), UCataclysmGems::LoadGeneratedTable()))
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FGear Jerkin;
	Jerkin.Add(TEXT("evasion"), {Flat(8.0f)});
	FScopedCharacter Evasive(World);
	FScopedCharacter Bare(World);
	Apply(Evasive, Jerkin);
	Apply(Bare);
	const float Amount = UCataclysmGems::AmountToIncreaseFor(Evasive.AbilitySystem, FName(TEXT("evasion")));
	const TArray<FString> WraithLines = LinesFor(Wraith, Evasive.AbilitySystem);
	const TArray<FString> WraithBareLines = LinesFor(Wraith, Bare.AbilitySystem);
	const TArray<FString> BulwarkLines = LinesFor(Bulwark, Evasive.AbilitySystem);
	AddInfo(FString::Printf(TEXT("GEM TOOLTIP, SOME: evasion to increase %.2f | Of The Wraith: %s | Of The Bulwark: %s"),
							Amount, *FString::Join(WraithLines, TEXT(" / ")), *FString::Join(BulwarkLines, TEXT(" / "))));

	TestEqual(TEXT("set-up: 8 flat evasion from its items, on a base of 0"), Amount, 8.0f);
	TestFalse(TEXT("**Of The Wraith says nothing of doing nothing**"), SaysItDoesNothing(WraithLines));
	TestEqual(TEXT("one line fewer than with no gear"), WraithLines.Num(), WraithBareLines.Num() - 1);
	TestTrue(TEXT("CONTROL: Of The Bulwark, block chance, which the items do not give, still says it does nothing"),
			 SaysItDoesNothing(BulwarkLines));
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
// GT3. The inventory's hover passes the character through to the gem's line.
// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmGemTooltipHoverTest,
	"Cataclysm.GemTooltip.TheInventoryHoverPassesTheCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `UCataclysmItemComparison::TooltipColumnsFor`, which the inventory widget's carried cells and the ground hover call,
 * given a gem stack and the character's component: a gem is not compared (it has no base), so it returns false and its
 * own text, whose fourth line is the character's ("You have no evasion"). No equipment and no stand-in are needed for a
 * gem. Prints "GEM TOOLTIP, HOVER".
 */
bool FCataclysmGemTooltipHoverTest::RunTest(const FString&)
{
	using namespace CataclysmGemTooltipTest;
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("set-up: a world"), World)
		|| !TestNotNull(TEXT("set-up: the generated gem table"), UCataclysmGems::LoadGeneratedTable()))
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	FScopedCharacter Ravager(World);
	Apply(Ravager);
	FString Left;
	FString Right;
	const bool bCompared = UCataclysmItemComparison::TooltipColumnsFor(
		GemSlot(Wraith), /*Equipment=*/nullptr, Ravager.AbilitySystem, /*StandIn=*/nullptr, /*WornNow=*/nullptr,
		nullptr, nullptr, nullptr, nullptr, nullptr, Left, Right);
	TArray<FString> Lines;
	Left.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);
	AddInfo(FString::Printf(TEXT("GEM TOOLTIP, HOVER: compared %d | %s"), bCompared, *FString::Join(Lines, TEXT(" / "))));

	TestFalse(TEXT("a gem is not compared"), bCompared);
	TestTrue(TEXT("and has no second column"), Right.IsEmpty());
	TestTrue(TEXT("**its fourth line is the character's**"), Lines.Num() >= 4 && Lines[3] == ForYou);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
