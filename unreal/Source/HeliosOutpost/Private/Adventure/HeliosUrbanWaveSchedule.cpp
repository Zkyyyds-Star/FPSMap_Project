#include "Adventure/HeliosUrbanWaveSchedule.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FHeliosUrbanWaveSchedule::Load(FString& Error)
{
    Waves.Reset();
    FString Text;
    TSharedPtr<FJsonObject> Root;
    const FString Path = FPaths::ProjectContentDir() / TEXT("UrbanChallenge/Data/waves.json");
    if (!FFileHelper::LoadFileToString(Text, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
    { Error = TEXT("无法读取都市挑战波次配置"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Root->TryGetArrayField(TEXT("waves"), Rows) || Rows->Num() != 30)
    { Error = TEXT("都市挑战必须配置30波"); return false; }
    TArray<FHeliosUrbanWave> Validated;
    for (int32 Index = 0; Index < Rows->Num(); ++Index)
    {
        const auto Row = (*Rows)[Index]->AsObject();
        double N, Total, Cap, Bosses, Interval, Rest;
        if (!Row.IsValid() || !Row->TryGetNumberField(TEXT("wave"), N) || N != Index + 1 ||
            !Row->TryGetNumberField(TEXT("total"), Total) || !FMath::IsFinite(Total) || Total != FMath::FloorToDouble(Total) || Total < 15 || Total > 60 ||
            !Row->TryGetNumberField(TEXT("alive_cap"), Cap) || !FMath::IsFinite(Cap) || Cap != FMath::FloorToDouble(Cap) || Cap < 1 || Cap > 20 ||
            !Row->TryGetNumberField(TEXT("midbosses"), Bosses) || (Bosses != 0 && Bosses != 1) ||
            !Row->TryGetNumberField(TEXT("spawn_seconds"), Interval) || !FMath::IsFinite(Interval) || Interval < .25 || Interval > 10 ||
            !Row->TryGetNumberField(TEXT("rest_seconds"), Rest) || !FMath::IsFinite(Rest) || Rest < 3 || Rest > 60)
        { Error = FString::Printf(TEXT("都市挑战第%d波配置无效"), Index + 1); return false; }
        FHeliosUrbanWave Wave;
        Wave.Total = static_cast<int32>(Total); Wave.AliveCap = static_cast<int32>(Cap);
        Wave.Midbosses = static_cast<int32>(Bosses); Wave.SpawnSeconds = Interval; Wave.RestSeconds = Rest;
        const TCHAR* WeightKeys[]={TEXT("rifle_weight"),TEXT("charger_weight"),TEXT("skirmisher_weight"),TEXT("basketball_weight")};
        for(int32 Type=0;Type<4;++Type)
        {
            double Value=Wave.TypeWeights[Type];
            if(Row->HasField(WeightKeys[Type]) && (!Row->TryGetNumberField(WeightKeys[Type],Value) || !FMath::IsFinite(Value) || Value!=FMath::FloorToDouble(Value) || Value<0 || Value>100))
            { Error=FString::Printf(TEXT("Invalid urban wave %d enemy weight"),Index+1);return false; }
            Wave.TypeWeights[Type]=static_cast<int32>(Value);
        }
        double RifleCap=Wave.RifleCap,SkirmisherCap=Wave.SkirmisherCap;
        if((Row->HasField(TEXT("rifle_cap")) && !Row->TryGetNumberField(TEXT("rifle_cap"),RifleCap)) ||
           (Row->HasField(TEXT("skirmisher_cap")) && !Row->TryGetNumberField(TEXT("skirmisher_cap"),SkirmisherCap)) ||
           !FMath::IsFinite(RifleCap) || !FMath::IsFinite(SkirmisherCap) || RifleCap!=FMath::FloorToDouble(RifleCap) || SkirmisherCap!=FMath::FloorToDouble(SkirmisherCap) ||
           RifleCap<1 || RifleCap>Cap || SkirmisherCap<1 || SkirmisherCap>Cap || Wave.TypeWeights[1]<=0)
        { Error=FString::Printf(TEXT("Invalid urban wave %d ranged caps/uncapped melee weight"),Index+1);return false; }
        Wave.RifleCap=static_cast<int32>(RifleCap);Wave.SkirmisherCap=static_cast<int32>(SkirmisherCap);
        Validated.Add(Wave);
    }
    Waves = MoveTemp(Validated);
    Error.Empty(); return true;
}
