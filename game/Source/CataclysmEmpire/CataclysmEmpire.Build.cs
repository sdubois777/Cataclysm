// Copyright Stephen Dubois. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// The empire and strategy layer: the day clock, cities, surges, dungeon timers
/// and resolution, and the empire upgrade tree.
///
/// Kept separate from the primary game module on purpose. This layer is a port of
/// the Python tuning rig in sim/, its rules are plain arithmetic on plain structs,
/// and it should stay testable without the combat, rendering or input systems.
/// It must not depend on the Cataclysm module; the dependency runs one way.
/// </summary>
public class CataclysmEmpire : ModuleRules
{
	public CataclysmEmpire(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// This module uses a flat layout (DayClock/, Tests/) rather than the
		// Public/Private split, the same as the Cataclysm module beside it and
		// for the same reason recorded there. Without adding the module root,
		// includes such as "DayClock/CataclysmDayClock.h" do not resolve, because
		// UnrealBuildTool only adds Public and Private automatically. The failure
		// is a plain "Cannot open include file" and it costs a build to find.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});
	}
}
