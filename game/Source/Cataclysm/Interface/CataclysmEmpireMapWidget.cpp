// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireMapWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Empire/CataclysmEmpireRun.h"
#include "Interface/CataclysmChoiceButton.h"
#include "Player/CataclysmGameInstance.h"
#include "Player/CataclysmPlayerController.h"

namespace
{
	/** The name a city's button carries. The identifier, written out. */
	FName ButtonNameFor(int32 CityId)
	{
		return FName(*FString::Printf(TEXT("City%d"), CityId));
	}

	/**
	 * The name the Cataclysm boss dungeon's button carries. No city's name can be
	 * it, so `CityForButton` answers `INDEX_NONE` for it. Issue #1324 slice 6c.
	 */
	FName CataclysmDungeonButtonName()
	{
		return FName(TEXT("CataclysmBossDungeon"));
	}
}

// ---------------------------------------------------------------------------
// Building and redrawing
// ---------------------------------------------------------------------------

void UCataclysmEmpireMapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (TitleLabel)
	{
		TitleLabel->SetText(Title);
	}

	Refresh();
}

void UCataclysmEmpireMapWidget::NativeTick(const FGeometry& MyGeometry,
										   float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	const UCataclysmEmpireRun* Run = ShownRun();
	const int32 Day = Run ? Run->Day() : INDEX_NONE;
	const FVector2D Size = CanvasSize();

	// ONLY WHEN SOMETHING MOVED. Rebuilding 25 boxes every frame would be
	// exactly the interface cost issue #825 is about, and the empire changes
	// only when a day passes -- which is never on its own, because nothing in
	// the game advances the day yet -- or when the panel is resized.
	if (Day != DrawnForDay || !Size.Equals(DrawnForSize, 1.0))
	{
		Refresh();
	}
}

void UCataclysmEmpireMapWidget::Refresh()
{
	if (CityButtons.IsEmpty())
	{
		BuildCities();
	}

	PlaceCities();
	WriteStatus();

	const UCataclysmEmpireRun* Run = ShownRun();
	DrawnForDay = Run ? Run->Day() : INDEX_NONE;
	DrawnForSize = CanvasSize();
}

UCataclysmEmpireRun* UCataclysmEmpireMapWidget::ShownRun() const
{
	// THE GAME'S OWN RUN FIRST, ALWAYS. The test seam is only reached when there
	// is no game instance of this project's class at all, which is the case a
	// headless test is in and nothing in a running game ever is.
	if (UCataclysmEmpireRun* Run =
			UCataclysmGameInstance::EmpireRunFor(this, /*bStartIfNone*/ false))
	{
		return Run;
	}

	return RunForTests;
}

void UCataclysmEmpireMapWidget::SetRunForTests(UCataclysmEmpireRun* Run)
{
	RunForTests = Run;
}

void UCataclysmEmpireMapWidget::SetPanelSizeForTests(FVector2D Size)
{
	PanelSizeForTests = Size;
}

// ---------------------------------------------------------------------------
// The cities
// ---------------------------------------------------------------------------

