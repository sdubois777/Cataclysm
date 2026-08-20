// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmHUD.h"
#include "Character/CataclysmCharacterBase.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmEnemyRarity.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interface/CataclysmCreaturePanel.h"
#include "Items/CataclysmDroppedItem.h"
#include "Player/CataclysmPlayerController.h"

ACataclysmHUD::ACataclysmHUD()
{
	// NOTHING HERE TICKS. AHUD is driven by PostRender, which the player
	// controller calls once a frame after the scene is drawn, so a tick would
	// only be a second clock saying the same thing.
	PrimaryActorTick.bCanEverTick = false;
}

void ACataclysmHUD::DrawHUD()
{
	Super::DrawHUD();

	// A HEADS-UP DISPLAY WITH NO CANVAS IS NOT AN ERROR. PostRender only
	// supplies one when there is something to draw on, and the checks the
	// engine makes before then -- FApp::CanEverRender() among them -- mean this
	// method does not run at all under -nullrhi.
	if (!Canvas)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DropExpired(World->GetTimeSeconds());

	if (UCataclysmCombatOverlay::PlayerVitalsEnabled())
	{
		DrawPlayerVitals();
	}

	if (UCataclysmCombatOverlay::OverheadBarsEnabled())
	{
		DrawOverheadBars();
	}

	if (UCataclysmCombatOverlay::RarityNamesEnabled())
	{
		DrawRarityNames();
	}

	DrawDamageNumbers();
	DrawDropNames();

	// LAST, SO IT SITS ON TOP OF EVERYTHING. The panel hides most of what is
	// behind it and covers a strip of the screen; a damage number or a drop
	// name drawn after it would print over the text describing the creature.
	if (UCataclysmCreaturePanel::CreaturePanelEnabled())
	{
		DrawCreaturePanel();
	}
}

void ACataclysmHUD::AddDamageNumber(const FCataclysmDamageNumber& Number)
{
	Numbers.Add(Number);

	// THE OLDEST GO FIRST. They are the ones already fading, so the ceiling
	// costs the numbers a player has largely finished reading rather than the
	// one describing the blow that just landed.
	const int32 Excess =
		Numbers.Num() - UCataclysmCombatOverlay::MaxNumbersWaiting;
	if (Excess > 0)
	{
		Numbers.RemoveAt(0, Excess, EAllowShrinking::No);
	}
}

void ACataclysmHUD::DropExpired(float WorldSeconds)
{
	Numbers.RemoveAll([WorldSeconds](const FCataclysmDamageNumber& Number)
	{
		return WorldSeconds - Number.StartedAt
			>= UCataclysmCombatOverlay::NumberLifetimeSeconds;
	});
}

UFont* ACataclysmHUD::OverlayFont() const
{
	// THE ENGINE'S OWN FONT, WHICH IS WHY THIS FEATURE SHIPS NO ASSET. There is
	// no font anywhere under game/Content and none is needed: AHUD::DrawText
	// falls back to the engine's font when handed null, and one is asked for
	// explicitly only so GetTextSize measures the same font that DrawText will
	// draw with. A mismatch there would centre text against the wrong width.
	//
	// THE LARGE ONE RATHER THAN THE MEDIUM ONE, SINCE ISSUE #671. The project
	// owner played a build where every number was drawn at 1.6 times the medium
	// font and reported it as "pretty small". Scaling a fixed-size font further
	// eventually blurs it rather than enlarging it, so the larger face carries
	// part of the increase and the scale carries the rest.
	return GEngine ? GEngine->GetLargeFont() : nullptr;
}

void ACataclysmHUD::DrawBar(float ScreenX, float ScreenY, float Width,
							float Height, float Fraction,
							const FLinearColor& Fill, float Opacity)
{
	FLinearColor Backing =
		UCataclysmCombatOverlay::ColourFromHex(
			UCataclysmCombatOverlay::BarBackingHex);
	Backing.A = Opacity;

	// THE DARK BACKING IS DRAWN WIDER THAN THE FILL ON EVERY SIDE, so it reads
	// as an outline. It is the same trick the attack telegraph uses with its
	// outermost near-black ring, and for the same reason: the design guarantees
	// a world surface stays under 30% brightness, which holds a large fill
	// against a floor but says nothing about a seven pixel bar seen against
	// Demonic lava or a Celestial wall. With the backing, the contrast is a
	// property of the bar rather than of wherever it is standing.
	DrawRect(Backing, ScreenX - BarBackingInsetPx, ScreenY - BarBackingInsetPx,
			 Width + BarBackingInsetPx * 2.0f,
			 Height + BarBackingInsetPx * 2.0f);

	const float Filled = Width * FMath::Clamp(Fraction, 0.0f, 1.0f);
	if (Filled <= 0.0f)
	{
		return;
	}

	FLinearColor Drawn = Fill;
	Drawn.A = Opacity;
	DrawRect(Drawn, ScreenX, ScreenY, Filled, Height);
}

