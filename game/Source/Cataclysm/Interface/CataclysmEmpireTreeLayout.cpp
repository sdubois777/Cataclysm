// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireTreeLayout.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Player/CataclysmEmpireTree.h"

const FString UCataclysmEmpireTreeLayout::WholeTreeView = TEXT("Whole tree");

TArray<FString> UCataclysmEmpireTreeLayout::Views()
{
	TArray<FString> Out = {WholeTreeView};
	Out.Append(UCataclysmEmpireTree::BranchNames());
	return Out;
}

FString UCataclysmEmpireTreeLayout::ViewLabel(const FString& View)
{
	if (View == WholeTreeView)
	{
		return TEXT("The whole tree");
	}
	return View == UCataclysmEmpireTree::CentreBranch ? FString(TEXT("The centre")) : View;
}

FString UCataclysmEmpireTreeLayout::ViewButtonLabel(const FString& View, int32 SpentInView)
{
	return FString::Printf(TEXT("%s  %d point%s"), *ViewLabel(View), SpentInView, SpentInView == 1 ? TEXT("") : TEXT("s"));
}

FCataclysmTreeExtent UCataclysmEmpireTreeLayout::ExtentOf(const UDataTable* NodeTable, const FString& View)
{
	FCataclysmTreeExtent Extent;
	if (!NodeTable || View.IsEmpty())
	{
		return Extent;
	}

	const bool bWhole = View == WholeTreeView;
	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		const FCataclysmEmpireTreeNodeRow* Row = reinterpret_cast<const FCataclysmEmpireTreeNodeRow*>(Pair.Value);
		if (!Row || (!bWhole && Row->Branch != View))
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

FCataclysmTreeExtent UCataclysmEmpireTreeLayout::WithDecorations(const FCataclysmTreeExtent& Extent,
																 const UDataTable* DecorationTable)
{
	FCataclysmTreeExtent Out = Extent;
	if (!DecorationTable)
	{
		return Out;
	}

	const auto Hold = [&Out](FVector2D At)
	{
		if (!Out.bAny)
		{
			Out.Least = At;
			Out.Most = At;
			Out.bAny = true;
			return;
		}
		Out.Least.X = FMath::Min(Out.Least.X, At.X);
		Out.Least.Y = FMath::Min(Out.Least.Y, At.Y);
		Out.Most.X = FMath::Max(Out.Most.X, At.X);
		Out.Most.Y = FMath::Max(Out.Most.Y, At.Y);
	};

	for (const TPair<FName, uint8*>& Pair : DecorationTable->GetRowMap())
	{
		const FCataclysmEmpireTreeDecorationRow* Row = reinterpret_cast<const FCataclysmEmpireTreeDecorationRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}
		const FVector2D Centre(Row->CentreX, Row->CentreY);
		if (Row->Kind == TEXT("circle"))
		{
			// A RING'S BOX, the square it is drawn in.
			Hold(Centre - FVector2D(Row->Radius, Row->Radius));
			Hold(Centre + FVector2D(Row->Radius, Row->Radius));
		}
		else if (Row->Kind == TEXT("line"))
		{
			Hold(Centre);
			Hold(LineEnd(*Row));
		}
		else
		{
			Hold(Centre);
		}
	}
	return Out;
}

FVector2D UCataclysmEmpireTreeLayout::MarkSizeFor(const FCataclysmEmpireTreeNodeRow& Row)
{
	return UCataclysmPassiveTreeLayout::MarkSizePx(UCataclysmPassiveTreeLayout::MarkFor(Row.Kind));
}

