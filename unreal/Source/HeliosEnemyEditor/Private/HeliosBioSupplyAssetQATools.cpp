#include "HeliosBioSupplyAssetQATools.h"

#include "Sound/SoundWave.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"

FString UHeliosBioSupplyAssetQATools::InspectImportedSoundPCM(USoundWave* Sound)
{
    auto Result=MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"),false);
    Result->SetStringField(TEXT("scope"),TEXT("Editor GetImportedSoundWaveData source PCM only; no asset mutation, cooked decode or export transformations."));
    if(!IsValid(Sound))Result->SetStringField(TEXT("error"),TEXT("Invalid SoundWave"));
    else
    {
        Result->SetStringField(TEXT("asset"),Sound->GetPathName());
        TArray<uint8> PCM;uint32 Rate=0;uint16 Channels=0;
        if(!Sound->GetImportedSoundWaveData(PCM,Rate,Channels)||Rate==0||Channels==0||PCM.IsEmpty()||PCM.Num()%(sizeof(int16)*Channels)!=0)
            Result->SetStringField(TEXT("error"),TEXT("Imported 16-bit source PCM unavailable or invalid frame alignment"));
        else
        {
                // Python hashlib computes SHA256 over these exact bytes. The
                // generic Windows GetSHA256Signature is an unsupported assert.
                Result->SetBoolField(TEXT("success"),true);
                Result->SetStringField(TEXT("pcmBase64"),FBase64::Encode(PCM));
                Result->SetNumberField(TEXT("sampleRate"),Rate);
                Result->SetNumberField(TEXT("channels"),Channels);
                Result->SetNumberField(TEXT("sampleWidth"),sizeof(int16));
                Result->SetNumberField(TEXT("frames"),PCM.Num()/(sizeof(int16)*Channels));
                Result->SetNumberField(TEXT("pcmBytes"),PCM.Num());
        }
    }
    FString Text;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));return Text;
}
