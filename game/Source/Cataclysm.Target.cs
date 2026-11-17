// Copyright Stephen Dubois. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/// <summary>
/// The packaged game target. Ships the two runtime modules; the editor module is
/// deliberately absent so editor-only tooling can never be compiled into a build.
/// </summary>
public class CataclysmTarget : TargetRules
{
	public CataclysmTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "Cataclysm", "CataclysmEmpire" });
	}
}