void UCataclysmEmpireMapWidget::BuildCities()
{
	const UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr || Run->Map == nullptr || MapCanvas == nullptr)
	{
		return;
	}

	TSubclassOf<UCataclysmChoiceButton> ButtonClass =
		ChoiceButtonClass.LoadSynchronous();
	if (!ButtonClass)
	{
		return;
	}

	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		UCataclysmChoiceButton* Button =
			CreateWidget<UCataclysmChoiceButton>(this, ButtonClass);
		if (!Button)
		{
			continue;
		}

		Button->OnChoiceHovered.AddDynamic(
			this, &UCataclysmEmpireMapWidget::HandleCityHovered);
		Button->OnChoiceClicked.AddDynamic(
			this, &UCataclysmEmpireMapWidget::HandleCityClicked);

		// A LABEL TOO LONG FOR ITS BOX IS CUT, NOT DRAWN OVER THE NEXT CITY.
		// The whole of it is in the detail line under the map whenever the
		// cursor is over a city.
		Button->SetClipping(EWidgetClipping::ClipToBounds);

		if (UCanvasPanelSlot* Placement =
				Cast<UCanvasPanelSlot>(MapCanvas->AddChild(Button)))
		{
			// ALIGNED TO ITS OWN MIDDLE, so the position a city is given is the
			// city's centre. Anchoring by the corner would push the whole
			// diamond half a box down and right of where it was fitted.
			Placement->SetAnchors(FAnchors(0.0f, 0.0f));
			Placement->SetAlignment(FVector2D(0.5, 0.5));
			Placement->SetAutoSize(false);
		}

		CityButtons.Add(Button);
	}

	// THE CATACLYSM BOSS DUNGEON'S BOX, made now with the cities and hidden until
	// the run has one standing, so opening it needs no widget made mid-run. The
	// same button class, the same two handlers, and the same placement rules as a
	// city's. Issue #1324 slice 6c.
	if (UCataclysmChoiceButton* Button =
			CreateWidget<UCataclysmChoiceButton>(this, ButtonClass))
	{
		Button->OnChoiceHovered.AddDynamic(
			this, &UCataclysmEmpireMapWidget::HandleCityHovered);
		Button->OnChoiceClicked.AddDynamic(
			this, &UCataclysmEmpireMapWidget::HandleCityClicked);
		Button->SetClipping(EWidgetClipping::ClipToBounds);
		Button->SetVisibility(ESlateVisibility::Collapsed);

		if (UCanvasPanelSlot* Placement =
				Cast<UCanvasPanelSlot>(MapCanvas->AddChild(Button)))
		{
			Placement->SetAnchors(FAnchors(0.0f, 0.0f));
			Placement->SetAlignment(FVector2D(0.5, 0.5));
			Placement->SetAutoSize(false);
		}

		CataclysmDungeonButton = Button;
	}
}

void UCataclysmEmpireMapWidget::PlaceCities()
{
	const UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr || Run->Map == nullptr)
	{
		return;
	}

	const FVector2D Size = CanvasSize();
	const float Scale = UCataclysmEmpireMapLayout::ScaleToFit(Size);
	const FVector2D BoxSize = UCataclysmEmpireMapLayout::CitySize(Scale);

	for (int32 Index = 0; Index < CityButtons.Num(); ++Index)
	{
		UCataclysmChoiceButton* Button = CityButtons[Index];
		const FCataclysmCity* City = Run->Map->Find(Index);
		if (!Button || City == nullptr)
		{
			continue;
		}

		if (UCanvasPanelSlot* Placement = Cast<UCanvasPanelSlot>(Button->Slot))
		{
			Placement->SetPosition(UCataclysmEmpireMapLayout::PositionFor(
				City->R, City->C, Scale, Size));
			Placement->SetSize(BoxSize);
		}

		// THE WORDS SHRINK WITH THE BOX. Without this the box scales to fit the
		// panel and the label stays the size the Widget Blueprint gave it, so
		// every city read "Outpost 10" where it should read "Outpost 100%".
		// Issue #1089.
		Button->SetLabelScale(Scale);

		const ECataclysmCityMark Mark =
			UCataclysmEmpireMapLayout::MarkFor(Run->Map, Index);

		// THREE STATES OUT OF THE CHOICE BUTTON'S OWN THREE APPEARANCES, rather
		// than three more colour properties on this screen. A fallen city is
		// unavailable and reads dim; an exposed one is chosen and reads bright,
		// because it is the one the player has to decide about; a sealed one is
		// plain.
		Button->SetChoice(
			ButtonNameFor(Index),
			FText::FromString(
				UCataclysmEmpireMapLayout::CityLabel(Run->Map, Index)),
			/*bChosen*/ Mark == ECataclysmCityMark::Exposed,
			/*bAvailable*/ Mark != ECataclysmCityMark::Fallen);
	}

	PlaceCataclysmDungeon(Scale);
}