FBox2D ACataclysmHUD::MeasureTextCentred(const FString& Text, float CentreX,
										float TopY, float Scale)
{
	UFont* Font = OverlayFont();

	// THE BASE SIZE IS APPLIED AT THIS ONE POINT rather than at each caller. It
	// covers the floating damage numbers and the player's own health and mana
	// figures together, and the project owner's complaint after playing was that
	// the font is "too small in general" rather than about either one. Issue
	// #668.
	//
	// THE CALLER'S Scale STAYS RELATIVE. UCataclysmCombatOverlay::ScaleFor
	// answers 1.0 for an ordinary blow, 0.7 for a damage over time tick and 1.35
	// for a critical strike, and those are ratios between numbers rather than
	// absolute sizes. Multiplying here keeps that separation: the overlay decides
	// what is bigger than what, and the heads-up display decides how big.
	//
	// MEASURING AND DRAWING BOTH DO THIS MULTIPLICATION, and they have to agree,
	// which is why DrawOutlinedText takes the same Scale rather than a size in
	// pixels.
	const float Sized = Scale * TextScale;

	float Width = 0.0f;
	float Height = 0.0f;
	GetTextSize(Text, Width, Height, Font, Sized);

	const float Left = CentreX - Width * 0.5f;

	// THE RECTANGLE THE TEXT ITSELF FILLS, not the outline around it. The
	// outline is one pixel of spread and including it would make two names
	// drawn close together overlap slightly more than they look like they do.
	return FBox2D(FVector2D(Left, TopY), FVector2D(Left + Width, TopY + Height));
}

FBox2D ACataclysmHUD::DrawTextCentred(const FString& Text,
									  const FLinearColor& Colour, float CentreX,
									  float TopY, float Scale)
{
	const FBox2D Where = MeasureTextCentred(Text, CentreX, TopY, Scale);
	DrawOutlinedText(Text, Colour, Where.Min.X, Where.Min.Y, Scale);
	return Where;
}

void ACataclysmHUD::DrawOutlinedText(const FString& Text,
									 const FLinearColor& Colour, float Left,
									 float Top, float Scale)
{
	UFont* Font = OverlayFont();
	const float Sized = Scale * TextScale;

	// A BLACK OUTLINE, BECAUSE A COLOUR ALONE DOES NOT SURVIVE THE FLOOR IT IS
	// STANDING ON. The project owner played a build without one and reported the
	// numbers washing out against pale stone; the critical strike colour is amber
	// orange and the ordinary one a warm near-white, and both are light. Issue
	// #671.
	//
	// THE SAME ANSWER THE BARS ALREADY USE, and for the reason DrawBar states:
	// the design guarantees a world surface stays under 30% brightness, which is
	// enough for a large fill and says nothing about a few pixels of text seen
	// against Demonic lava or a Celestial wall. With an outline, the contrast is
	// a property of the text rather than of wherever it is drawn.
	//
	// EIGHT OFFSETS RATHER THAN FOUR. Four leaves the diagonals thin, and it
	// shows at this size. The cost is eight extra text draws per number, bounded
	// by UCataclysmCombatOverlay::MaxNumbersWaiting at 96 numbers, so the worst
	// frame draws 864 of them. That is a real number and it is why the ceiling
	// on waiting numbers exists.
	const float Spread = FMath::Max(1.0f, FMath::RoundToFloat(Sized));
	const FVector2D Offsets[] = {
		{ -Spread, -Spread }, { 0.0f, -Spread }, { Spread, -Spread },
		{ -Spread,    0.0f },                    { Spread,    0.0f },
		{ -Spread,  Spread }, { 0.0f,  Spread }, { Spread,  Spread },
	};

	FLinearColor Outline = FLinearColor::Black;
	Outline.A = Colour.A;

	for (const FVector2D& Offset : Offsets)
	{
		DrawText(Text, Outline, Left + Offset.X, Top + Offset.Y, Font, Sized);
	}

	DrawText(Text, Colour, Left, Top, Font, Sized);
}

