using UnrealBuildTool;
using System.Collections.Generic;

public class HeliosOutpostEditorTarget : TargetRules
{
    public HeliosOutpostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.AddRange(new string[] { "HeliosOutpost", "TP_FirstPerson", "HeliosEnemyEditor" });
    }
}
