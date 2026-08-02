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

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Cataclysm",
			"CataclysmEmpire",
		});
	}
}