float UCataclysmEmpireTreeLayout::MarkScaleFor(const UDataTable* NodeTable)
{
	if (!NodeTable)
	{
		return 1.0f;
	}

	TArray<TPair<FVector2D, FVector2D>> Marks;
	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		if (const FCataclysmEmpireTreeNodeRow* Row = reinterpret_cast<const FCataclysmEmpireTreeNodeRow*>(Pair.Value))
		{
			Marks.Emplace(FVector2D(Row->PositionX, Row->PositionY), MarkSizeFor(*Row));
		}
	}
	if (Marks.Num() < 2)
	{
		return 1.0f;
	}

	// EVERY PAIR, about 12,500 for the 159 nodes: the class trees' reason, that the closest pair by distance is not the
	// binding one when one mark is larger than another or wider than it is tall.
	float Allowed = 1.0f;
	for (int32 First = 0; First < Marks.Num(); ++First)
	{
		for (int32 Second = First + 1; Second < Marks.Num(); ++Second)
		{
			const FVector2D Gap = (Marks[First].Key - Marks[Second].Key).GetAbs();
			const FVector2D Half = (Marks[First].Value + Marks[Second].Value) * 0.5;
			const float Pair = FMath::Max(static_cast<float>(Gap.X / Half.X), static_cast<float>(Gap.Y / Half.Y));
			Allowed = FMath::Min(Allowed, Pair);
		}
	}
	return FMath::Max(Allowed, 0.05f);
}

TArray<TPair<FName, FName>> UCataclysmEmpireTreeLayout::EdgesOf(const UDataTable* NodeTable,
																const UDataTable* EdgeTable)
{
	TArray<TPair<FName, FName>> Out;
	if (!NodeTable || !EdgeTable)
	{
		return Out;
	}

	for (const TPair<FName, uint8*>& Pair : EdgeTable->GetRowMap())
	{
		const FCataclysmEmpireTreeEdgeRow* Edge = reinterpret_cast<const FCataclysmEmpireTreeEdgeRow*>(Pair.Value);
		if (!Edge)
		{
			continue;
		}
		const FName Source(*Edge->Source);
		const FName Target(*Edge->Target);
		if (UCataclysmEmpireTree::FindNode(NodeTable, Source) && UCataclysmEmpireTree::FindNode(NodeTable, Target))
		{
			Out.Emplace(Source, Target);
		}
	}
	return Out;
}

int32 UCataclysmEmpireTreeLayout::RingSegments(float Radius)
{
	return FMath::Clamp(FMath::CeilToInt(Radius / 8.0f), 24, 96);
}

TArray<FVector2D> UCataclysmEmpireTreeLayout::RingPoints(FVector2D Centre, float Radius)
{
	const int32 Segments = RingSegments(Radius);
	TArray<FVector2D> Out;
	Out.Reserve(Segments + 1);
	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		// THE LAST POINT IS THE FIRST, worked out rather than copied, so the ring closes on itself to rounding.
		const double Angle = 2.0 * UE_DOUBLE_PI * (Index % Segments) / Segments;
		Out.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
	}
	return Out;
}

FVector2D UCataclysmEmpireTreeLayout::LineEnd(const FCataclysmEmpireTreeDecorationRow& Row)
{
	const double Angle = FMath::DegreesToRadians(static_cast<double>(Row.AngleDegrees));
	return FVector2D(Row.CentreX, Row.CentreY) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Row.Length;
}

FLinearColor UCataclysmEmpireTreeLayout::ColourOf(const FString& Hex, float Opacity)
{
	FLinearColor Colour = FLinearColor::White;
	if (Hex.Len() == 7 && Hex.StartsWith(TEXT("#")))
	{
		// THE BUILDER'S COLOURS ARE sRGB, as a web page's are, so they are read as sRGB and not as linear values.
		Colour = FLinearColor(FColor::FromHex(Hex));
	}
	Colour.A = FMath::Clamp(Opacity, 0.0f, 1.0f);
	return Colour;
}

int32 UCataclysmEmpireTreeLayout::LabelFontSize(float FontSize, float Zoom)
{
	return FMath::Max(1, FMath::RoundToInt(FontSize * Zoom * 0.75f));
}

float UCataclysmEmpireTreeLayout::DecorationThicknessPx(float Thickness, float Zoom)
{
	return FMath::Max(1.5f, Thickness * Zoom);
}

