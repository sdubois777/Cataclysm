// Copyright Stephen Dubois. All Rights Reserved.

#include "CataclysmWidgetAuthoring.h"

#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
// FSavePackageArgs. UPackage.h forward declares it and does not define it,
// so without this the four-argument SavePackage overload takes a struct the
// compiler has only ever seen the name of.
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

DEFINE_LOG_CATEGORY_STATIC(LogCataclysmWidgetAuthoring, Log, All);

namespace
{
	/** `/Game/Interface` and `WBP_Thing` joined the way the asset registry
	 *  wants them: `/Game/Interface/WBP_Thing.WBP_Thing`. */
	FString ObjectPathFor(const FString& PackagePath, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName,
							   *AssetName);
	}

	/** The tree, or null, without any of the callers repeating the two checks. */
	UWidgetTree* TreeOf(UWidgetBlueprint* Blueprint)
	{
		return Blueprint ? Blueprint->WidgetTree : nullptr;
	}
}

bool UCataclysmWidgetAuthoring::WidgetBlueprintExists(const FString& PackagePath,
													  const FString& AssetName)
{
	return FindObject<UWidgetBlueprint>(
			   nullptr, *ObjectPathFor(PackagePath, AssetName)) != nullptr
		   || LoadObject<UWidgetBlueprint>(
				  nullptr, *ObjectPathFor(PackagePath, AssetName), nullptr,
				  LOAD_NoWarn | LOAD_Quiet) != nullptr;
}

UWidgetBlueprint* UCataclysmWidgetAuthoring::CreateOrLoadWidgetBlueprint(
	const FString& PackagePath, const FString& AssetName,
	TSubclassOf<UUserWidget> ParentClass)
{
	if (!ParentClass)
	{
		UE_LOG(LogCataclysmWidgetAuthoring, Error,
			   TEXT("No parent class was given for %s, and a Widget Blueprint "
					"with no C++ base class can carry no logic at all."),
			   *AssetName);
		return nullptr;
	}

	// AN EXISTING ASSET IS RETURNED UNTOUCHED. A generator that rebuilt one
	// would throw away every layout change made in the designer since, which is
	// the half of the split `docs/DECISIONS.md` puts in the designer's hands.
	if (UWidgetBlueprint* Already = LoadObject<UWidgetBlueprint>(
			nullptr, *ObjectPathFor(PackagePath, AssetName), nullptr,
			LOAD_NoWarn | LOAD_Quiet))
	{
		return Already;
	}

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	IAssetTools& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Created = AssetTools.CreateAsset(AssetName, PackagePath,
											  UWidgetBlueprint::StaticClass(),
											  Factory);

	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Created);
	if (!Blueprint)
	{
		UE_LOG(LogCataclysmWidgetAuthoring, Error,
			   TEXT("Could not create %s/%s."), *PackagePath, *AssetName);
	}

	return Blueprint;
}

