#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace HeliosMCP
{
inline TSharedPtr<FJsonObject> Object() { return MakeShared<FJsonObject>(); }
inline TSharedPtr<FJsonObject> Result(bool bOK=true, const FString& Code=TEXT(""), const FString& Message=TEXT(""))
{
    auto Out=Object(); Out->SetBoolField(TEXT("ok"),bOK); Out->SetStringField(TEXT("error_code"),Code);
    Out->SetStringField(TEXT("message"),Message); return Out;
}
inline FString Text(const TSharedPtr<FJsonObject>& Value)
{
    FString Out; FJsonSerializer::Serialize(Value.ToSharedRef(),TJsonWriterFactory<>::Create(&Out)); return Out;
}
inline TSharedPtr<FJsonObject> Parse(const FString& Value)
{
    TSharedPtr<FJsonObject> Out; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Value),Out); return Out;
}
inline FString String(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
{
    FString Value; if(Obj) Obj->TryGetStringField(Key,Value); return Value;
}
inline TArray<TSharedPtr<FJsonValue>> Vector(const FVector& V)
{
    return {MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};
}
inline bool ReadVector(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Obj->TryGetArrayField(Key,Values)||Values->Num()!=3) return false;
    double N[3]; for(int I=0;I<3;++I) if(!(*Values)[I]->TryGetNumber(N[I])||!FMath::IsFinite(N[I])) return false;
    Out=FVector(N[0],N[1],N[2]); return true;
}
}
