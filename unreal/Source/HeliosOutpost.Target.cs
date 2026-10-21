using UnrealBuildTool;
using System.Collections.Generic;

public class HeliosOutpostTarget : TargetRules
{
    public HeliosOutpostTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.AddRange(new string[] { "HeliosOutpost", "TP_FirstPerson" });
    }
}