void ACataclysmHUD::DrawPlayerPool(float Top, float Current, float Maximum,
								   const TCHAR* FillHex)
{
	const float Left = PlayerBarMarginPx;

	DrawBar(Left, Top, PlayerBarWidthPx, PlayerBarHeightPx,
			UCataclysmCombatOverlay::BarFractionFor(Current, Maximum),
			UCataclysmCombatOverlay::ColourFromHex(FillHex), 1.0f);

	// THE FIGURES AS WELL AS THE BAR, because the whole reason this exists is to
	// judge combat numbers. A bar answers "how close am I to dying" and the
	// figures answer "did that hit do what the design says". Issue #518 was
	// about the second one, and issue #653 showed the first is not enough on its
	// own either: a mana pool at zero and a mana pool at one look the same on a
	// bar that short, and the difference decides whether anything can be cast.
	//
	// AND THE FIGURES ARE NOT PRINTED HERE. UCataclysmCombatOverlay::PoolTextFor
	// is what builds them, because a pool with anything left in it must never
	// read zero and rounding alone breaks that. Issue #743: this printed
	// "0 / 500" for a character alive on a fraction of a point, which is the
	// one thing a health readout must not say about somebody still standing.
	DrawTextCentred(
		UCataclysmCombatOverlay::PoolTextFor(Current, Maximum),
		FLinearColor::White, Left + PlayerBarWidthPx * 0.5f, Top + 2.0f, 1.0f);
}

void ACataclysmHUD::DrawPlayerVitals()
{
	const APawn* Pawn = GetOwningPawn();
	if (!Pawn)
	{
		return;
	}

	float Health = 0.0f;
	float MaxHealth = 0.0f;
	if (!UCataclysmCombatOverlay::VitalsOf(Pawn, Health, MaxHealth))
	{
		// NO ABILITY SYSTEM YET IS A NORMAL FRAME, not a fault. The player's
		// lives on its player state, and on a client that arrives by
		// replication some frames after the pawn does.
		return;
	}

	// STACKED UPWARD FROM THE BOTTOM LEFT CORNER, health nearest the corner.
	// Health is the one a player looks at while being hit, so it is the one that
	// does not move when a bar above it appears or disappears.
	float Top = Canvas->SizeY - PlayerBarMarginPx - PlayerBarHeightPx;

	DrawPlayerPool(Top, Health, MaxHealth,
				   UCataclysmCombatOverlay::HealthFillHex);

	// MANA NEXT, AND ALWAYS. Every class has a mana pool -- the design's stat
	// table gives all three Demonic classes one -- so unlike the shield below
	// there is no case where the bar would be permanently empty and meaningless.
	//
	// IT IS HERE BECAUSE ITS ABSENCE HID A BUG. Issue #653: mana was spent and
	// never returned, so every ability became permanently refused, and it was
	// reported as "sometimes all of my abilities just become disabled" because
	// nothing on screen said the pool was empty.
	float Mana = 0.0f;
	float MaxMana = 0.0f;
	if (UCataclysmCombatOverlay::ManaOf(Pawn, Mana, MaxMana) && MaxMana > 0.0f)
	{
		Top -= PlayerBarHeightPx + PlayerBarGapPx;
		DrawPlayerPool(Top, Mana, MaxMana,
					   UCataclysmCombatOverlay::ManaFillHex);
	}

	// THE ENERGY SHIELD LAST AND ONLY WHEN THERE IS ONE. A class with no energy
	// shield is a design position rather than an error state --
	// UCataclysmVitalAttributeSet::PreAttributeChange says so -- and a bar
	// permanently at zero would say the opposite.
	float Shield = 0.0f;
	float MaxShield = 0.0f;
	if (UCataclysmCombatOverlay::ShieldOf(Pawn, Shield, MaxShield)
		&& MaxShield > 0.0f)
	{
		Top -= PlayerBarHeightPx + PlayerBarGapPx;
		DrawPlayerPool(Top, Shield, MaxShield,
					   UCataclysmCombatOverlay::ShieldFillHex);
	}
}

