#include "Bio/HeliosBioSupplyWeapon.h"
#include "Bio/HeliosBioSupplyWeaponDefinition.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Serialization/JsonSerializer.h"

AHeliosBioSupplyTracer::AHeliosBioSupplyTracer()
{
    PrimaryActorTick.bCanEverTick=true;Ribbon=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalLineRendererBridge"));SetRootComponent(Ribbon);
    Ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);Ribbon->SetCastShadow(false);Ribbon->SetCanEverAffectNavigation(false);
}
bool AHeliosBioSupplyTracer::Initialize(UHeliosBioSupplyWeaponDefinition* Definition,const FVector& From,const FVector& To,bool FP)
{
    TSharedPtr<FJsonObject> Recipe;
    if(!Definition||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Definition->NativeTracerRecipe),Recipe)||!Recipe)return false;
    auto* Source=Definition->NativeTracerMaterial.LoadSynchronous();if(!Source||FVector::DistSquared(From,To)<1.f)return false;
    const auto& UV=Recipe->GetObjectField(TEXT("sourceUVAnimator"));
    const auto& Gradient=Recipe->GetObjectField(TEXT("sourceLineRenderer"))->GetObjectField(TEXT("m_Parameters"))->GetObjectField(TEXT("colorGradient"));
    const auto Color=[](const TSharedPtr<FJsonObject>& C){return FLinearColor(C->GetNumberField(TEXT("r")),C->GetNumberField(TEXT("g")),C->GetNumberField(TEXT("b")),C->GetNumberField(TEXT("a")));};
    StartColor=Color(Gradient->GetObjectField(TEXT("key0")));EndColor=Color(Gradient->GetObjectField(TEXT("key1")));
    Duration=Recipe->GetNumberField(TEXT("sourceDurationSeconds"));Width=Recipe->GetNumberField(FP?TEXT("nativeWeaponWidth1PCm"):TEXT("nativeWeaponBasicWidthCm"));
    if(Duration<=0||Width<=0)return false;
    StartU=UV->GetObjectField(TEXT("StartOffset"))->GetNumberField(TEXT("x"));EndU=UV->GetObjectField(TEXT("EndOffset"))->GetNumberField(TEXT("x"));
    TilingU=UV->GetObjectField(TEXT("Tiling"))->GetNumberField(TEXT("x"));TilingV=UV->GetObjectField(TEXT("Tiling"))->GetNumberField(TEXT("y"));
    Start=From;End=To;Material=UMaterialInstanceDynamic::Create(Source,this);Ribbon->SetMaterial(0,Material);
    SetLifeSpan(Duration+.05f);Tick(0);return true;
}
void AHeliosBioSupplyTracer::Tick(float Dt)
{
    Super::Tick(Dt);if(!Material||GetWorld()->IsPaused())return;Age+=Dt;if(Age>=Duration){Destroy();return;}
    const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);if(!Camera)return;
    const FVector Direction=(End-Start).GetSafeNormal(),View=Camera->GetCameraLocation()-(Start+End)*.5f;
    const FVector Side=FVector::CrossProduct(Direction,View).GetSafeNormal(SMALL_NUMBER,Camera->GetActorRightVector())*Width*.5f;
    const TArray<FVector> Vertices={Start-Side,Start+Side,End+Side,End-Side};const TArray<int32> Indices={0,1,2,0,2,3};
    const TArray<FVector> Normals={View.GetSafeNormal(),View.GetSafeNormal(),View.GetSafeNormal(),View.GetSafeNormal()};
    const TArray<FVector2D> UV={FVector2D(0,0),FVector2D(0,1),FVector2D(1,1),FVector2D(1,0)};
    const TArray<FLinearColor> Colors={StartColor,StartColor,EndColor,EndColor};const TArray<FProcMeshTangent> Tangents;
    if(Ribbon->GetNumSections()==0)Ribbon->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false,false);
    else Ribbon->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,Colors,Tangents,false);
    Material->SetVectorParameterValue(TEXT("UVTransform"),FLinearColor(TilingU,TilingV,FMath::Lerp(StartU,EndU,FMath::Clamp(Age/Duration,0.f,1.f)),0));
}
