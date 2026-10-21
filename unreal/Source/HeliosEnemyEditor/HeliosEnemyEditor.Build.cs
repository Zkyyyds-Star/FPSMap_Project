using UnrealBuildTool;
public class HeliosEnemyEditor : ModuleRules
{
    public HeliosEnemyEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","Niagara","NiagaraShader"});
        PrivateDependencyModuleNames.AddRange(new string[]{
            "UnrealEd","HeliosOutpost","NavigationSystem","Json","RenderCore","RHI","MeshDescription","SkeletalMeshDescription",
            "StateTreeModule","StateTreeEditorModule","GameplayStateTreeModule","PropertyBindingUtils","NiagaraEditor"
        });
        // UE 5.8 keeps lightweight Niagara emitter authoring types in Internal.
        // This editor-only module intentionally pins the authoring helper to 5.8.
        PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Plugins","FX","Niagara","Source","Niagara","Internal"));
        PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Plugins","FX","Niagara","Source","NiagaraShader","Internal"));
    }
}
