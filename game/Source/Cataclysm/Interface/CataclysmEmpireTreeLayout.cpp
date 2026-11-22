// Copyright Stephen Dubois. All Rights Reserved.

#include "Interface/CataclysmEmpireTreeLayout.h"
#include "Data/CataclysmDataRows.h"
#include "Engine/DataTable.h"
#include "Interface/CataclysmEmpireMapLayout.h"
#include "Player/CataclysmEmpireTree.h"

TArray<FString> UCataclysmEmpireTreeLayout::Views()
{
	return UCataclysmEmpireTree::BranchNames();
}

FString UCataclysmEmpireTreeLayout::ViewLabel(const FString& Branch)
{
	return Branch == UCataclysmEmpireTree::CentreBranch ? FString(TEXT("The centre")) : Branch;
}

FCataclysmTreeExtent UCataclysmEmpireTreeLayout::ExtentOf(const UDataTable* NodeTable, const FString& Branch)
{
	FCataclysmTreeExtent Extent;
	if (!NodeTable || Branch.IsEmpty())
	{
		return Extent;
	}

	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		const FCataclysmEmpireTreeNodeRow* Row = reinterpret_cast<const FCataclysmEmpireTreeNodeRow*>(Pair.Value);
		if (!Row || Row->Branch != Branch)
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

float UCataclysmEmpireTreeLayout::NodeScaleFor(const UDataTable* NodeTable, const FString& Branch)
{
	if (!NodeTable || Branch.IsEmpty())
	{
		return 1.0f;
	}

	TArray<FVector2D> Points;
	for (const TPair<FName, uint8*>& Pair : NodeTable->GetRowMap())
	{
		const FCataclysmEmpireTreeNodeRow* Row = reinterpret_cast<const FCataclysmEmpireTreeNodeRow*>(Pair.Value);
		if (Row && Row->Branch == Branch)
		{
			Points.Add(FVector2D(Row->PositionX, Row->PositionY));
		}
	}
	if (Points.Num() < 2)
	{
		return 1.0f;
	}

	// EVERY PAIR, about 780 for a branch of 40: the class trees' reason, that the closest pair by distance is not the
	// binding one when a node is wider than it is tall.
	float Allowed = 1.0f;
	for (int32 First = 0; First < Points.Num(); ++First)
	{
		for (int32 Second = First + 1; Second < Points.Num(); ++Second)
		{
			const FVector2D Gap = (Points[First] - Points[Second]).GetAbs();
			const float Pair = FMath::Max(static_cast<float>(Gap.X) / UCataclysmPassiveTreeLayout::NodeWidthPx,
										  static_cast<float>(Gap.Y) / UCataclysmPassiveTreeLayout::NodeHeightPx);
			Allowed = FMath::Min(Allowed, Pair);
		}
	}
	return Allowed;
}

TArray<TPair<FName, FName>> UCataclysmEmpireTreeLayout::EdgesIn(const UDataTable* NodeTable,
																const UDataTable* EdgeTable, const FString& Branch)
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
		if (UCataclysmEmpireTree::BranchOf(NodeTable, Source) == Branch
			&& UCataclysmEmpireTree::BranchOf(NodeTable, Target) == Branch)
		{
			Out.Emplace(Source, Target);
		}
	}
	return Out;
}

FString UCataclysmEmpireTreeLayout::BankLine(double Banked, bool bTheCharactersOwn)
{
	return FString::Printf(TEXT("%s empire upgrade points banked: %d to spend%s"),
						   *UCataclysmEmpireMapLayout::EmpirePointsText(Banked), UCataclysmEmpireTree::WholePointsIn(Banked),
						   bTheCharactersOwn ? TEXT("; this character's own") : TEXT(""));
}

FString UCataclysmEmpireTreeLayout::BranchLine(const FString& Branch, int32 SpentHere, int32 SpentInTree)
{
	return FString::Printf(TEXT("%s: %d point%s spent here, %d in the whole tree"), *ViewLabel(Branch), SpentHere,
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