void ACataclysmHUD::DrawOverheadBars()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const AActor* LocalPawn = GetOwningPawn();

	// EVERY CHARACTER RATHER THAN EVERY ENEMY. ACataclysmCharacterBase covers
	// the enemy branch, both its subclasses, and a minion, which is a separate
	// branch off the same base. A minion is the player's rather than an
	// opponent, and a bar over one is the same answer to the same question:
	// something with health has been hurt. Last Epoch treats minions the same
	// way, with their own toggle beside enemies and players.
	for (TActorIterator<ACataclysmCharacterBase> It(World); It; ++It)
	{
		const ACataclysmCharacterBase* Character = *It;
		if (!UCataclysmCombatOverlay::IsOverheadBarCandidate(Character,
															 LocalPawn))
		{
			continue;
		}

		float Health = 0.0f;
		float MaxHealth = 0.0f;
		if (!UCataclysmCombatOverlay::VitalsOf(Character, Health, MaxHealth))
		{
			continue;
		}

		if (!UCataclysmCombatOverlay::ShouldShowBarFor(Health, MaxHealth))
		{
			continue;
		}

		const FVector Anchor = Character->GetActorLocation()
			+ FVector(0.0f, 0.0f,
					  UCataclysmCombatOverlay::AnchorHeightFor(Character));

		// THE Z TEST IS WHAT REJECTS ANYTHING BEHIND THE CAMERA, and it is the
		// part that matters. Either value of bClampToZeroPlane works: with it
		// true a point behind the camera comes back with Z forced to zero, and
		// with it false the engine's reversed-Z projection makes Z negative
		// there. False is passed only so the depth is the projection's own
		// answer rather than a clamped one. The engine's own
		// AHUD::GetActorsInSelectionRectangle uses the other combination.
		const FVector Screen = Project(Anchor, /*bClampToZeroPlane=*/false);
		if (Screen.Z <= 0.0f)
		{
			continue;
		}

		DrawBar(Screen.X - OverheadBarWidthPx * 0.5f, Screen.Y,
				OverheadBarWidthPx, OverheadBarHeightPx,
				UCataclysmCombatOverlay::BarFractionFor(Health, MaxHealth),
				UCataclysmCombatOverlay::ColourFromHex(
					UCataclysmCombatOverlay::HealthFillHex),
				1.0f);
	}
}

void ACataclysmHUD::DrawRarityNames()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ENEMIES ONLY, AND NOT EVERY CHARACTER. DrawOverheadBars walks
	// ACataclysmCharacterBase because a minion has health too; a minion has no
	// rarity, so this walks the enemy branch instead.
	const AActor* LocalPawn = GetOwningPawn();

	// LOADED ONCE FOR THE WHOLE WALK. A creature fetching its own would repeat
	// the lookup for every enemy on screen, every frame, for a table that cannot
	// change while the level is loaded.
	const UDataTable* Rarities = UCataclysmEnemyRarity::LoadEnemyRarityTable();

	const FLinearColor Ink = UCataclysmCombatOverlay::ColourFromHex(
		UCataclysmCombatOverlay::RarityNameHex);

	for (TActorIterator<ACataclysmEnemyCharacter> It(World); It; ++It)
	{
		const ACataclysmEnemyCharacter* Enemy = *It;
		if (!UCataclysmCombatOverlay::IsOverheadBarCandidate(Enemy, LocalPawn))
		{
			continue;
		}

		float Health = 0.0f;
		float MaxHealth = 0.0f;
		if (!UCataclysmCombatOverlay::VitalsOf(Enemy, Health, MaxHealth))
		{
			continue;
		}

		if (!UCataclysmCombatOverlay::ShouldShowRarityNameFor(
				Enemy->RarityStep, Health, MaxHealth))
		{
			continue;
		}

		const FString Word =
			UCataclysmEnemyRarity::RarityNameForStep(Rarities, Enemy->RarityStep);
		if (Word.IsEmpty())
		{
			// A RUNG THE TABLE DOES NOT NAME DRAWS NOTHING, rather than an empty
			// outline over the creature's head.
			continue;
		}

		const FVector Anchor = Enemy->GetActorLocation()
			+ FVector(0.0f, 0.0f,
					  UCataclysmCombatOverlay::AnchorHeightFor(Enemy));

		// THE Z TEST IS WHAT REJECTS ANYTHING BEHIND THE CAMERA, the same test
		// and the same reasoning as DrawOverheadBars.
		const FVector Screen = Project(Anchor, /*bClampToZeroPlane=*/false);
		if (Screen.Z <= 0.0f)
		{
			continue;
		}

		// MEASURED BEFORE IT IS DRAWN, because DrawTextCentred places the TOP of
		// the text at the height it is given, and this word has to sit above the
		// bar rather than start where the bar starts.
		const FBox2D Measured = MeasureTextCentred(
			Word, Screen.X, 0.0f, UCataclysmCombatOverlay::RarityNameScale);
		const float Height = static_cast<float>(Measured.Max.Y - Measured.Min.Y);

		DrawTextCentred(Word, Ink, Screen.X,
						Screen.Y - Height
							- UCataclysmCombatOverlay::RarityNameGapPx
							- BarBackingInsetPx,
						UCataclysmCombatOverlay::RarityNameScale);
	}
}

