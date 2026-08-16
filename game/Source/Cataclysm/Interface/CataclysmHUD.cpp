// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmHUD.h"
#include "Character/CataclysmCharacterBase.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"

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

	DrawDamageNumbers();
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
	// falls back to this same font when handed null, and this is asked for
	// explicitly only so GetTextSize measures the same font that DrawText will
	// draw with. A mismatch there would centre text against the wrong width.
	return GEngine ? GEngine->GetMediumFont() : nullptr;
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

void ACataclysmHUD::DrawTextCentred(const FString& Text,
									const FLinearColor& Colour, float CentreX,
									float TopY, float Scale)
{
	UFont* Font = OverlayFont();

	float Width = 0.0f;
	float Height = 0.0f;
	GetTextSize(Text, Width, Height, Font, Scale);

	DrawText(Text, Colour, CentreX - Width * 0.5f, TopY, Font, Scale);
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

	const float Left = PlayerBarMarginPx;
	float Top = Canvas->SizeY - PlayerBarMarginPx - PlayerBarHeightPx;

	// THE ENERGY SHIELD SITS ABOVE HEALTH AND ONLY WHEN THERE IS ONE. A class
	// with no energy shield is a design position rather than an error state --
	// UCataclysmVitalAttributeSet::PreAttributeChange says so -- and a bar
	// permanently at zero would say the opposite.
	float Shield = 0.0f;
	float MaxShield = 0.0f;
	if (UCataclysmCombatOverlay::ShieldOf(Pawn, Shield, MaxShield)
		&& MaxShield > 0.0f)
	{
		const float ShieldTop = Top - PlayerBarHeightPx - 6.0f;
		DrawBar(Left, ShieldTop, PlayerBarWidthPx, PlayerBarHeightPx,
				UCataclysmCombatOverlay::BarFractionFor(Shield, MaxShield),
				UCataclysmCombatOverlay::ColourFromHex(
					UCataclysmCombatOverlay::ShieldFillHex),
				1.0f);

		DrawTextCentred(
			FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Shield),
							FMath::RoundToInt(MaxShield)),
			FLinearColor::White, Left + PlayerBarWidthPx * 0.5f,
			ShieldTop + 2.0f, 1.0f);
	}

	DrawBar(Left, Top, PlayerBarWidthPx, PlayerBarHeightPx,
			UCataclysmCombatOverlay::BarFractionFor(Health, MaxHealth),
			UCataclysmCombatOverlay::ColourFromHex(
				UCataclysmCombatOverlay::HealthFillHex),
			1.0f);

	// THE FIGURES AS WELL AS THE BAR, because the whole reason this exists is to
	// judge combat numbers. A bar answers "how close am I to dying" and the
	// figures answer "did that hit do what the design says", and issue #518 is
	// about the second one.
	DrawTextCentred(
		FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Health),
						FMath::RoundToInt(MaxHealth)),
		FLinearColor::White, Left + PlayerBarWidthPx * 0.5f, Top + 2.0f, 1.0f);
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

		// bClampToZeroPlane IS FALSE SO THE DEPTH SURVIVES. With it true, a
		// point behind the camera comes back with Z forced to zero and its X
		// and Y left alone, which draws a bar for a creature standing behind
		// the player as though it were in front of them.
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
