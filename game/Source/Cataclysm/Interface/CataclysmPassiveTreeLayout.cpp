// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmPassiveTreeLayout.h"
#include "Character/CataclysmPassiveTree.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"

FCataclysmTreeExtent UCataclysmPassiveTreeLayout::ExtentOf(
	const UDataTable* NodeTable, const FString& Tree)
{
	FCataclysmTreeExtent Extent;
	if (!NodeTable || Tree.IsEmpty())
	{
		return Extent;
	}

	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		const FCataclysmPassiveNodeRow* Row =
			reinterpret_cast<const FCataclysmPassiveNodeRow*>(Pair.Value);
		if (!Row || Row->Tree != Tree)
		{
			continue;
		}

		const FVector2D At(Row->PositionX, Row->PositionY);
		if (!Extent.bAny)
		{
			Extent.Least = At;
			Extent.Most = At;
			Extent.bAny = true;
			continue;
		}

		Extent.Least.X = FMath::Min(Extent.Least.X, At.X);
		Extent.Least.Y = FMath::Min(Extent.Least.Y, At.Y);
		Extent.Most.X = FMath::Max(Extent.Most.X, At.X);
		Extent.Most.Y = FMath::Max(Extent.Most.Y, At.Y);
	}

	return Extent;
}

ECataclysmTreeMark UCataclysmPassiveTreeLayout::MarkFor(const FString& Kind)
{
	// THE TREE FILES' OWN WORDS, the ones `UCataclysmPassiveTree` reads: a vow is a "capstone" there.
	if (Kind == UCataclysmPassiveTree::KeystoneKind)
	{
		return ECataclysmTreeMark::Keystone;
	}
	if (Kind == UCataclysmPassiveTree::CapstoneKind)
	{
		return ECataclysmTreeMark::Vow;
	}
	return ECataclysmTreeMark::Ordinary;
}

FVector2D UCataclysmPassiveTreeLayout::MarkSizePx(ECataclysmTreeMark Mark)
{
	switch (Mark)
	{
	case ECataclysmTreeMark::Keystone:
		return FVector2D(KeystoneMarkPx, KeystoneMarkPx);
	case ECataclysmTreeMark::Vow:
		return FVector2D(VowMarkWidthPx, VowMarkHeightPx);
	case ECataclysmTreeMark::Ordinary:
	default:
		return FVector2D(OrdinaryMarkPx, OrdinaryMarkPx);
	}
}

FVector2D UCataclysmPassiveTreeLayout::MarkSizeForNode(const UDataTable* NodeTable, FName Node)
{
	const FCataclysmPassiveNodeRow* Row = UCataclysmPassiveTree::FindNode(NodeTable, Node);
	return MarkSizePx(Row ? MarkFor(Row->Kind) : ECataclysmTreeMark::Ordinary);
}

float UCataclysmPassiveTreeLayout::MarkScaleFor(const UDataTable* NodeTable, const FString& Tree)
{
	if (!NodeTable || Tree.IsEmpty())
	{
		return 1.0f;
	}

	// EACH NODE'S POSITION AND ITS OWN MARK'S SIZE, since a keystone's or a vow's mark needs more room than an
	// ordinary one.
	TArray<FVector2D> Points;
	TArray<FVector2D> Sizes;
	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		const FCataclysmPassiveNodeRow* Row = reinterpret_cast<const FCataclysmPassiveNodeRow*>(Pair.Value);
		if (Row && Row->Tree == Tree)
		{
			Points.Add(FVector2D(Row->PositionX, Row->PositionY));
			Sizes.Add(MarkSizePx(MarkFor(Row->Kind)));
		}
	}

	// ONE NODE CANNOT OVERLAP ANYTHING.
	if (Points.Num() < 2)
	{
		return 1.0f;
	}

	// EVERY PAIR, ABOUT 2,700 OF THEM FOR A 74-NODE TREE: work for the moment a tree is opened, not for a frame.
	float Allowed = 1.0f;
	for (int32 First = 0; First < Points.Num(); ++First)
	{
		for (int32 Second = First + 1; Second < Points.Num(); ++Second)
		{
			const FVector2D Apart = (Points[First] - Points[Second]).GetAbs();
			const FVector2D Needed = (Sizes[First] + Sizes[Second]) * 0.5;

			// TWO MARKS MISS EACH OTHER WHEN THEY ARE CLEAR ON EITHER AXIS, so this pair permits the LARGER of what
			// each axis allows.
			const float ByThisPair = FMath::Max(static_cast<float>(Apart.X / Needed.X),
												static_cast<float>(Apart.Y / Needed.Y));
			Allowed = FMath::Min(Allowed, ByThisPair);
		}
	}

	// A FLOOR, BECAUSE TWO NODES ON EXACTLY THE SAME POINT PERMIT NOTHING, and a mark of no size could not be clicked.
	return FMath::Clamp(Allowed, 0.05f, 1.0f);
}

FString UCataclysmPassiveTreeLayout::MarkCaption(int32 Points, int32 MaxPoints)
{
	// NOTHING ON A NODE HOLDING NO POINTS, so the marks of an unspent tree stay plain and its shape reads; a spent node
	// keeps its count readable without a hover.
	return Points > 0 ? FString::Printf(TEXT("%d/%d"), Points, MaxPoints) : FString();
}

FString UCataclysmPassiveTreeLayout::NameCaption(const FString& Name, int32 Points, int32 MaxPoints)
{
	// THE TEXT A NODE'S BOX HELD UNTIL 2026-11-24, now drawn beside the mark of the node hovered or selected.
	return FString::Printf(TEXT("%s  %d/%d"), *Name, Points, MaxPoints);
}

