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

			// Widgets. Interface/CataclysmInventoryWidget.h derives from
			// UUserWidget, so a UMG type appears in a public header of this
			// module and UMG cannot be kept private the way Niagara below is.
			// Issue #735.
			//
			// ALL THREE ARE NEEDED, AND ADDING ONLY UMG DOES NOT COMPILE. UMG
			// lists Slate and SlateCore as PRIVATE dependencies of itself, so
			// neither propagates to whoever depends on UMG, while UMG's own
			// public headers include SlateCore headers. This is exactly the
			// cost issue #650 recorded when the canvas heads-up display was
			// chosen over a widget in the first place.
			"UMG",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"NetCore",

			// Save files. `FCataclysmSaveStorage` writes the three save records
			// as JSON through FJsonObjectConverter, which is JsonUtilities, and
			// the migration chain works on an FJsonObject, which is Json.
			//
			// PRIVATE, AND IT STAYS PRIVATE ONLY WHILE NOTHING OUTSIDE THIS
			// MODULE INCLUDES `Save/CataclysmSaveMigration.h`. That header names
			// FJsonObject in a function signature, so another module including
			// it would need Json itself. Nothing does today. If one ever has to,
			// move Json to the public list rather than forward-declaring around
			// it -- the same cost UMG above already pays. Issue #529.
			"Json",
			"JsonUtilities",

			// Particle effects. Private rather than public, and deliberately:
			// no header in this module exposes a Niagara type, and the only
			// thing that needs it is the automation test reading the four
			// effect type assets built by tools/generate_effect_types.py.
			// Making it public would put Niagara on every module that depends
			// on this one. Issue #555.
			"Niagara",
		});
	}
}
