// Copyright Stephen Dubois. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// Editor-only tooling: data import and validation commandlets, asset actions,
/// and the pipeline that turns the design spreadsheet into DataTables.
///
/// Never compiled into a packaged build. Anything here can safely assume the
/// editor is present.
/// </summary>
public class CataclysmEditor : ModuleRules
{
	public CataclysmEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The same flat layout the Cataclysm module uses, and for the same
		// reason: without the module root here, an include such as
		// "CataclysmLevelAuthoring.h" from a file in Tests/ does not resolve,
		// because UnrealBuildTool only adds Public and Private automatically
		// and this module has neither.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Cataclysm",
			"CataclysmEmpire",

			// ANavMeshBoundsVolume, for the level authoring helper that gives the
			// sandbox level its navigation bounds.
			"NavigationSystem",

			// The widget authoring helper, which builds a Widget Blueprint's
			// tree because the editor's Python layer cannot reach it. All four
			// are needed together and none is spare:
			//   UMG          UWidget, UPanelWidget, UUserWidget, UWidgetTree
			//   UMGEditor    UWidgetBlueprint and UWidgetBlueprintFactory
			//   Kismet       FKismetEditorUtilities::CompileBlueprint
			//   AssetTools   IAssetTools::CreateAsset
			// UnrealEd above already brings FBlueprintEditorUtils.
			"UMG",
			"UMGEditor",
			"Kismet",
			"AssetTools",
		});
	}
}
