#include "Weapons/HeliosBuLianShiP90Effect.h"
#include "Player/HeliosArenaCharacter.h"
#include "Camera/CameraComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

AHeliosBuLianShiP90Effect::AHeliosBuLianShiP90Effect()
{PrimaryActorTick.bCanEverTick=true;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Source effect"));SetActorEnableCollision(false);}
AHeliosBuLianShiP90Effect* AHeliosBuLianShiP90Effect::Spawn(AActor* Owner,const FString& Name,const FVector& At,const FRotator& Rotation,bool FP,float PlaybackRate,const FString& LibraryPath)
{
    if(!Owner)return nullptr;FActorSpawnParameters P;P.Owner=Owner;P.Instigator=Owner->GetInstigator();
    auto* A=Owner->GetWorld()->SpawnActor<AHeliosBuLianShiP90Effect>(At,Rotation,P);
    if(!A->Configure(Name,FP,LibraryPath)){A->Destroy();return nullptr;}A->PlaybackRate=FMath::Max(.01f,PlaybackRate);A->Tick(0);return A;
}
bool AHeliosBuLianShiP90Effect::Configure(const FString& Name,bool FP,const FString& LibraryPath)
{
    Library=LoadObject<UHeliosSSXEffectLibrary>(nullptr,*LibraryPath);if(!Library)return false;
    const TSharedPtr<FJsonObject> Root=Library->GetParsedRecipe();if(!Root)return false;
    const TSharedPtr<FJsonObject>* Recipe=nullptr;if(!Root->TryGetObjectField(Name,Recipe))return false;
    Duration=(*Recipe)->GetNumberField(TEXT("duration"));
    bFirstPerson=FP;bMuzzle=Name.Contains(TEXT("Gunfire_P90"));bSoftTransient=bMuzzle||Name==TEXT("FX_P90_ShuangJiLong_Kill");
    // Retain the source burst layers, but vary the camera-plane roll between
    // shots. The offline source seed otherwise stamps the same flash repeatedly.
    ShotRoll=bMuzzle?FMath::DegreesToRadians(FMath::FRandRange(-16.f,16.f)):0.f;
    double Loop=0;(*Recipe)->TryGetNumberField(TEXT("loop_seconds"),Loop);LoopSeconds=FMath::Max(0.f,float(Loop));
    for(const auto& V:(*Recipe)->GetArrayField(TEXT("tracks")))
    {
        const auto D=V->AsObject();FHeliosSSXEffectTrack T;T.Start=D->GetNumberField(TEXT("start"));T.Life=D->GetNumberField(TEXT("life"));T.Billboard=D->GetBoolField(TEXT("billboard"));
        D->TryGetBoolField(TEXT("world_space"),T.WorldSpace);
        D->TryGetBoolField(TEXT("uv_is_sheet"),T.UVIsSheet);
        D->TryGetBoolField(TEXT("enabled"),T.Enabled);
        for(const auto& F:D->GetArrayField(TEXT("frames"))){TArray<float> Row;for(const auto& N:F->AsArray())Row.Add(N->AsNumber());if(Row.Num()!=18&&Row.Num()!=26&&Row.Num()!=30)return false;if(!T.Frames.IsEmpty()&&Row.Num()!=T.Frames[0].Num())return false;T.Frames.Add(MoveTemp(Row));}
        auto* C=NewObject<UStaticMeshComponent>(this);C->SetupAttachment(RootComponent);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);
          C->SetBoundsScale(4.f); // Original vertex displacement may extend past static bind bounds.
          C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*D->GetStringField(TEXT("mesh"))));
        auto* M=LoadObject<UMaterialInterface>(nullptr,*D->GetStringField(TEXT("material")));if(!C->GetStaticMesh()||!M)return false;
        // Register hidden: even the spawn frame cannot show a disabled layer or
        // an uninitialized particle. Tick(0) presents the enabled source tracks.
        C->SetVisibility(false);C->SetMaterial(0,M);C->RegisterComponent();T.Mesh=C;T.Material=C->CreateDynamicMaterialInstance(0);
        if(FP){C->SetOnlyOwnerSee(true);C->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);}
        Tracks.Add(MoveTemp(T));
        if(auto* Dynamic=Tracks.Last().Material.Get())
        {
            Dynamic->SetScalarParameterValue(TEXT("SpriteSoftness"),bSoftTransient&&D->GetStringField(TEXT("mesh")).Contains(TEXT("SourceBillboard"))?1.f:0.f);
            Dynamic->SetScalarParameterValue(TEXT("SoftDepth"),FP?0.f:1.f);
        }
    }
    return !Tracks.IsEmpty();
}
void AHeliosBuLianShiP90Effect::FollowTravelingEmitter()
{
    for(auto& Track:Tracks){Track.WorldSpace=false;Track.Anchored=false;}
    Tick(0);
}
void AHeliosBuLianShiP90Effect::FollowLocalViewTranslation()
{
    bFollowLocalViewTranslation=true;
    SetTickGroup(TG_PostUpdateWork);
    if(auto* Parent=RootComponent->GetAttachParent())AddTickPrerequisiteComponent(Parent);
    Tick(0);
}
void AHeliosBuLianShiP90Effect::NormalizeLocalWaveView(AHeliosArenaCharacter* Player,const FVector& BirthEye)
{
    WaveViewPlayer=Player;WaveViewBirthEye=BirthEye;
    SetTickGroup(TG_PostUpdateWork);
    Tick(0); // A newly spawned effect can render before its first scheduled tick.
}
void AHeliosBuLianShiP90Effect::Tick(float Dt)
{
    Super::Tick(Dt);Age+=Dt*PlaybackRate;
    if(auto* Player=WaveViewPlayer.Get();Player&&Player->IsLocallyControlled())
    {
        if(auto* Emitter=GetAttachParentActor())
        {
            const float T=FMath::Clamp((Age/PlaybackRate-.03f)/.22f,0.f,1.f);
            const float Weight=1.f-T*T*(3.f-2.f*T);
            // Native near-emitter layers cross the camera on their birth frame.
            // Present the local wave ahead of that plane, then continuously join
            // its unchanged world trajectory. The offset fades slowly enough
            // that the displayed wave keeps traveling forward throughout.
            SetActorLocation(Emitter->GetActorLocation()+(Player->FirstPersonCamera->GetComponentLocation()-WaveViewBirthEye+Emitter->GetActorForwardVector()*400.f)*Weight);
            if((FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKTemporalQA"))||FParse::Param(FCommandLine::Get(),TEXT("HeliosInputControlQA")))&&Age/PlaybackRate<.2f)
                UE_LOG(LogTemp,Display,TEXT("WAVE_VIEW age=%.6f speed=%.3f relative=%s birth=%s current=%s emitter=%s"),Age/PlaybackRate,Player->GetVelocity().Size(),*(GetActorLocation()-Player->FirstPersonCamera->GetComponentLocation()).ToString(),*WaveViewBirthEye.ToString(),*Player->FirstPersonCamera->GetComponentLocation().ToString(),*Emitter->GetActorLocation().ToString());
        }
    }
    if(!IsValid(GetOwner())){Destroy();return;}
    if(LoopSeconds>0)Age=FMath::Fmod(Age,LoopSeconds);
    else if(Age>Duration){Destroy();return;}
    for(auto& T:Tracks)
    {
        auto* C=T.Mesh.Get();if(!C||T.Frames.IsEmpty())continue;
        const float Phase=LoopSeconds>0?FMath::Fmod(Age-T.Start+LoopSeconds,LoopSeconds):Age-T.Start;
        if(LoopSeconds>0&&Phase<T.LastPhase)T.Anchored=false;
        T.LastPhase=Phase;
        const bool Visible=T.Enabled&&Phase>=0&&Phase<T.Life;
        C->SetVisibility(Visible);if(!Visible)continue;
        const float Frame=FMath::Clamp(Phase*60.f,0.f,float(T.Frames.Num()-1));const int32 L=FMath::FloorToInt(Frame),H=FMath::Min(L+1,T.Frames.Num()-1);const float Alpha=Frame-L;
        auto V=[&](int32 I){return FMath::Lerp(T.Frames[L][I],T.Frames[H][I],Alpha);};
        const FQuat Low(T.Frames[L][3],T.Frames[L][4],T.Frames[L][5],T.Frames[L][6]);
        const FQuat High(T.Frames[H][3],T.Frames[H][4],T.Frames[H][5],T.Frames[H][6]);
        const FQuat Q=FQuat::Slerp(Low.GetNormalized(),High.GetNormalized(),Alpha).GetNormalized();
        const FTransform Sample(Q,FVector(V(0),V(1),V(2)),FVector(V(7),V(8),V(9)));
        if(T.WorldSpace)
        {
            if(!T.Anchored){T.BirthTransform=GetActorTransform();T.BirthViewLocation=GetActorLocation();T.Anchored=true;}
            FTransform Anchor=T.BirthTransform;
            if(bFollowLocalViewTranslation)Anchor.AddToTranslation(GetActorLocation()-T.BirthViewLocation);
            C->SetWorldTransform(Sample*Anchor);
        }
        else C->SetRelativeTransform(Sample);
        if(T.Billboard)if(auto* Cam=UGameplayStatics::GetPlayerCameraManager(this,0))
        {
            const auto* WavePlayer=WaveViewPlayer.Get();
            const FVector Eye=WavePlayer?WavePlayer->FirstPersonCamera->GetComponentLocation():Cam->GetCameraLocation();
            const FQuat Facing=(Eye-C->GetComponentLocation()).Rotation().Quaternion();
            FQuat Swing,Twist;Q.ToSwingTwist(FVector::ForwardVector,Swing,Twist);
            const FQuat Roll(FVector::ForwardVector,2.f*FMath::Atan2(Twist.X,Twist.W)+ShotRoll);
            C->SetWorldRotation((Facing*Roll).GetNormalized());
        }
        if(auto* M=T.Material.Get())
        {
            M->SetVectorParameterValue(TEXT("Tint"),FLinearColor(V(10),V(11),V(12),V(13)));
            // A flipbook frame is a discrete cell. Interpolating its UV offset
            // pans through neighbouring cells and exposes hard rectangular seams.
            M->SetVectorParameterValue(TEXT("UVTransform"),T.UVIsSheet?FLinearColor(T.Frames[L][14],T.Frames[L][15],T.Frames[L][16],T.Frames[L][17]):FLinearColor(V(14),V(15),V(16),V(17)));
            float Weight=1.f;
            if(bSoftTransient)
            {
                const float In=FMath::Clamp(Phase/FMath::Min(bMuzzle?.006f:.018f,T.Life*.12f),0.f,1.f);
                const float Out=FMath::Clamp((T.Life-Phase)/FMath::Min(bMuzzle?.018f:.12f,T.Life*.28f),0.f,1.f);
                Weight=In*In*(3.f-2.f*In)*Out*Out*(3.f-2.f*Out);
            }
            M->SetScalarParameterValue(TEXT("PresentationOpacity"),Weight);
        }
        if(auto* M=T.Material.Get())
        {
            if(T.Frames[L].Num()==30)M->SetVectorParameterValue(TEXT("ParticleColor"),FLinearColor(V(26),V(27),V(28),V(29)));
            M->SetScalarParameterValue(TEXT("SourceTime"),Age);
            M->SetScalarParameterValue(TEXT("UVIsSheet"),T.UVIsSheet?1.f:0.f);
            if(T.Frames[L].Num()>=26){M->SetVectorParameterValue(TEXT("Custom1"),FLinearColor(V(18),V(19),V(20),V(21)));M->SetVectorParameterValue(TEXT("Custom2"),FLinearColor(V(22),V(23),V(24),V(25)));}
        }
    }
}

int32 AHeliosBuLianShiP90Effect::VisibleTrackCount() const
{
    int32 Count=0;for(const auto& T:Tracks)if(T.Mesh.IsValid()&&T.Mesh->IsVisible())++Count;return Count;
}

void AHeliosBuLianShiP90Effect::ResetBirthAnchors()
{
    for(auto& T:Tracks){T.Anchored=false;T.LastPhase=-1;}Tick(0);
}
