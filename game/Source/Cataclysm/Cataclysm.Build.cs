// Copyright Stephen Dubois. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// The primary game module: the player, combat, abilities, items and dungeons.
/// </summary>
public class Cataclysm : ModuleRules
{
	public Cataclysm(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// This module uses a flat layout (AbilitySystem/, Character/, Player/)
		// rather than the Public/Private split. Without adding the module root
		// here, includes such as "AbilitySystem/CataclysmAttributeSet.h" do not
		// resolve, because UnrealBuildTool only adds Public and Private
		// automatically.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",

			// Click-to-move. SimpleMoveToLocation and the path following
			// component both live in AIModule, which is what actually walks a
			// character along a navigation path. Without a NavMeshBoundsVolume in
			// the level it finds no path and the character does not move, and
			// nothing reports why -- see the test map the generator builds.
			"AIModule",
			"NavigationSystem",

			// The Gameplay Ability System. GameplayTags is public because
			// attribute sets, abilities and item data all expose tags in their
			// headers; GameplayTasks is a hard dependency of GameplayAbilities.
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"NetCore",
		});
	}
}
