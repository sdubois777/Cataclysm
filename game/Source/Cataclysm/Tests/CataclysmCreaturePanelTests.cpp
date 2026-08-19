// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "Character/CataclysmAbyssalWardenCharacter.h"
#include "Character/CataclysmBruteCharacter.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Interface/CataclysmCombatOverlay.h"
#include "Interface/CataclysmCreaturePanel.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The panel at the top of the screen describing the creature under the cursor.
 * Issue #740.
 *
 * WHAT IS COVERED. Every judgement the panel makes: whether it appears at all,
 * what its title says, where the words come from, what the health figures read,
 * and where on the screen the box lands.
 *
 * WHAT CANNOT BE. Anything reaching the screen, and which creature the cursor
 * is actually over. AHUD::PostRender checks FApp::CanEverRender() before it
 * calls DrawHUD and the automation command in tools/unreal_build.py passes
 * -nullrhi, so no drawing runs under test; and the cursor trace needs a player
 * controller, a mouse and a physics scene. That is why
 * ACataclysmHUD::CreatureUnderCursor is three lines with no decisions in it and
 * everything it feeds is here.
 *
 * THE ONE RULE MOST WORTH GUARDING. A Common creature gets a panel. That is the
 * OPPOSITE of the rule the word over the head follows, where a Common is left
 * bare on purpose, and copying the wrong one is an easy mistake that would look
 * like it worked: most creatures would simply never answer when pointed at.
 */
namespace CataclysmCreaturePanelTest
{
	using FPanel = UCataclysmCreaturePanel;

	/** The steps, in the order `sim/cataclysm_sim/enemy_stats.py` lists them. */
	constexpr int32 CommonStep = 0;
	constexpr int32 EliteStep = 1;

	/** An archetype table holding the two creatures that exist. */
	static UDataTable* MakeArchetypeTable()
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmEnemyArchetypeRow::StaticStruct();

		FCataclysmEnemyArchetypeRow Brute;
		Brute.ArchetypeName = TEXT("Brute");
		Table->AddRow(TEXT("Brute"), Brute);

		FCataclysmEnemyArchetypeRow Warden;
		Warden.ArchetypeName = TEXT("Abyssal Warden");
		Table->AddRow(TEXT("Abyssal_Warden"), Warden);

		return Table;
	}

	/** A modifier table holding two of the Demonic modifiers. */
	static UDataTable* MakeModifierTable()
	{
		UDataTable* Table = NewObject<UDataTable>();
		Table->RowStruct = FCataclysmEnemyModifierRow::StaticStruct();

		FCataclysmEnemyModifierRow Aura;
		Aura.CataclysmType = TEXT("Demonic");
		Aura.ModifierName = TEXT("Hellfire Aura");
		Aura.Description = TEXT("Emits a burning aura.");
		Table->AddRow(TEXT("Demonic_Hellfire_Aura"), Aura);

		FCataclysmEnemyModifierRow Charm;
		Charm.CataclysmType = TEXT("Demonic");
		Charm.ModifierName = TEXT("Beguiling");
		Charm.Description = TEXT("Charms a player that damages it.");
		Table->AddRow(TEXT("Demonic_Beguiling"), Charm);

		return Table;
	}

	static ACataclysmEnemyCharacter* SpawnEnemy(UWorld* World, float Health)
	{
		ACataclysmEnemyCharacter* Spawned =
			World->SpawnActor<ACataclysmEnemyCharacter>(FVector::ZeroVector,
														FRotator::ZeroRotator);
		if (Spawned)
		{
			Spawned->SetGenericTeamId(
				UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			// SetHealth sets the MAXIMUM, and the current value follows it.
			Spawned->SetHealth(Health);
		}
		return Spawned;
	}

	static ACataclysmPlayerCharacter* SpawnPlayer(UWorld* World)
	{
		ACataclysmPlayerCharacter* Pawn =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector,
														 FRotator::ZeroRotator);
		ACataclysmPlayerState* State =
			World->SpawnActor<ACataclysmPlayerState>();
		if (Pawn && State)
		{
			Pawn->SetPlayerState(State);
			Pawn->OnRep_PlayerState();
		}
		return Pawn;
	}
}

// ---------------------------------------------------------------------------
// Whether the panel appears at all
// ---------------------------------------------------------------------------

