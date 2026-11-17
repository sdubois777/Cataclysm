// Copyright Stephen Dubois. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>
/// The editor target. Adds the editor tooling module on top of the two runtime
/// modules the game target ships.
/// </summary>
public class CataclysmEditorTarget : TargetRules
{
	public CataclysmEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "Cataclysm", "CataclysmEmpire", "CataclysmEditor" });
	}
}
