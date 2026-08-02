// Copyright Stephen Dubois. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// The primary game module: the player, combat, abilities, items and dungeons.
///
/// The Gameplay Ability System dependencies are deliberately NOT added here yet.
/// They arrive with the GAS setup work, so that the first compile proves the
/// project skeleton alone.
/// </summary>
public class Cataclysm : ModuleRules
{
	public Cataclysm(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});
	}
}