/**
 * A Common creature gets a panel, and that is the point of the whole feature.
 *
 * THE OPPOSITE OF THE WORD OVER THE HEAD, WHICH IS THE MISTAKE WORTH GUARDING.
 * `UCataclysmCombatOverlay::ShouldShowRarityNameFor` refuses a Common because a
 * word over 60% of what spawns is a word over most of the screen. Nothing is
 * cluttered by a panel: the player pointed at one creature and asked what it is.
 * Reusing the word's rule here would leave most of the game silent when hovered
 * and would look like it worked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreaturePanelShownForEveryRung,
	"Cataclysm.CreaturePanel.EveryRungIsDescribedIncludingCommon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreaturePanelShownForEveryRung::RunTest(const FString&)
{
	using namespace CataclysmCreaturePanelTest;

	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world to spawn in"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACataclysmPlayerCharacter* Player = SpawnPlayer(World);
	ACataclysmEnemyCharacter* Enemy = SpawnEnemy(World, 250.0f);
	if (!TestNotNull(TEXT("a player pawn"), Player)
		|| !TestNotNull(TEXT("an enemy"), Enemy))
	{
		return false;
	}

	Enemy->SetRarityStep(CommonStep);

	TestTrue(TEXT("an untouched Common creature is described"),
		FPanel::ShouldShowFor(Enemy, Player, 250.0f, 250.0f));

	// AND THE WORD OVER ITS HEAD IS STILL REFUSED FOR THE SAME CREATURE, which
	// is what makes the two rules different rather than one copied twice.
	TestFalse(TEXT("while no word is drawn over that same Common creature"),
		UCataclysmCombatOverlay::ShouldShowRarityNameFor(CommonStep, 250.0f,
														 250.0f));

	Enemy->SetRarityStep(EliteStep);
	TestTrue(TEXT("an Elite is described too"),
		FPanel::ShouldShowFor(Enemy, Player, 250.0f, 250.0f));

	// A HURT CREATURE IS STILL DESCRIBED. Nothing here waits for damage, unlike
	// the health bar over a creature.
	TestTrue(TEXT("and so is a badly hurt one"),
		FPanel::ShouldShowFor(Enemy, Player, 1.0f, 250.0f));

	// NOTHING FOR THE PLAYER'S OWN PAWN. Their health is already on the frame.
	TestFalse(TEXT("the player's own character is not described"),
		FPanel::ShouldShowFor(Player, Player, 250.0f, 250.0f));

	// NOTHING FOR A CORPSE, or the panel flashes for one frame at the end of
	// every fight: an enemy destroys itself on the tick AFTER it dies.
	TestFalse(TEXT("a creature at zero health is not described"),
		FPanel::ShouldShowFor(Enemy, Player, 0.0f, 250.0f));
	TestFalse(TEXT("and neither is one past zero"),
		FPanel::ShouldShowFor(Enemy, Player, -5.0f, 250.0f));

	// A CREATURE WITH NO HEALTH POOL IS NOT A CREATURE YET. Its ability system
	// arrives some frames after the actor does on a client.
	TestFalse(TEXT("a creature with no maximum health is not described"),
		FPanel::ShouldShowFor(Enemy, Player, 0.0f, 0.0f));

	TestFalse(TEXT("and nothing at all is not described"),
		FPanel::ShouldShowFor(nullptr, Player, 250.0f, 250.0f));

	// MARKED DEAD IS ITS OWN REFUSAL, and it is last because it cannot be
	// undone: an enemy is marked dead before its health has finished falling,
	// so the panel has to go on the mark rather than on the figures.
	UCataclysmSkillEffects::MarkDead(Enemy);
	TestFalse(TEXT("a creature marked dead is not described, whatever its "
				   "health still reads"),
		FPanel::ShouldShowFor(Enemy, Player, 250.0f, 250.0f));

	return true;
}

// ---------------------------------------------------------------------------
// What the panel says
// ---------------------------------------------------------------------------

/**
 * The first line: which rung, then which creature.
 *
 * BOTH HALVES COME OUT OF A GENERATED TABLE, so this checks the joining rather
 * than the words. What matters is that a missing half never produces a stray
 * space or a line that reads as though the creature lost its name.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreaturePanelTitle,
	"Cataclysm.CreaturePanel.TheTitleSaysTheRungThenTheCreature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreaturePanelTitle::RunTest(const FString&)
{
	using namespace CataclysmCreaturePanelTest;

	TestEqual(TEXT("both halves read as a person says them"),
		FPanel::TitleFor(TEXT("Brute"), TEXT("Elite")), FString(TEXT("Elite Brute")));

	TestEqual(TEXT("a Common is said out loud rather than left off"),
		FPanel::TitleFor(TEXT("Abyssal Warden"), TEXT("Common")),
		FString(TEXT("Common Abyssal Warden")));

	// A CREATURE WITH NO ARCHETYPE IS STILL NAMED. The sandbox's training
	// dummies are the base enemy class and carry no archetype row; issue #39 is
	// what gives every creature one.
	TestEqual(TEXT("a creature with no archetype gets the standing word"),
		FPanel::TitleFor(FString(), TEXT("Elite")),
		FString(TEXT("Elite ")) + FPanel::UnnamedCreature);

	// AND A RUNG THE TABLE CANNOT NAME LEAVES NO STRAY SPACE IN FRONT.
	TestEqual(TEXT("an unnamed rung leaves the creature's name alone"),
		FPanel::TitleFor(TEXT("Brute"), FString()), FString(TEXT("Brute")));

	TestEqual(TEXT("and neither half known still says something"),
		FPanel::TitleFor(FString(), FString()), FString(FPanel::UnnamedCreature));

	return true;
}

/**
 * A creature's name comes from the archetype table, not from its class name.
 *
 * WHY IT MATTERS. `game/Data/EnemyArchetypes.csv` is generated from the design
 * workbook, so a creature renamed in the design is renamed on screen without
 * anybody editing C++. It also holds the spacing: the row key is
 * `Abyssal_Warden` and the name is "Abyssal Warden".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreaturePanelReadsTheArchetypeTable,
	"Cataclysm.CreaturePanel.ACreatureIsNamedByTheArchetypeTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreaturePanelReadsTheArchetypeTable::RunTest(const FString&)
{
	using namespace CataclysmCreaturePanelTest;

	UDataTable* Table = MakeArchetypeTable();

	TestEqual(TEXT("the row key is turned into the designed name"),
		FPanel::ArchetypeNameForRow(Table, TEXT("Abyssal_Warden")),
		FString(TEXT("Abyssal Warden")));

	TestEqual(TEXT("and so is a one-word one"),
		FPanel::ArchetypeNameForRow(Table, TEXT("Brute")),
		FString(TEXT("Brute")));

	// EVERY FAILURE ANSWERS THE EMPTY STRING, which TitleFor turns into the
	// standing word. None of them throws a warning into the log every frame,
	// which is what bWarnIfRowMissing would do: this runs while the cursor is
	// moving.
	TestTrue(TEXT("a row the table does not hold answers nothing"),
		FPanel::ArchetypeNameForRow(Table, TEXT("Gatekeeper")).IsEmpty());
	TestTrue(TEXT("a creature naming no row answers nothing"),
		FPanel::ArchetypeNameForRow(Table, NAME_None).IsEmpty());
	TestTrue(TEXT("and no table at all answers nothing"),
		FPanel::ArchetypeNameForRow(nullptr, TEXT("Brute")).IsEmpty());

	return true;
}

/**
 * The two creatures that exist name rows the real table actually holds.
 *
 * WHAT THIS CATCHES THAT THE TEST ABOVE CANNOT. That one builds its own table
 * and would agree with a typo. This reads the generated asset, so a Brute whose
 * constructor said `Brutes` would be caught here and nowhere else -- the panel
 * would simply call it "Elite Creature" in play, which reads like a creature
 * with no archetype rather than like a mistake.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreaturePanelCreaturesNameRealRows,
	"Cataclysm.CreaturePanel.EveryEnemyClassNamesARowTheTableHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreaturePanelCreaturesNameRealRows::RunTest(const FString&)
{
	using namespace CataclysmCreaturePanelTest;

	const UDataTable* Table = FPanel::LoadEnemyArchetypeTable();
	if (!TestNotNull(TEXT("the generated archetype table loads"), Table))
	{
		return false;
	}

	const FName BruteRow = GetDefault<ACataclysmBruteCharacter>()->ArchetypeRow;
	TestEqual(TEXT("the Brute names its own row"), BruteRow, FName(TEXT("Brute")));
	TestEqual(TEXT("and the table names it back"),
		FPanel::ArchetypeNameForRow(Table, BruteRow), FString(TEXT("Brute")));

	const FName WardenRow =
		GetDefault<ACataclysmAbyssalWardenCharacter>()->ArchetypeRow;
	TestEqual(TEXT("the Abyssal Warden names its own row"), WardenRow,
		FName(TEXT("Abyssal_Warden")));
	TestEqual(TEXT("and the table spells it out"),
		FPanel::ArchetypeNameForRow(Table, WardenRow),
		FString(TEXT("Abyssal Warden")));

	// THE BASE ENEMY CLASS NAMES NONE, ON PURPOSE. It is what the sandbox spawns
	// as a training dummy, which is not one of the designed archetypes.
	TestTrue(TEXT("the base enemy class names no archetype"),
		GetDefault<ACataclysmEnemyCharacter>()->ArchetypeRow.IsNone());

	return true;
}

/**
 * The modifier lines, which are the reason this panel exists.
 *
 * NOTHING GRANTS A CREATURE A MODIFIER YET, so in play this list is empty until
 * one is typed into a placed creature by hand. What is checked here is that the
 * names come out of `game/Data/EnemyModifiers.csv` in the order the creature
 * carries them, and that a row the table does not hold is dropped rather than
 * drawn as a blank line in the middle of the panel.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreaturePanelModifierNames,
	"Cataclysm.CreaturePanel.ModifiersAreNamedFromTheTableInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreaturePanelModifierNames::RunTest(const FString&)
{
	using namespace CataclysmCreaturePanelTest;

	UDataTable* Table = MakeModifierTable();

	TArray<FString> Names;
	FPanel::ModifierNamesFor(
		Table, { TEXT("Demonic_Beguiling"), TEXT("Demonic_Hellfire_Aura") },
		Names);

	TestEqual(TEXT("both modifiers came back"), Names.Num(), 2);
	TestEqual(TEXT("in the order the creature carries them"), Names,
		TArray<FString>({ TEXT("Beguiling"), TEXT("Hellfire Aura") }));

	// A ROW THE TABLE DOES NOT HOLD IS DROPPED. A blank line in the middle of a
	// panel reads as a fault in the panel.
	FPanel::ModifierNamesFor(
		Table, { TEXT("Demonic_Hellfire_Aura"), TEXT("No_Such_Modifier") },
		Names);
	TestEqual(TEXT("an unknown row leaves one line rather than two"),
		Names.Num(), 1);
	TestEqual(TEXT("and it is the one that was found"), Names[0],
		FString(TEXT("Hellfire Aura")));

	// A CREATURE WITH NO MODIFIERS DRAWS NO LINES, which is every creature in
	// the game today.
	FPanel::ModifierNamesFor(Table, {}, Names);
	TestEqual(TEXT("a creature carrying none has no lines"), Names.Num(), 0);

	// AND THE OUTPUT IS CLEARED RATHER THAN APPENDED TO, or a second creature
	// hovered would inherit the first one's modifiers.
	FPanel::ModifierNamesFor(nullptr, { TEXT("Demonic_Beguiling") }, Names);
	TestEqual(TEXT("no table at all leaves nothing behind"), Names.Num(), 0);

	return true;
}

/**
 * The health figures, and the one value they must never print.
 *
 * A LIVING CREATURE NEVER READS ZERO. Health is an unrounded float that is only
 * clamped, so a creature sitting on 0.3 health is alive and hittable, and
 * rounding alone prints "0 / 250" for it. That is the same trap
 * `UCataclysmCombatOverlay::FigureFor` exists for, and it is not rare: every
 * killing blow in the game is clamped to exactly the target's remaining health.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreaturePanelHealthFigures,
	"Cataclysm.CreaturePanel.ALivingCreatureNeverReadsZeroHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreaturePanelHealthFigures::RunTest(const FString&)
{
	using namespace CataclysmCreaturePanelTest;

	TestEqual(TEXT("a creature at full health reads both figures"),
		FPanel::HealthTextFor(250.0f, 250.0f), FString(TEXT("250 / 250")));

	TestEqual(TEXT("a hurt one reads what is left"),
		FPanel::HealthTextFor(112.4f, 250.0f), FString(TEXT("112 / 250")));

	// THE ONE THAT MATTERS.
	TestEqual(TEXT("a creature on a fraction of a point still reads 1"),
		FPanel::HealthTextFor(0.3f, 250.0f), FString(TEXT("1 / 250")));

	TestEqual(TEXT("and one just above rounding still reads 1"),
		FPanel::HealthTextFor(0.49f, 250.0f), FString(TEXT("1 / 250")));

	// A CORPSE READS ZERO, WHICH IS TRUE OF IT. The panel is refused for one
	// anyway; this is the arithmetic being honest rather than the panel.
	TestEqual(TEXT("a creature actually at zero reads zero"),
		FPanel::HealthTextFor(0.0f, 250.0f), FString(TEXT("0 / 250")));

	// NO POOL AT ALL SAYS NOTHING RATHER THAN "0 / 0".
	TestTrue(TEXT("a creature with no health pool says nothing"),
		FPanel::HealthTextFor(0.0f, 0.0f).IsEmpty());

	return true;
}

// ---------------------------------------------------------------------------
// Where the panel goes
// ---------------------------------------------------------------------------

/**
 * The top centre of the screen, at a size that does not change as health does.
 *
 * THE JITTER IS THE PART A TEST CAN CATCH. Health is redrawn every frame and
 * its figures change width as the digits do -- "250 / 250" becomes "9 / 250" --
 * so a panel sized only to its contents would breathe in and out through a
 * fight. The floor on the width is what stops that, and it only works if it is
 * a share of the viewport rather than a fixed number of pixels.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmCreaturePanelBox,
	"Cataclysm.CreaturePanel.ItSitsAtTheTopCentreAndKeepsItsWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmCreaturePanelBox::RunTest(const FString&)
{
	using namespace CataclysmCreaturePanelTest;

	const FVector2D Viewport(1920.0, 1080.0);

	const FBox2D Narrow = FPanel::PanelBoxFor(Viewport, 120.0f, 90.0f);

	// AT THE TOP, BY ITS MARGIN.
	TestEqual(TEXT("the panel starts at the top margin"),
		static_cast<float>(Narrow.Min.Y), FPanel::TopMarginPx);

	// CENTRED, which is what "top centre" means and what a title centred inside
	// the panel depends on.
	const float LeftGap = static_cast<float>(Narrow.Min.X);
	const float RightGap = static_cast<float>(Viewport.X - Narrow.Max.X);
	TestTrue(TEXT("and it is centred left to right"),
		FMath::IsNearlyEqual(LeftGap, RightGap, 0.01f));

	// THE FLOOR HOLDS FOR A SHORT TITLE, which is what stops the jitter.
	const float Floor = static_cast<float>(Viewport.X) * FPanel::MinimumWidthShare;
	TestTrue(TEXT("a short title still gets the whole panel width"),
		FMath::IsNearlyEqual(static_cast<float>(Narrow.Max.X - Narrow.Min.X),
							 Floor, 0.01f));

	// AND TWO CONTENT WIDTHS UNDER THE FLOOR GIVE THE SAME PANEL. This is the
	// jitter check itself rather than a check on the floor's value.
	const FBox2D Shorter = FPanel::PanelBoxFor(Viewport, 40.0f, 90.0f);
	TestTrue(TEXT("and a shorter one gives exactly the same box"),
		Shorter.Min.Equals(Narrow.Min, 0.01) && Shorter.Max.Equals(Narrow.Max, 0.01));

	// THE HEIGHT FOLLOWS THE CONTENT, because a creature with five modifiers has
	// five more lines than one with none.
	TestEqual(TEXT("the height is the content plus padding on both sides"),
		static_cast<float>(Narrow.Max.Y - Narrow.Min.Y),
		90.0f + FPanel::PaddingPx * 2.0f);

	const FBox2D Taller = FPanel::PanelBoxFor(Viewport, 120.0f, 210.0f);
	TestTrue(TEXT("and more content makes a taller panel"),
		Taller.Max.Y > Narrow.Max.Y);

	// PAST THE FLOOR, THE PANEL GROWS RATHER THAN CUTTING THE TEXT OFF.
	const FBox2D Wide = FPanel::PanelBoxFor(Viewport, 900.0f, 90.0f);
	TestEqual(TEXT("a long line makes the panel wider than the floor"),
		static_cast<float>(Wide.Max.X - Wide.Min.X),
		900.0f + FPanel::PaddingPx * 2.0f);
	TestTrue(TEXT("and it stays centred"),
		FMath::IsNearlyEqual(static_cast<float>(Wide.Min.X),
							 static_cast<float>(Viewport.X - Wide.Max.X), 0.01f));

	// AND IT NEVER LEAVES THE SCREEN. Without the clamp a line wider than the
	// viewport would put the panel's left edge off the left of it, taking the
	// centred title with it.
	const FBox2D Overflowing = FPanel::PanelBoxFor(Viewport, 4000.0f, 90.0f);
	TestTrue(TEXT("an overlong line does not push the panel off the screen"),
		Overflowing.Min.X >= 0.0);
	TestTrue(TEXT("and does not push it off the right either"),
		Overflowing.Max.X <= Viewport.X);

	// A DIFFERENT VIEWPORT MOVES THE FLOOR WITH IT, which is the whole reason it
	// is a share rather than a number of pixels.
	const FVector2D Small(1280.0, 720.0);
	const FBox2D OnSmall = FPanel::PanelBoxFor(Small, 40.0f, 90.0f);
	TestTrue(TEXT("a smaller screen gets a narrower panel"),
		FMath::IsNearlyEqual(static_cast<float>(OnSmall.Max.X - OnSmall.Min.X),
							 static_cast<float>(Small.X) * FPanel::MinimumWidthShare,
							 0.01f));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
