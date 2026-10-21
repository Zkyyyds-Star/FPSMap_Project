#include "Player/HeliosQingYaSelection.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
namespace HeliosQingYaSelection
{
    static FString Path(){return FPaths::ProjectSavedDir()/TEXT("SaveGames/HeliosQingYaSelection.json");}
    bool Restore(FHeliosLoadout& Out)
    {
        FString Text;TSharedPtr<FJsonObject> Json;bool Selected=false;FHeliosLoadout Saved;FString Error;
        if(!FFileHelper::LoadFileToString(Text,*Path())||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json)||!Json||
           !Json->TryGetBoolField(TEXT("selected"),Selected)||!Selected||!HeliosLoadout::ReadJson(Json,Saved)||
           Saved.CharacterId!=TEXT("cfm_qingya")||!FHeliosLoadout::Validate(Saved,Error))return false;
        Out=Saved;return true;
    }
    void Save(const FHeliosLoadout& Loadout)
    {
        const bool Selected=Loadout.CharacterId==TEXT("cfm_qingya");
        if(!Selected&&!FPaths::FileExists(Path()))return;
        auto Json=MakeShared<FJsonObject>();Json->SetBoolField(TEXT("selected"),Selected);
        if(Selected)HeliosLoadout::WriteJson(Json,Loadout);
        FString Text;FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text));
        FString Previous;if(FFileHelper::LoadFileToString(Previous,*Path())&&Previous==Text)return;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path()),true);
        if(!FFileHelper::SaveStringToFile(Text,*Path(),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            UE_LOG(LogTemp,Warning,TEXT("QINGYA_SELECTION_SAVE_FAILED"));
    }
}