UWidget* UCataclysmWidgetAuthoring::AddWidget(UWidgetBlueprint* Blueprint,
											  TSubclassOf<UWidget> WidgetClass,
											  const FString& Name,
											  const FString& ParentName)
{
	UWidgetTree* Tree = TreeOf(Blueprint);
	if (!Tree || !WidgetClass)
	{
		UE_LOG(LogCataclysmWidgetAuthoring, Error,
			   TEXT("Cannot add %s: no widget tree or no widget class."), *Name);
		return nullptr;
	}

	const FName WidgetName(*Name);
	if (Tree->FindWidget(WidgetName))
	{
		// REFUSED RATHER THAN RENAMED. Unreal would silently make a second
		// widget called `Title_1`, which satisfies nothing and leaves the
		// `BindWidget` property this was meant to fill still unbound.
		UE_LOG(LogCataclysmWidgetAuthoring, Error,
			   TEXT("%s already holds a widget called %s."),
			   *Blueprint->GetName(), *Name);
		return nullptr;
	}

	UWidget* Widget = Tree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (!Widget)
	{
		UE_LOG(LogCataclysmWidgetAuthoring, Error,
			   TEXT("Could not construct a %s called %s."),
			   *WidgetClass->GetName(), *Name);
		return nullptr;
	}

	// A VARIABLE, WHICH IS WHAT `BindWidget` MATCHES ON. Without this the widget
	// is in the tree and has no name the base class can bind to, and the compile
	// fails saying the widget is missing.
	Widget->bIsVariable = true;

	if (ParentName.IsEmpty())
	{
		Tree->RootWidget = Widget;
	}
	else
	{
		UPanelWidget* Parent = Cast<UPanelWidget>(Tree->FindWidget(FName(*ParentName)));
		if (!Parent)
		{
			UE_LOG(LogCataclysmWidgetAuthoring, Error,
				   TEXT("%s is not a panel widget in %s, so %s has nowhere to "
						"go."),
				   *ParentName, *Blueprint->GetName(), *Name);
			return nullptr;
		}

		if (!Parent->AddChild(Widget))
		{
			// A PANEL THAT IS FULL RATHER THAN MISSING. A UBorder and a
			// UButton hold exactly one child, so a second one is refused, and
			// that is a mistake in the layout being generated rather than a
			// fault here.
			UE_LOG(LogCataclysmWidgetAuthoring, Error,
				   TEXT("%s would not take %s as a child. A Border and a Button "
						"hold one child each."),
				   *ParentName, *Name);
			return nullptr;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Widget;
}

UWidget* UCataclysmWidgetAuthoring::FindWidget(UWidgetBlueprint* Blueprint,
											   const FString& Name)
{
	UWidgetTree* Tree = TreeOf(Blueprint);
	return Tree ? Tree->FindWidget(FName(*Name)) : nullptr;
}

TArray<FString> UCataclysmWidgetAuthoring::WidgetNames(
	const UWidgetBlueprint* Blueprint)
{
	TArray<FString> Names;
	const UWidgetTree* Tree = Blueprint ? Blueprint->WidgetTree : nullptr;
	if (!Tree)
	{
		return Names;
	}

	Tree->ForEachWidget([&Names](UWidget* Widget)
	{
		if (Widget)
		{
			Names.Add(Widget->GetName());
		}
	});

	Names.Sort();
	return Names;
}

TArray<FString> UCataclysmWidgetAuthoring::RequiredWidgetNames(
	TSubclassOf<UUserWidget> WidgetClass)
{
	TArray<FString> Names;
	if (!WidgetClass)
	{
		return Names;
	}

	// READ OFF THE CLASS, WHICH IS WHERE THE ENGINE READS THEM TOO. The Blueprint
	// compiler finds a `BindWidget` by looking for that exact metadata key on an
	// object property, so asking the same question the same way cannot answer
	// differently.
	//
	// `HasMetaData` AND NOT `GetBoolMetaData`, AND THE DIFFERENCE COST A
	// GENERATOR RUN. `meta = (BindWidget)` stores the key with an EMPTY value,
	// because the key's presence is the whole statement. `GetBoolMetaData`
	// parses that value as a bool, an empty string is not `true`, and so it
	// answered false for every property. The check then reported "satisfies 0
	// BindWidget properties" for a class with two of them and passed -- a guard
	// that could not fail.
	for (TFieldIterator<FObjectPropertyBase> It(WidgetClass); It; ++It)
	{
		const FObjectPropertyBase* Property = *It;
		if (Property && Property->HasMetaData(TEXT("BindWidget")))
		{
			Names.Add(Property->GetName());
		}
	}

	Names.Sort();
	return Names;
}

bool UCataclysmWidgetAuthoring::CompileAndSave(UWidgetBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// THE COMPILE'S OWN VERDICT RATHER THAN THE ABSENCE OF AN EXCEPTION.
	// `CompileBlueprint` returns void and reports through the Blueprint's
	// status, so a generator that only checked for a crash would call a
	// Blueprint that failed to bind its widgets a success.
	if (Blueprint->Status == BS_Error)
	{
		UE_LOG(LogCataclysmWidgetAuthoring, Error,
			   TEXT("%s did not compile. A BindWidget property with no widget "
					"of that name and type in the tree is the usual cause."),
			   *Blueprint->GetName());
		return false;
	}

	UPackage* Package = Blueprint->GetOutermost();
	if (!Package)
	{
		return false;
	}

	Package->MarkPackageDirty();
	const FString FileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, nullptr, *FileName, Args);
}
