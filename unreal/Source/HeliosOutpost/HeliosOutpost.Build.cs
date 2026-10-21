using UnrealBuildTool;

public class HeliosOutpost : ModuleRules
{
    public HeliosOutpost(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Keep file-local helpers isolated when the source file list changes.
        bUseUnity = false;

        // Private headers are referenced with feature-relative include paths.
        PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Private"));
        PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "RenderCore" });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "Slate",
            "SlateCore",
            "Json",
            "Sockets",
            "Networking",
            "NavigationSystem",
            "AIModule",
            "EnhancedInput",
            "TP_FirstPerson",
            "Niagara",
            "AudioMixer",
            "ImageWrapper",
            "PhysicsCore",
            "AnimGraphRuntime",
            "ProceduralMeshComponent"
        });

        RuntimeDependencies.Add("$(ProjectDir)/Content/UI/Fonts/SimHei.ttf", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/IkunTown/Data/town.json", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/IkunGhostVillage/Data/village.json", StagedFileType.NonUFS);
    }
}
