// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"
#include "CataclysmWidgetAuthoring.generated.h"

class UWidget;
class UWidgetBlueprint;

/**
 * Building a Widget Blueprint's widget tree, which the editor's Python layer
 * cannot do on its own.
 *
 * WHY THIS EXISTS, AND IT IS THE SAME REASON `UCataclysmLevelAuthoring` EXISTS.
 * `docs/DECISIONS.md`, 2026-08-24, "Screens are a C++ base class with the layout
 * in a Widget Blueprint", commits every screen from now on to shipping a
 * `.uasset` deriving from a C++ `UUserWidget`, whose tree has to contain a
 * widget of the right name and type for every `BindWidget` property the base
 * class declares. Nothing in this repository had ever made a Widget Blueprint,
 * so the first question was whether a script could.
 *
 * IT CAN CREATE ONE AND CANNOT FILL IT. `tools/probe_widget_blueprint.py`
 * measured this on 2026-08-25. `unreal.AssetToolsHelpers` creates the asset
 * happily. The very next step fails:
 *
 *     Exception: WidgetBlueprint: Failed to find property 'widget_tree' for
 *     attribute 'widget_tree' on 'WidgetBlueprint'
 *
 * `UWidgetBlueprint::WidgetTree` is a plain `UPROPERTY()` with no `EditAnywhere`
 * and no `BlueprintReadWrite`, so the editor scripting layer refuses to read it,
 * and every widget in the tree is behind it. `UWidgetTree::ConstructWidget` is a
 * template and is not reachable either.
 *
 * WHAT IS DELIBERATELY NOT HERE: anything about how a widget LOOKS. Fonts,
 * colours, padding, brushes and slot geometry are all ordinary `EditAnywhere`
 * properties, so a Python script can set them itself once it has the widget --
 * which is why every function below that makes a widget hands it back. Adding a
 * SetFont here would be adding a second way to do something that already works.
 *
 * THE ASSETS THIS BUILDS ARE STILL EDITED IN THE DESIGNER AFTERWARDS, and that
 * is the point rather than a caveat. A generator makes the first version so that
 * there is something to open, and the decision above is that layout belongs to
 * whoever opens it. `tools/generate_interface_assets.py` therefore refuses to
 * touch a Widget Blueprint that already exists.
 */
UCLASS()
class UCataclysmWidgetAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * A Widget Blueprint at this path deriving from this class.
	 *
	 * @param PackagePath  a content path such as `/Game/Interface`
	 * @param AssetName    the asset's name, by convention starting `WBP_`
	 * @param ParentClass  the C++ `UUserWidget` subclass it derives from
	 * @return null when the parent class is not a widget, or the asset could not
	 *         be created. An asset already at that path is returned as it is and
	 *         nothing about it is changed, so a caller has to ask.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Widget Authoring")
	static UWidgetBlueprint* CreateOrLoadWidgetBlueprint(const FString& PackagePath,
														 const FString& AssetName,
														 TSubclassOf<UUserWidget> ParentClass);

	/** Whether an asset already sits at that path. Asked before creating one. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Widget Authoring")
	static bool WidgetBlueprintExists(const FString& PackagePath,
									  const FString& AssetName);

	/**
	 * Put a widget of this class into the tree, under this name.
	 *
	 * IT IS MADE A VARIABLE, WHICH IS WHAT `BindWidget` MATCHES ON. A widget
	 * whose `bIsVariable` is false has no name the base class can bind to, and
	 * the Blueprint would fail to compile with a message about a missing widget
	 * that is plainly there in the tree.
	 *
	 * @param ParentName  the name of a panel widget already in the tree. Empty
	 *                    makes this the tree's root, which replaces whatever was
	 *                    root before.
	 * @return the widget, so the caller can set its own properties, or null when
	 *         the name is taken, the parent is missing or is not a panel
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Widget Authoring")
	static UWidget* AddWidget(UWidgetBlueprint* Blueprint,
							  TSubclassOf<UWidget> WidgetClass,
							  const FString& Name,
							  const FString& ParentName);

	/** The widget of that name, or null. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Widget Authoring")
	static UWidget* FindWidget(UWidgetBlueprint* Blueprint, const FString& Name);

	/**
	 * Every widget in the tree, by name.
	 *
	 * FOR A SCRIPT TO CHECK ITS OWN WORK WITH. A generator that believes it
	 * added twelve widgets and added nine has produced a Blueprint that will not
	 * compile against its base class, and the compile failure names one missing
	 * widget rather than all of them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Widget Authoring")
	static TArray<FString> WidgetNames(const UWidgetBlueprint* Blueprint);

	/**
	 * The name of every `BindWidget` property the base class declares.
	 *
	 * WHAT A GENERATOR HAS TO SATISFY, read from the class itself rather than
	 * repeated in the script. A property added to the C++ and forgotten in the
	 * generator is then a failure the generator reports by name, instead of a
	 * Blueprint compile error somebody meets later in the editor.
	 *
	 * `BindWidgetOptional` properties are NOT included, because a missing one is
	 * allowed by definition.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Widget Authoring")
	static TArray<FString> RequiredWidgetNames(TSubclassOf<UUserWidget> WidgetClass);

	/**
	 * Compile the Blueprint and write it to disk.
	 *
	 * COMPILING IS WHAT CHECKS THE `BindWidget` PROPERTIES, so a generator that
	 * skipped it would save an asset the editor refuses to open properly and
	 * report success.
	 *
	 * @return false when the compile produced errors, or the save failed
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Widget Authoring")
	static bool CompileAndSave(UWidgetBlueprint* Blueprint);
};
