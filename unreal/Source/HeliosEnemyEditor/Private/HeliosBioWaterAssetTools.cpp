#include "HeliosBioWaterAssetTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialShared.h"
#include "Engine/Texture2D.h"
#include "ShaderCompiler.h"
#include "ShaderCompilerCore.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UHeliosBioWaterAssetTools::CompileOriginalWaterForRendering(UMaterialInterface* Interface)
{
    auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("success"),false);Result->SetBoolField(TEXT("package_saved"),false);
    Result->SetStringField(TEXT("scope"),TEXT("Exact existing original-water asset. Native texture async completion and Default-usage rendering shader compilation for actual RHI platform/quality. No color, graph, collision, map, console policy or StateId regeneration. Ordinary uncooked package saves do not embed cooked inline shader maps; cold game render-proxy validation remains required."));
    auto ResourceState=[](FMaterialResource* Resource)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetBoolField(TEXT("exists"),Resource!=nullptr);if(!Resource)return Row;
        Row->SetStringField(TEXT("name"),Resource->GetFriendlyName());Row->SetNumberField(TEXT("feature_level"),int32(Resource->GetFeatureLevel()));Row->SetNumberField(TEXT("quality_level"),int32(Resource->GetQualityLevel()));Row->SetNumberField(TEXT("shader_platform"),int32(Resource->GetShaderPlatform()));Row->SetNumberField(TEXT("shader_map_usage"),int32(Resource->GetMaterialShaderMapUsage()));Row->SetBoolField(TEXT("complete"),Resource->IsGameThreadShaderMapComplete());Row->SetBoolField(TEXT("valid"),Resource->HasValidGameThreadShaderMap());Row->SetBoolField(TEXT("compilation_finished"),Resource->IsCompilationFinished());
        if(const auto* Map=Resource->GetGameThreadShaderMap()){Row->SetBoolField(TEXT("map_finalized"),Map->IsCompilationFinalized());Row->SetBoolField(TEXT("map_compiled_successfully"),Map->CompiledSuccessfully());Row->SetBoolField(TEXT("map_layout_with_editor_only"),Map->GetShaderMapId().LayoutParams.WithEditorOnly());}
        TArray<TSharedPtr<FJsonValue>> Errors;for(const auto& Error:Resource->GetCompileErrors())Errors.Add(MakeShared<FJsonValueString>(Error));Row->SetArrayField(TEXT("compile_errors"),Errors);return Row;
    };
    if(!Interface||Interface->GetPathName()!=TEXT("/Game/Helios/CFMBioPyramidFloating/Map/Materials/M_BPF_Water_2_37__NoLM.M_BPF_Water_2_37__NoLM"))Result->SetStringField(TEXT("error"),TEXT("Only the exact original source-water material is accepted"));
    else if(!FApp::CanEverRender()||GUsingNullRHI)Result->SetStringField(TEXT("error"),TEXT("Actual commandlet GPU rendering is required"));
    else if(auto* Material=Interface->GetMaterial())
    {
        Result->SetStringField(TEXT("material"),Material->GetPathName());Result->SetBoolField(TEXT("GIsEditor"),GIsEditor);Result->SetBoolField(TEXT("allow_shader_compiling"),AllowShaderCompiling());Result->SetBoolField(TEXT("shader_manager_skips_compilation"),!GShaderCompilingManager||GShaderCompilingManager->IsShaderCompilationSkipped());Result->SetNumberField(TEXT("actual_shader_platform"),int32(GMaxRHIShaderPlatform));
        if(const auto* CVar=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderCompiler.JobCacheDDC")))Result->SetNumberField(TEXT("actual_shader_job_cache_DDC"),CVar->GetInt());
        Result->SetObjectField(TEXT("before"),ResourceState(Material->GetMaterialResource(GMaxRHIShaderPlatform)));
        TArray<UTexture*> Textures;Material->GetUsedTextures(Textures);TArray<TSharedPtr<FJsonValue>> TextureRows;
        for(auto* Texture:Textures)if(auto* Tex=Cast<UTexture2D>(Texture))
        {
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("asset"),Tex->GetPathName());Row->SetBoolField(TEXT("was_compiling"),Tex->IsCompiling());Row->SetNumberField(TEXT("before_platform_X"),Tex->GetSizeX());Row->SetNumberField(TEXT("before_platform_Y"),Tex->GetSizeY());Row->SetNumberField(TEXT("actual_source_X"),Tex->Source.GetSizeX());Row->SetNumberField(TEXT("actual_source_Y"),Tex->Source.GetSizeY());
            Tex->BlockOnAnyAsyncBuild();Row->SetBoolField(TEXT("still_compiling"),Tex->IsCompiling());Row->SetNumberField(TEXT("after_platform_X"),Tex->GetSizeX());Row->SetNumberField(TEXT("after_platform_Y"),Tex->GetSizeY());TextureRows.Add(MakeShared<FJsonValueObject>(Row));
        }
        Result->SetArrayField(TEXT("actual_texture_async_completion"),TextureRows);
        Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
        auto* Resource=Material->GetMaterialResource(GMaxRHIShaderPlatform);if(Resource){Resource->CacheShaders(EMaterialShaderPrecompileMode::Default);Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);Resource->FinishCompilation();}
        FlushRenderingCommands();Result->SetObjectField(TEXT("after"),ResourceState(Resource));
        struct FRenderState{bool Original=false,Complete=false;FString Name;};auto Actual=MakeShared<FRenderState,ESPMode::ThreadSafe>();const FMaterialRenderProxy* Proxy=Material->GetRenderProxy();
        ENQUEUE_RENDER_COMMAND(BioWaterAuthoringActualProxy)([Proxy,Actual](FRHICommandListImmediate&)
        {const FMaterialRenderProxy* Used=Proxy;const FMaterial& Selected=Proxy->GetMaterialWithFallback(GMaxRHIFeatureLevel,Used);Actual->Original=Used==Proxy;Actual->Complete=Selected.IsRenderingThreadShaderMapComplete();Actual->Name=Selected.GetAssetName();});FlushRenderingCommands();
        Result->SetBoolField(TEXT("actual_renderer_uses_original_proxy"),Actual->Original);Result->SetBoolField(TEXT("actual_renderer_shader_map_complete"),Actual->Complete);Result->SetStringField(TEXT("actual_renderer_material"),Actual->Name);Result->SetBoolField(TEXT("success"),Resource&&Resource->IsGameThreadShaderMapComplete()&&Resource->HasValidGameThreadShaderMap()&&Actual->Original&&Actual->Complete&&Resource->GetCompileErrors().IsEmpty());
    }
    FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return Text;
}