void UCataclysmEmpireMapWidget::PlaceCataclysmDungeon(float Scale)
{
	if (CataclysmDungeonButton == nullptr)
	{
		return;
	}

	// NO LABEL, NO BOX: before the run has opened it, and after it has been
	// cleared. See `UCataclysmEmpireMapLayout::CataclysmDungeonMarkerLabel`.
	const FString Label =
		UCataclysmEmpireMapLayout::CataclysmDungeonMarkerLabel(ShownRun());
	if (Label.IsEmpty())
	{
		CataclysmDungeonButton->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	if (UCanvasPanelSlot* Placement =
			Cast<UCanvasPanelSlot>(CataclysmDungeonButton->Slot))
	{
		Placement->SetPosition(
			UCataclysmEmpireMapLayout::CataclysmDungeonMarkerPosition(Scale));
		Placement->SetSize(
			UCataclysmEmpireMapLayout::CataclysmDungeonMarkerSize(Scale));
	}

	CataclysmDungeonButton->SetLabelScale(Scale);

	// CHOSEN, SO IT READS BRIGHT, as an exposed city does: it is the dungeon the
	// whole run has been working towards, and it should not read as a plain
	// sealed city in a corner.
	CataclysmDungeonButton->SetChoice(CataclysmDungeonButtonName(),
									  FText::FromString(Label),
									  /*bChosen*/ true, /*bAvailable*/ true);
	CataclysmDungeonButton->SetVisibility(ESlateVisibility::Visible);
}

void UCataclysmEmpireMapWidget::WriteCataclysmDungeonDetail()
{
	if (DetailLabel != nullptr)
	{
		DetailLabel->SetText(FText::FromString(
			UCataclysmEmpireMapLayout::CataclysmDungeonBrief(ShownRun())));
	}
}

void UCataclysmEmpireMapWidget::WriteStatus()
{
	const UCataclysmEmpireRun* Run = ShownRun();

	if (Run == nullptr || Run->Map == nullptr || Run->Surges == nullptr)
	{
		if (StatusLabel)
		{
			StatusLabel->SetText(NSLOCTEXT("Cataclysm", "EmpireNoRun",
										   "No run has been started."));
		}
		if (SurgeLabel)
		{
			SurgeLabel->SetText(FText::GetEmpty());
		}
		return;
	}

	if (StatusLabel)
	{
		// WHAT THE PLAYER HAS DONE, ON THE END OF WHAT HAS BEEN DONE TO THEM.
		// Every other number on this screen describes the empire's decline;
		// `ProgressLine` is the only one that describes progress. Issue #1324
		// slice 5, and it lives on the layout class because a headless test
		// cannot read a label. See `UCataclysmEmpireMapLayout::ProgressLine`.
		//
		// AND, ON ITS END, THE CATACLYSMS FINISHED AND WHAT OPENS THE BOSS
		// DUNGEON, or that it is open, since issue #1324 slice 6c. The dungeons
		// standing count it: it stands, on no city. AND ONE DUNGEON IS SINGULAR,
		// which it was not until then: with the boss standing the empty-board
		// surge does not fire, so a board holding the boss alone is a state a
		// player now sits in, and it read "1 dungeons standing".
		//
		// AND THE DAYS A WALK OWES, after the day they will be added to, since
		// 2026-10-24: nothing resolves while the player is inside a dungeon, so
		// the day shown does not move during a walk. Empty outside one. See
		// `UCataclysmEmpireMapLayout::DaysOwedLine`.
		const FString Owed = UCataclysmEmpireMapLayout::DaysOwedLine(Run);
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Day %d%s   %d cities lost of 24   %d dungeon%s standing   %s"),
			Run->Day(), Owed.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Owed),
			Run->Map->FallenCityCount(), Run->DungeonCount(),
			Run->DungeonCount() == 1 ? TEXT("") : TEXT("s"),
			*UCataclysmEmpireMapLayout::ProgressLine(Run))));
	}

	if (SurgeLabel)
	{
		// DISTANCE TO DEFEAT AND NOT THE COUNT OF CITIES LOST, because that is
		// the number that decides the run: twenty Outposts scattered around the
		// rim cost less than three in a line.
		const FString Threat = Run->Map->IsPillarExposed()
			? FString(TEXT("The Cataclysm can reach the Pillar."))
			: FString::Printf(TEXT("%d cities from defeat."),
							  Run->Map->DistanceToDefeat());

		// AND THE LAST HIT A DUNGEON'S TIMER LANDED, ON THE END OF THIS LABEL.
		// An ordinary dungeon leaves after it hits, so nothing else on the
		// screen says what took a city's defence. Or, when it is as recent,
		// the last Quest dungeon a city's fall moved, since 2026-10-21.
		// Appended here so that no Widget Blueprint has to gain a label; empty
		// until the first hit or move. See
		// `UCataclysmEmpireMapLayout::LastHitLine`.
		//
		// OR WHAT THE PLAYER'S LAST DEATH COST, while it is the most recent of the
		// three, since 2026-11-02 (issue #41). `LatestEventLine` chooses; see
		// `UCataclysmEmpireMapLayout::LastDeathLine`. OR THE DAY THE CATACLYSM
		// BOSS DUNGEON OPENED, while that is the most recent, since issue #1324
		// slice 6c.
		FString LastHit = UCataclysmEmpireMapLayout::LatestEventLine(Run);

		// AND THE WIN OF THE RUN BEFORE THIS ONE, FIRST, for as long as this run lasts: the run this map shows began
		// at once after it, one tier higher (issue #1324 slice 6e). Empty when the run before was not won. See
		// `UCataclysmEmpireMapLayout::LastRunWonLine`.
		const FString Won = UCataclysmEmpireMapLayout::LastRunWonLine(Run);
		if (!Won.IsEmpty())
		{
			LastHit = LastHit.IsEmpty() ? Won : FString::Printf(TEXT("%s   %s"), *Won, *LastHit);
		}

		SurgeLabel->SetText(FText::FromString(FString::Printf(
			TEXT("%s   Next surge in %.0f days, bringing %d.%s%s"),
			*Threat,
			Run->Surges->DaysUntilNextSurge(Run->Day()),
			Run->Surges->DungeonsInNextSurge(),
			LastHit.IsEmpty() ? TEXT("") : TEXT("   "),
			*LastHit)));
	}
}