const ACataclysmEnemyCharacter* ACataclysmHUD::CreatureUnderCursor() const
{
	const ACataclysmPlayerController* Player =
		Cast<ACataclysmPlayerController>(GetOwningPlayerController());
	if (!Player)
	{
		return nullptr;
	}

	// A CURSOR OVER AN OPEN SCREEN POINTS AT NOTHING IN THE WORLD. See the
	// header: without this a cursor resting on an inventory cell would describe
	// whatever creature happens to stand behind the panel.
	if (Player->CursorIsOverInterface())
	{
		return nullptr;
	}

	// PAWN OBJECTS RATHER THAN THE VISIBILITY CHANNEL, which is the whole trick
	// and is explained on the header. The engine's stock Pawn and CharacterMesh
	// collision profiles both set Visibility to Ignore, so a visibility trace
	// goes straight through every creature in the game.
	TArray<TEnumAsByte<EObjectTypeQuery>> Creatures;
	Creatures.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	FHitResult Hit;
	if (!Player->GetHitResultUnderCursorForObjects(
			Creatures, /*bTraceComplex=*/false, Hit))
	{
		return nullptr;
	}

	// A MINION AND THE PLAYER ARE PAWNS TOO, and neither is described here: a
	// minion has no rarity and no archetype, and the player's own health is on
	// the frame already. The cast is what excludes them.
	return Cast<ACataclysmEnemyCharacter>(Hit.GetActor());
}