FVector2D UCataclysmPassiveTreeLayout::NameCaptionPosition(FVector2D MarkCentre, FVector2D MarkSize)
{
	return FVector2D(MarkCentre.X + MarkSize.X * 0.5 + CaptionGapPx, MarkCentre.Y);
}

float UCataclysmPassiveTreeLayout::ZoomToFit(const FCataclysmTreeExtent& Extent,
											 FVector2D CanvasSize)
{
	if (!Extent.bAny)
	{
		return ClampZoom(1.0f);
	}

	// WHAT IS LEFT ONCE THE MARGIN HAS BEEN TAKEN OFF BOTH SIDES. A node is
	// drawn centred on its position, so without the margin half of the outermost
	// node on each side would sit off the panel.
	const FVector2D Room(CanvasSize.X - FitMarginPx * 2.0,
						 CanvasSize.Y - FitMarginPx * 2.0);

	// A PANEL WITH NO ROOM LEFT IS NOT THE SAME AS A TREE WITH NO SIZE, and
	// reading them as one thing was a real fault a test caught. A panel smaller
	// than twice the margin leaves NEGATIVE room, and dividing by it gives a
	// negative zoom, which mirrors the whole tree about both axes. The smallest
	// zoom is the honest answer: the tree cannot fit, so show as much as
	// possible.
	if (Room.X <= 0.0 || Room.Y <= 0.0)
	{
		return SmallestZoom;
	}

	const FVector2D Span = Extent.Span();

	// A TREE WITH NO WIDTH OR NO HEIGHT IS A DIFFERENT CASE ENTIRELY. All four
	// real trees have both, but a tree of one node has neither, and there is no
	// scale at which one point fails to fit -- so that axis does not bind and
	// the other one decides.
	const float ByWidth = Span.X > 0.0
		? static_cast<float>(Room.X / Span.X) : LargestZoom;
	const float ByHeight = Span.Y > 0.0
		? static_cast<float>(Room.Y / Span.Y) : LargestZoom;

	// THE SMALLER OF THE TWO, so the tree fits both ways rather than one.
	return ClampZoom(FMath::Min(ByWidth, ByHeight));
}

FVector2D UCataclysmPassiveTreeLayout::ScreenPositionFor(FVector2D TreePosition,
														 FVector2D Focus,
														 float Zoom,
														 FVector2D CanvasSize)
{
	// THE FOCUS LANDS IN THE MIDDLE OF THE PANEL, whatever the zoom. That is
	// what makes zooming keep looking at the same thing rather than drifting
	// towards a corner.
	return CanvasSize * 0.5 + (TreePosition - Focus) * Zoom;
}

FVector2D UCataclysmPassiveTreeLayout::TreePositionFor(FVector2D ScreenPosition,
													   FVector2D Focus,
													   float Zoom,
													   FVector2D CanvasSize)
{
	const float Safe = ClampZoom(Zoom);
	return Focus + (ScreenPosition - CanvasSize * 0.5) / Safe;
}

void UCataclysmPassiveTreeLayout::EdgeGeometry(FVector2D From, FVector2D To,
											   float Thickness,
											   FVector2D& OutPosition,
											   FVector2D& OutSize,
											   float& OutAngle)
{
	const FVector2D Along = To - From;
	const double Length = Along.Size();

	OutPosition = From;
	OutSize = FVector2D(Length, FMath::Max(1.0f, Thickness));

	// DEGREES CLOCKWISE FROM POINTING RIGHT, which is what
	// `SetRenderTransformAngle` wants, and it comes out of `Atan2` directly
	// because screen y grows downwards. Getting the sign wrong mirrors every
	// edge about the horizontal and the tree looks plausible upside down.
	//
	// TWO POINTS IN THE SAME PLACE HAVE NO DIRECTION, and `Atan2(0, 0)` is zero
	// rather than an error, so a zero-length edge is drawn as nothing pointing
	// right rather than as a fault.
	OutAngle = Length > 0.0
		? FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(Along.Y, Along.X)))
		: 0.0f;
}

float UCataclysmPassiveTreeLayout::ClampZoom(float Zoom)
{
	// NOT-A-NUMBER IS CLAMPED TO THE SMALLEST RATHER THAN PASSED THROUGH.
	// FMath::Clamp compares, and every comparison against a not-a-number is
	// false, so it would answer the value it was given and every node would be
	// placed nowhere.
	if (FMath::IsNaN(Zoom))
	{
		return SmallestZoom;
	}

	// AN INFINITE ZOOM IS CLAMPED BY ITS SIGN, and reading it as a not-a-number
	// was a real fault a test caught. Scrolling in a thousand notches raises
	// 1.15 to the thousandth power, which overflows to positive infinity, and
	// answering the SMALLEST zoom for that would send a player who kept
	// scrolling in all the way out instead.
	if (!FMath::IsFinite(Zoom))
	{
		return Zoom > 0.0f ? LargestZoom : SmallestZoom;
	}

	return FMath::Clamp(Zoom, SmallestZoom, LargestZoom);
}

float UCataclysmPassiveTreeLayout::ZoomAfterNotches(float Zoom, float Notches)
{
	// A FACTOR PER NOTCH RATHER THAN AN AMOUNT. Adding a fixed step makes one
	// notch nothing at all when zoomed out and an enormous jump when zoomed in;
	// multiplying makes every notch the same size to look at, which is what
	// every application with a zoom does.
	return ClampZoom(ClampZoom(Zoom) * FMath::Pow(ZoomPerNotch, Notches));
}