// ---------------------------------------------------------------------------
// The cursor
// ---------------------------------------------------------------------------

void UCataclysmEmpireMapWidget::HandleCityHovered(FName Value)
{
	if (DetailLabel == nullptr)
	{
		return;
	}

	// THE BOSS DUNGEON'S BOX, which is no city's. Issue #1324 slice 6c.
	if (Value == CataclysmDungeonButtonName())
	{
		WriteCataclysmDungeonDetail();
		return;
	}

	const UCataclysmEmpireRun* Run = ShownRun();
	const int32 CityId = CityForButton(Value);

	if (Run == nullptr || Run->Map == nullptr || CityId == INDEX_NONE)
	{
		DetailLabel->SetText(FText::GetEmpty());
		return;
	}

	const FCataclysmCity* City = Run->Map->Find(CityId);
	if (City == nullptr)
	{
		DetailLabel->SetText(FText::GetEmpty());
		return;
	}

	TArray<FString> Parts;

	Parts.Add(City->Name);

	if (City->bFallen)
	{
		Parts.Add(TEXT("fallen"));
	}
	else
	{
		Parts.Add(FString::Printf(TEXT("%.0f of %.0f defence"),
								  City->Defence, City->MaxDefence));
		Parts.Add(FString::Printf(TEXT("%.0f of %.0f people"),
								  City->Population, City->MaxPopulation));
		Parts.Add(Run->Map->IsExposed(CityId) ? TEXT("exposed")
											  : TEXT("sealed behind the frontier"));
	}

	// WHAT IS STANDING ON IT AND HOW LONG EACH HAS. This is the whole triage
	// decision: which dungeon is about to detonate on which city.
	for (const int32 DungeonId : Run->DungeonsOn(CityId))
	{
		const FCataclysmDungeon* Dungeon = Run->FindDungeon(DungeonId);
		if (Dungeon == nullptr || Run->Clock == nullptr)
		{
			continue;
		}

		Parts.Add(FString::Printf(
			TEXT("dungeon of %d floors, %.0f days left"),
			Dungeon->Floors, Run->Clock->DaysUntilResolveFor(DungeonId)));
	}

	DetailLabel->SetText(FText::FromString(FString::Join(Parts, TEXT("   "))));
}

void UCataclysmEmpireMapWidget::HandleCityClicked(FName Value)
{
	// THE BOSS DUNGEON'S BOX SAYS WHAT IT IS AND OPENS NOTHING. It has no city
	// screen, and nothing walks a dungeon from this map -- there is no travel
	// between the empire and a dungeon level, issue #48 -- so a press does what
	// the cursor does: the brief, which ends by naming the command that walks it,
	// `Cataclysm.EnterDungeon <id>`, with its identifier. Issue #1324 slice 6c.
	if (Value == CataclysmDungeonButtonName())
	{
		WriteCataclysmDungeonDetail();
		return;
	}

	const int32 CityId = CityForButton(Value);
	const UCataclysmEmpireRun* Run = ShownRun();

	if (Run == nullptr || Run->Map == nullptr || CityId == INDEX_NONE)
	{
		return;
	}

	// WHICH CITY WAS CLICKED, RECORDED WHETHER OR NOT A SCREEN OPENS. A headless
	// test has no player controller, so opening the screen is the one part of
	// this that cannot be watched; recording the city is what lets a test check
	// that a click on a particular box means a particular city.
	ClickedCityId = CityId;

	// THE DETAIL LINE STILL SAYS WHAT WAS CLICKED. The city screen covers the
	// map while it is open, and closing it leaves the map reading as though
	// nothing happened unless the line remembers.
	if (DetailLabel != nullptr)
	{
		if (const FCataclysmCity* City = Run->Map->Find(CityId))
		{
			DetailLabel->SetText(FText::FromString(*City->Name));
		}
	}

	// AND THE CITY SCREEN OPENS ON IT. The player controller owns every screen
	// in this project, so this asks rather than making one: a second city screen
	// belonging to the map would be a second one to keep in step.
	if (ACataclysmPlayerController* Controller =
			Cast<ACataclysmPlayerController>(GetOwningPlayer()))
	{
		Controller->ToggleCityScreen(CityId);
	}
}