FString UCataclysmEmpireTreeLayout::BankLine(double Banked, bool bTheCharactersOwn)
{
	return FString::Printf(TEXT("%s empire upgrade points banked: %d to spend%s"),
						   *UCataclysmEmpireMapLayout::EmpirePointsText(Banked), UCataclysmEmpireTree::WholePointsIn(Banked),
						   bTheCharactersOwn ? TEXT("; this character's own") : TEXT(""));
}

FString UCataclysmEmpireTreeLayout::BranchLine(const FString& View, int32 SpentHere, int32 SpentInTree)
{
	if (View == WholeTreeView)
	{
		return FString::Printf(TEXT("%s: %d point%s spent"), *ViewLabel(View), SpentInTree,
							   SpentInTree == 1 ? TEXT("") : TEXT("s"));
	}
	return FString::Printf(TEXT("%s: %d point%s spent here, %d in the whole tree"), *ViewLabel(View), SpentHere,
						   SpentHere == 1 ? TEXT("") : TEXT("s"), SpentInTree);
}

FString UCataclysmEmpireTreeLayout::NodeLabel(const FCataclysmEmpireTreeNodeRow& Row, int32 Points)
{
	return FString::Printf(TEXT("%s  %d/%d"), *Row.NodeName, Points, Row.MaxPoints);
}

FString UCataclysmEmpireTreeLayout::NodeDetail(const FCataclysmEmpireTreeNodeRow& Row, int32 Points,
											   int32 ChosenOption, const FString& Refusal, int32 ExtraOption,
											   const FString& ExtraGranter)
{
	FString Detail = FString::Printf(TEXT("%s (%s, %d of %d): %s"), *Row.NodeName, *ViewLabel(Row.Branch), Points,
									 Row.MaxPoints, *Row.Description);

	// A CENTRE CAPSTONE'S THREE OPTIONS, NAMED: a choice that stands until a respec should not be asked for between
	// things the screen has not named, the class trees' issue #1076.
	const TArray<FString> Options = UCataclysmEmpireTree::OptionNamesOf(Row);
	TArray<FString> Named;
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		if (!Options[Index].IsEmpty())
		{
			// AND THE EXTRA OPTION IMPERIAL PROWESS TOOK, marked with what took it (issue #2741 slice 8).
			const FString Extra = ExtraOption == Index + 1 && ChosenOption != Index + 1
				? FString::Printf(TEXT(" (taken with %s)"), *ExtraGranter)
				: FString();
			Named.Add(FString::Printf(TEXT("%d %s%s%s"), Index + 1, *Options[Index],
									  ChosenOption == Index + 1 ? TEXT(" (chosen)") : TEXT(""), *Extra));
		}
	}
	if (!Named.IsEmpty())
	{
		Detail += FString::Printf(TEXT(" Options: %s."), *FString::Join(Named, TEXT("; ")));
	}

	if (!Refusal.IsEmpty())
	{
		Detail += TEXT(" ") + Refusal;
	}
	return Detail;
}

FString UCataclysmEmpireTreeLayout::OptionLabel(int32 Option, const FString& Name, bool bChosen,
												const FString& WaitsOn, const FString& TakenWith)
{
	FString Label = FString::Printf(TEXT("%d  %s"), Option, *Name);
	if (bChosen)
	{
		Label += TEXT("  (chosen)");
	}
	if (!TakenWith.IsEmpty())
	{
		Label += FString::Printf(TEXT("  (taken with %s)"), *TakenWith);
	}
	if (!WaitsOn.IsEmpty())
	{
		Label += FString::Printf(TEXT("  (waits on %s)"), *WaitsOn);
	}
	return Label;
}

FString UCataclysmEmpireTreeLayout::RespecLabel(int32 Days, bool bAwaitingTheSecondPress)
{
	// TWO CALLS, NOT ONE WITH A CHOSEN FORMAT: `FString::Printf` takes its format as a literal.
	if (bAwaitingTheSecondPress)
	{
		return FString::Printf(TEXT("Press again to respec: %d days of the run"), Days);
	}
	return FString::Printf(TEXT("Respec: %d days of the run"), Days);
}