void ACataclysmHUD::DrawCreaturePanel()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// POINTING AT A CREATURE RESTARTS THE CLOCK, and pointing at a DIFFERENT
	// creature replaces the one being described on the same frame. So moving
	// between two enemies in a pack never shows the wrong one, and only
	// pointing at nothing leaves the last one on screen.
	if (const ACataclysmEnemyCharacter* Pointed = CreatureUnderCursor())
	{
		Described = Pointed;
		DescribedAt = Now;
	}

	// GET() RATHER THAN A RAW POINTER, because the creature may have been
	// destroyed since the cursor was on it. A weak pointer answers nullptr.
	const ACataclysmEnemyCharacter* Creature = Described.Get();
	if (!Creature)
	{
		return;
	}

	// AND IT DOES NOT GO THE MOMENT THE CURSOR DOES. See
	// UCataclysmCreaturePanel::StillDescribed for why the panel is held on
	// screen after the cursor has left the creature.
	if (!UCataclysmCreaturePanel::StillDescribed(Now - DescribedAt))
	{
		Described.Reset();
		return;
	}

	float Health = 0.0f;
	float MaxHealth = 0.0f;
	if (!UCataclysmCombatOverlay::VitalsOf(Creature, Health, MaxHealth))
	{
		return;
	}

	if (!UCataclysmCreaturePanel::ShouldShowFor(Creature, GetOwningPawn(),
											   Health, MaxHealth))
	{
		return;
	}

	// THE WORDS FIRST, ALL OF THEM, before anything is measured. Each comes out
	// of a generated table rather than out of this file, so a creature or a rung
	// renamed in the design workbook is renamed on screen.
	const FString Title = UCataclysmCreaturePanel::TitleFor(
		UCataclysmCreaturePanel::ArchetypeNameForRow(
			UCataclysmCreaturePanel::LoadEnemyArchetypeTable(),
			Creature->ArchetypeRow),
		UCataclysmEnemyRarity::RarityNameForStep(
			UCataclysmEnemyRarity::LoadEnemyRarityTable(),
			Creature->RarityStep));

	const FString HealthText =
		UCataclysmCombatOverlay::PoolTextFor(Health, MaxHealth);

	TArray<FString> Modifiers;
	UCataclysmCreaturePanel::ModifierNamesFor(
		UCataclysmCreaturePanel::LoadEnemyModifierTable(),
		Creature->ModifierRows, Modifiers);

	// MEASURED IN FULL BEFORE ANYTHING IS DRAWN, because the panel is as wide as
	// the widest line inside it. The X and Y passed here are throwaway: only the
	// width and height of each measurement are used, and the real positions are
	// worked out once the panel's box is known.
	const FBox2D TitleBox = MeasureTextCentred(
		Title, 0.0f, 0.0f, UCataclysmCreaturePanel::TitleScale);
	const FBox2D FiguresBox = MeasureTextCentred(
		HealthText, 0.0f, 0.0f, UCataclysmCreaturePanel::LineScale);

	const float TitleHeight = static_cast<float>(TitleBox.Max.Y - TitleBox.Min.Y);
	const float FiguresHeight =
		static_cast<float>(FiguresBox.Max.Y - FiguresBox.Min.Y);

	// THE HEALTH ROW IS AS TALL AS THE TALLER OF THE TWO THINGS IN IT. The
	// figures are drawn centred ON the bar, the way the player's own pools are
	// drawn, and the text is taller than the bar at every size this uses -- so
	// sizing the row to the bar alone would clip the figures against the
	// panel's edge.
	const float HealthRowHeight = FMath::Max(
		UCataclysmCreaturePanel::HealthBarHeightPx, FiguresHeight);

	float ContentWidth = FMath::Max(
		static_cast<float>(TitleBox.Max.X - TitleBox.Min.X),
		static_cast<float>(FiguresBox.Max.X - FiguresBox.Min.X));
	float ContentHeight =
		TitleHeight + UCataclysmCreaturePanel::LineGapPx + HealthRowHeight;

	TArray<FBox2D> ModifierBoxes;
	ModifierBoxes.Reserve(Modifiers.Num());
	for (const FString& Name : Modifiers)
	{
		const FBox2D Box = MeasureTextCentred(
			Name, 0.0f, 0.0f, UCataclysmCreaturePanel::LineScale);
		ModifierBoxes.Add(Box);

		ContentWidth = FMath::Max(
			ContentWidth, static_cast<float>(Box.Max.X - Box.Min.X));
		ContentHeight += UCataclysmCreaturePanel::LineGapPx
			+ static_cast<float>(Box.Max.Y - Box.Min.Y);
	}

	const FBox2D Panel = UCataclysmCreaturePanel::PanelBoxFor(
		FVector2D(Canvas->SizeX, Canvas->SizeY), ContentWidth, ContentHeight);

	FLinearColor Fill = UCataclysmCombatOverlay::ColourFromHex(
		UCataclysmCreaturePanel::PanelHex);
	Fill.A = UCataclysmCreaturePanel::PanelOpacity;
	DrawRect(Fill, static_cast<float>(Panel.Min.X),
			 static_cast<float>(Panel.Min.Y),
			 static_cast<float>(Panel.Max.X - Panel.Min.X),
			 static_cast<float>(Panel.Max.Y - Panel.Min.Y));

	// THE EDGE, WHICH IS THE ONLY THING THAT MAKES THE PANEL A SHAPE. The fill
	// is near-black so the text stays readable, and a near-black panel over a
	// floor at the brightness the design caps a world surface at measures
	// 1.86:1 -- so on pale stone the fill alone is very nearly invisible.
	// UCataclysmCreaturePanel::EdgeHex carries the measurements.
	DrawBorder(Panel, UCataclysmCreaturePanel::EdgePx,
			   UCataclysmCombatOverlay::ColourFromHex(
				   UCataclysmCreaturePanel::EdgeHex));

	const FLinearColor Ink = UCataclysmCombatOverlay::ColourFromHex(
		UCataclysmCreaturePanel::InkHex);

	const float CentreX =
		static_cast<float>(Panel.Min.X + Panel.Max.X) * 0.5f;
	const float InnerLeft =
		static_cast<float>(Panel.Min.X) + UCataclysmCreaturePanel::PaddingPx;
	const float InnerWidth = static_cast<float>(Panel.Max.X - Panel.Min.X)
		- UCataclysmCreaturePanel::PaddingPx * 2.0f;

	float Top = static_cast<float>(Panel.Min.Y)
		+ UCataclysmCreaturePanel::PaddingPx;

	DrawTextCentred(Title, Ink, CentreX, Top,
					UCataclysmCreaturePanel::TitleScale);
	Top += TitleHeight + UCataclysmCreaturePanel::LineGapPx;

	// THE BAR SITS IN THE MIDDLE OF ITS ROW and the figures sit in the middle of
	// the bar, so the two stay lined up whichever of them is the taller.
	const float BarTop = Top
		+ (HealthRowHeight - UCataclysmCreaturePanel::HealthBarHeightPx) * 0.5f;

	// AN OUTLINE AND A FILL RATHER THAN DrawBar, WHICH IS A TRACK AND A FILL.
	// DrawBar paints its dark backing in UCataclysmCombatOverlay::BarBackingHex,
	// which is the same near-black this panel is, so inside the panel the
	// backing would be invisible and the bar would have no visible extent at
	// all. See UCataclysmCreaturePanel::BarOutlinePx for why a lighter track
	// cannot be used instead: there is no grey that separates from both the
	// panel and the health red.
	DrawRect(UCataclysmCombatOverlay::ColourFromHex(
				 UCataclysmCombatOverlay::HealthFillHex),
			 InnerLeft, BarTop,
			 InnerWidth * UCataclysmCombatOverlay::BarFractionFor(Health,
																  MaxHealth),
			 UCataclysmCreaturePanel::HealthBarHeightPx);

	DrawBorder(FBox2D(FVector2D(InnerLeft, BarTop),
					  FVector2D(InnerLeft + InnerWidth,
								BarTop + UCataclysmCreaturePanel::HealthBarHeightPx)),
			   UCataclysmCreaturePanel::BarOutlinePx,
			   UCataclysmCombatOverlay::ColourFromHex(
				   UCataclysmCreaturePanel::EdgeHex));

	DrawTextCentred(HealthText, Ink, CentreX,
					Top + (HealthRowHeight - FiguresHeight) * 0.5f,
					UCataclysmCreaturePanel::LineScale);
	Top += HealthRowHeight;

	// THE MODIFIERS LAST, WHICH IS THE REASON THIS PANEL EXISTS. There will be
	// none of them until something grants them, which is issue #742; see
	// ACataclysmEnemyCharacter::ModifierRows, which is what lets a creature
	// placed in a level be given some by hand in the meantime.
	for (int32 Index = 0; Index < ModifierBoxes.Num(); ++Index)
	{
		Top += UCataclysmCreaturePanel::LineGapPx;
		DrawTextCentred(Modifiers[Index], Ink, CentreX, Top,
						UCataclysmCreaturePanel::LineScale);
		Top += static_cast<float>(
			ModifierBoxes[Index].Max.Y - ModifierBoxes[Index].Min.Y);
	}
}

