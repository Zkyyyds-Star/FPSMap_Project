#include "Combat/HeliosCombatTempoSettings.h"
#include "Combat/CombatDamageSettings.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogHeliosCombatTempo, Log, All);

const TCHAR* FHeliosCombatTempoTable::Filename(EHeliosTempoFile File)
{
    switch (File)
    {
    case EHeliosTempoFile::Attack: return TEXT("攻击速度.ini");
    case EHeliosTempoFile::Movement: return TEXT("移动速度.ini");
    case EHeliosTempoFile::Recovery: return TEXT("复活与冷却.ini");
    }
    return TEXT("");
}

void FHeliosCombatTempoTable::LoadDirectory(const FString& Directory, bool bLogWarnings)
{
    bWarnings = bLogWarnings;
    ReportedKeys.Reset();
    FConfigFile* Files[] = {&Attack, &Movement, &Recovery};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Files); ++Index)
    {
        Files[Index]->Empty();
        const FString Path = Directory / Filename(static_cast<EHeliosTempoFile>(Index));
        FString Contents;
        if (FFileHelper::LoadFileToString(Contents, *Path))
            Files[Index]->ProcessInputFileContents(Contents, Path);
        else if (bWarnings)
            UE_LOG(LogHeliosCombatTempo, Warning, TEXT("Cannot read %s; original values will be used."), *Path);
    }
}

float FHeliosCombatTempoTable::Get(EHeliosTempoFile File, const TCHAR* Section,
    const TCHAR* Key, float Fallback, float Minimum) const
{
    const FConfigFile* Config = nullptr;
    switch (File)
    {
    case EHeliosTempoFile::Attack: Config = &Attack; break;
    case EHeliosTempoFile::Movement: Config = &Movement; break;
    case EHeliosTempoFile::Recovery: Config = &Recovery; break;
    }
    FString Raw;
    const bool bPresent = Config && Config->GetString(Section, Key, Raw);
    Raw.TrimStartAndEndInline();
    const float Parsed = static_cast<float>(FCString::Atod(*Raw));
    const bool bValid = bPresent && !Raw.IsEmpty() && Raw.IsNumeric()
        && FMath::IsFinite(Parsed) && Parsed >= Minimum;
    const float Value = bValid ? Parsed : Fallback;
    const FString Id = FString::Printf(TEXT("%s|%s|%s"), Filename(File), Section, Key);
    if (bWarnings && !ReportedKeys.Contains(Id))
    {
        ReportedKeys.Add(Id);
        if (!bValid)
            UE_LOG(LogHeliosCombatTempo, Warning,
                TEXT("%s raw='%s' missing or invalid (minimum %.3g); using original %.9g"),
                *Id, *Raw, Minimum, Fallback);
        UE_LOG(LogHeliosCombatTempo, Display, TEXT("COMBAT_TEMPO %s=%.9g"), *Id, Value);
    }
    return Value;
}

void UHeliosCombatTempoSettings::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const FString Directory = UCombatDamageSettings::Directory();
    UE_LOG(LogHeliosCombatTempo, Display, TEXT("COMBAT_TEMPO_MAP map=%s directory=%s"),
        *GetWorld()->GetMapName(), *Directory);
    Table.LoadDirectory(Directory);
    const TCHAR* Names[]={TEXT("孙尚香.ini"),TEXT("钟小葵.ini")};
    for(int32 I=0;I<2;++I)
    {
        FString Contents;const FString Path=Directory/Names[I];
        if(FFileHelper::LoadFileToString(Contents,*Path))HunterFiles[I].ProcessInputFileContents(Contents,Path);
        else UE_LOG(LogHeliosCombatTempo,Warning,TEXT("HUNTER_SETTINGS missing %s; using existing defaults"),*Path);
    }
}

bool UHeliosCombatTempoSettings::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

float UHeliosCombatTempoSettings::Resolve(const UObject* Context, EHeliosTempoFile File,
    const TCHAR* Section, const TCHAR* Key, float Fallback, float Minimum)
{
    if (const UWorld* World = Context ? Context->GetWorld() : nullptr)
        if (const UHeliosCombatTempoSettings* Settings = World->GetSubsystem<UHeliosCombatTempoSettings>())
            return Settings->Table.Get(File, Section, Key, Fallback, Minimum);
    return Fallback;
}

float UHeliosCombatTempoSettings::Hunter(const UObject* Context,bool ZXK,bool Pet,const TCHAR* Key,float Fallback,float Minimum,float Maximum)
{
    const UWorld* World=Context?Context->GetWorld():nullptr;
    const auto* S=World?World->GetSubsystem<UHeliosCombatTempoSettings>():nullptr;if(!S)return Fallback;
    const TCHAR* Section=Pet?TEXT("伙伴"):TEXT("武器");FString Raw;
    const bool Present=S->HunterFiles[ZXK?1:0].GetString(Section,Key,Raw);Raw.TrimStartAndEndInline();
    const float Parsed=FCString::Atod(*Raw);
    const bool Valid=Present&&!Raw.IsEmpty()&&Raw.IsNumeric()&&FMath::IsFinite(Parsed)&&Parsed>=Minimum&&Parsed<=Maximum;
    const float Value=Valid?Parsed:Fallback;
    const FString Id=FString::Printf(TEXT("%s|%s|%s"),ZXK?TEXT("钟小葵"):TEXT("孙尚香"),Section,Key);
    if(!S->HunterReported.Contains(Id))
    {
        S->HunterReported.Add(Id);
        if(!Valid)UE_LOG(LogHeliosCombatTempo,Warning,TEXT("HUNTER_SETTINGS %s raw='%s' invalid/missing [%.3f,%.3f]; fallback %.6f"),*Id,*Raw,Minimum,Maximum,Fallback);
        UE_LOG(LogHeliosCombatTempo,Display,TEXT("HUNTER_SETTINGS %s=%.6f"),*Id,Value);
    }
    return Value;
}