int32 UCataclysmEmpireMapWidget::CityForButton(FName Value) const
{
	// AGAINST THE MAP RATHER THAN AGAINST THE BOXES, and it is a fix rather than
	// a tidy-up. Walking `CityButtons` needed the boxes to exist, and the
	// automation test command passes `-nullrhi`: a widget with no Widget
	// Blueprint has a null canvas, builds no boxes, and so resolved every click
	// to nothing. That made the one part of a click that can go wrong silently
	// -- sending a player to the wrong city -- impossible to test at all.
	//
	// THE MAP IS ALSO THE BETTER AUTHORITY. A city identifier means a city on
	// the map, not a widget, and the two lists are the same 25 entries in a
	// running game because `BuildCities` makes one box per city.
	const UCataclysmEmpireRun* Run = ShownRun();
	if (Run == nullptr || Run->Map == nullptr)
	{
		return INDEX_NONE;
	}

	for (const FCataclysmCity& City : Run->Map->Cities)
	{
		if (ButtonNameFor(City.CityId) == Value)
		{
			return City.CityId;
		}
	}

	return INDEX_NONE;
}

// ---------------------------------------------------------------------------
// What a test can read
// ---------------------------------------------------------------------------

FVector2D UCataclysmEmpireMapWidget::PlacedAt(int32 CityId) const
{
	if (!CityButtons.IsValidIndex(CityId) || CityButtons[CityId] == nullptr)
	{
		return FVector2D::ZeroVector;
	}

	if (const UCanvasPanelSlot* Placement =
			Cast<UCanvasPanelSlot>(CityButtons[CityId]->Slot))
	{
		return Placement->GetPosition();
	}

	return FVector2D::ZeroVector;
}

FString UCataclysmEmpireMapWidget::StatusText() const
{
	return StatusLabel ? StatusLabel->GetText().ToString() : FString();
}

FString UCataclysmEmpireMapWidget::SurgeText() const
{
	return SurgeLabel ? SurgeLabel->GetText().ToString() : FString();
}

FString UCataclysmEmpireMapWidget::DetailText() const
{
	return DetailLabel ? DetailLabel->GetText().ToString() : FString();
}

void UCataclysmEmpireMapWidget::ClickCityForTests(int32 CityId)
{
	// THE NAME THE BUTTON WOULD CARRY, so this goes through the same lookup a
	// real click does. Passing the identifier straight through would skip
	// `CityForButton`, which is the part that can be wrong.
	HandleCityClicked(ButtonNameFor(CityId));
}

FVector2D UCataclysmEmpireMapWidget::CanvasSize() const
{
	// WHAT A TEST SAID IT IS, BEFORE ANYTHING ELSE. A headless test has no
	// geometry at all, so without this the two answers below are the same number
	// for ever and a test cannot make the panel change size. Zero means nothing
	// was said. Issue #1078 recorded this for the passive tree.
	if (!PanelSizeForTests.IsNearlyZero())
	{
		return PanelSizeForTests;
	}

	if (MapCanvas)
	{
		const FVector2D Measured = MapCanvas->GetCachedGeometry().GetLocalSize();
		if (Measured.X > 1.0 && Measured.Y > 1.0)
		{
			return Measured;
		}
	}

	// A COMMON WINDOW, so the first frame is roughly right rather than absurd.
	// A widget has no geometry until it has been laid out at least once, and the
	// first refresh happens in `NativeConstruct`, which is before that.
	return FVector2D(1600.0, 800.0);
}