void ACataclysmHUD::DrawDropNames()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// EVERY DROP IN THE WORLD, ASKED FOR EACH FRAME rather than kept in a list
	// here. A drop appears when something dies and goes away when it is picked
	// up, and neither of those happens through this class; keeping a copy would
	// be a second record of what is on the floor and a way for the two to
	// disagree.
	// REBUILT EVERY FRAME, because a name that was not drawn this frame is not
	// clickable this frame. A drop behind the camera, or one just picked up, has
	// to leave this list or a click at its old position would still find it.
	DropNameRects.Reset();
	DropsNamed.Reset();

	// MEASURED FIRST AND DRAWN AFTERWARDS, in two passes over the same list,
	// because where a name goes depends on where the other names went. Drawing
	// as they were found would print the first one before it was known that the
	// third would land on top of it.
	for (TActorIterator<ACataclysmDroppedItem> It(World); It; ++It)
	{
		ACataclysmDroppedItem* Drop = *It;
		if (!Drop || Drop->DisplayName.IsEmpty())
		{
			continue;
		}

		const FVector Anchor = Drop->GetActorLocation()
			+ FVector(0.0f, 0.0f, ACataclysmDroppedItem::NameHeightCm);

		// THE Z TEST IS WHAT REJECTS ANYTHING BEHIND THE CAMERA, the same test
		// and the same reasoning as DrawOverheadBars above.
		const FVector Screen = Project(Anchor, /*bClampToZeroPlane=*/false);
		if (Screen.Z <= 0.0f)
		{
			continue;
		}

		// THE WHOLE TAG IS RECORDED, NOT JUST THE TEXT. TagAround grows the
		// text by its padding and its rarity's border thickness, so a
		// Cataclysmic tag is 11 pixels bigger on every side than its letters.
		// Recording only the text would let two tags print over each other
		// while their texts did not, and would leave a click on the border
		// finding nothing.
		//
		// THE TWO ARRAYS ARE APPENDED TOGETHER AND NOWHERE ELSE, which is what
		// keeps index N of one describing the same drop as index N of the other.
		const FBox2D Text = MeasureTextCentred(Drop->DisplayName, Screen.X,
											   Screen.Y, 1.0f);
		DropNameRects.Add(UCataclysmDropPickup::TagAround(
			Text, UCataclysmDropPickup::NameBorderThicknessOf(*Drop)));
		DropsNamed.Add(Drop);
	}

	// NOTHING IS DRAWN YET, so a tag that would have sat on top of another can
	// still be moved. The rectangles that move are the same ones a click is
	// tested against, so a name stays clickable wherever it ends up.
	UCataclysmDropPickup::SeparateOverlappingNames(
		DropNameRects, UCataclysmDropPickup::NameGapPx);

	for (int32 Index = 0; Index < DropNameRects.Num(); ++Index)
	{
		const ACataclysmDroppedItem* Drop = DropsNamed[Index].Get();
		if (!Drop)
		{
			continue;
		}

		const FBox2D& Tag = DropNameRects[Index];
		const int32 Thickness =
			UCataclysmDropPickup::NameBorderThicknessOf(*Drop);

		// THE BORDER IS THE SECOND CHANNEL, and it is drawn before the text so
		// the letters sit on top of it rather than under it.
		DrawBorder(Tag, static_cast<float>(Thickness), Drop->NameColour);

		// THE TEXT SITS INSIDE THE BORDER AND ITS PADDING. TagAround grew the
		// text by exactly this much on every side, so undoing it here puts the
		// letters back where they were measured.
		const float Inset = static_cast<float>(
			UCataclysmDropPickup::NameBorderPaddingPx + Thickness);
		DrawOutlinedText(Drop->DisplayName, Drop->NameColour,
						 Tag.Min.X + Inset, Tag.Min.Y + Inset, 1.0f);
	}
}

void ACataclysmHUD::DrawBorder(const FBox2D& Around, float Thickness,
							   const FLinearColor& Colour)
{
	if (!Around.bIsValid || Thickness <= 0.0f)
	{
		return;
	}

	// A BLACK BAND ONE PIXEL WIDER ON EACH SIDE, DRAWN FIRST. See the header for
	// why the border needs its own contrast rather than relying on the floor.
	FLinearColor Edge = FLinearColor::Black;
	Edge.A = Colour.A;

	const auto Hollow = [this](const FBox2D& Box, float Deep,
							   const FLinearColor& Paint)
	{
		const float Width = static_cast<float>(Box.Max.X - Box.Min.X);
		const float Height = static_cast<float>(Box.Max.Y - Box.Min.Y);
		const float Left = static_cast<float>(Box.Min.X);
		const float Top = static_cast<float>(Box.Min.Y);

		DrawRect(Paint, Left, Top, Width, Deep);
		DrawRect(Paint, Left, Top + Height - Deep, Width, Deep);
		DrawRect(Paint, Left, Top, Deep, Height);
		DrawRect(Paint, Left + Width - Deep, Top, Deep, Height);
	};

	Hollow(FBox2D(Around.Min - FVector2D(1.0, 1.0),
				  Around.Max + FVector2D(1.0, 1.0)), Thickness + 2.0f, Edge);
	Hollow(Around, Thickness, Colour);
}

ACataclysmDroppedItem* ACataclysmHUD::DropUnderPoint(const FVector2D& Point) const
{
	const int32 Index =
		UCataclysmDropPickup::IndexOfNameUnderPoint(DropNameRects, Point);
	if (Index == INDEX_NONE || !DropsNamed.IsValidIndex(Index))
	{
		return nullptr;
	}

	// GET() RATHER THAN A RAW POINTER, because the drop may have been destroyed
	// since it was drawn. A weak pointer answers nullptr; a raw one would not.
	return DropsNamed[Index].Get();
}

void ACataclysmHUD::DrawDamageNumbers()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	for (const FCataclysmDamageNumber& Number : Numbers)
	{
		const float Age = Now - Number.StartedAt;
		const float Opacity = UCataclysmCombatOverlay::FadeFor(Age);
		if (Opacity <= 0.0f)
		{
			continue;
		}

		const FVector Screen =
			Project(Number.WorldAnchor, /*bClampToZeroPlane=*/false);
		if (Screen.Z <= 0.0f)
		{
			continue;
		}

		// IT RISES IN SCREEN PIXELS RATHER THAN IN CENTIMETRES. A world-space
		// rise looks different depending on how far away the blow landed and how
		// far the camera is zoomed out, and the mouse wheel zooms this camera.
		// A pixel rise looks the same everywhere, which is what makes several
		// numbers at different distances readable at once.
		const float Top = Screen.Y - UCataclysmCombatOverlay::RisePixelsFor(Age);

		FLinearColor Colour = Number.Colour;
		Colour.A = Opacity;

		DrawTextCentred(Number.Text, Colour, Screen.X, Top, Number.Scale);
	}
}
