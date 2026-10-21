#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "HeliosSSXEffect.generated.h"

UCLASS()
class HELIOSOUTPOST_API UHeliosSSXEffectLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) FString Recipe;
    UPROPERTY(EditAnywhere) TArray<TObjectPtr<UObject>> Dependencies;
    TSharedPtr<class FJsonObject> GetParsedRecipe();
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
private:
    TSharedPtr<class FJsonObject> ParsedRecipe;
};

struct FHeliosSSXEffectTrack
{
    float Start=0,Life=0,LastPhase=-1;
    bool Enabled=true,Billboard=false,WorldSpace=false,Anchored=false,UVIsSheet=false;
    FTransform BirthTransform=FTransform::Identity;
    FVector BirthViewLocation=FVector::ZeroVector;
    TArray<TArray<float>> Frames;
    TWeakObjectPtr<class UStaticMeshComponent> Mesh;
    TWeakObjectPtr<class UMaterialInstanceDynamic> Material;
};

/** Playback of converted source meshes, particle samples and material animation. */
UCLASS()
class HELIOSOUTPOST_API AHeliosSSXEffect : public AActor
{
    GENERATED_BODY()
public:
    AHeliosSSXEffect();
    static AHeliosSSXEffect* Spawn(AActor* Owner,const FString& Name,const FVector& At,const FRotator& Rotation,bool FP,float PlaybackRate=1.f,const FString& LibraryPath=TEXT("/Game/Helios/CFMSunShangXiang/Effects/DA_SSX_Effects.DA_SSX_Effects"));
    virtual void Tick(float Dt) override;
    // Traveling projectile geometry follows its emitter; other world-space
    // particle instances retain their authored birth transforms.
    void FollowTravelingEmitter();
    /** Explicit offline lifetime bridge; source preview samples are unchanged. */
    void KeepTravelingEmitterUntilOwnerEnds();
    // Local blade trails retain their birth rotation and source particle motion,
    // while sharing the attached first-person mesh's camera translation.
    void FollowLocalViewTranslation();
    void NormalizeLocalWaveView(class AHeliosArenaCharacter* Player,const FVector& BirthEye);
    bool IsLooping() const { return LoopSeconds>0; }
    int32 VisibleTrackCount() const;
    void ResetBirthAnchors();
    // Keep the single authored wave cycle aligned with configurable travel life.
    void MatchTravelLifetime(float Seconds)
    {
        float WaveEnd=0.f;
        for(const auto& T:Tracks)if(T.Enabled&&!T.Billboard)WaveEnd=FMath::Max(WaveEnd,T.Start+T.Life);
        PlaybackRate=(WaveEnd>0.f?WaveEnd:Duration)/FMath::Max(Seconds,.05f);
    }
private:
    bool Configure(const FString& Name,bool FP,const FString& LibraryPath);
    float Age=0,Duration=0,PlaybackRate=1.f,LoopSeconds=0;
    uint64 BirthFrame=0;
    TArray<FHeliosSSXEffectTrack> Tracks;
    TWeakObjectPtr<class AHeliosArenaCharacter> WaveViewPlayer;
    FVector WaveViewBirthEye=FVector::ZeroVector;
    bool bFollowLocalViewTranslation=false;
    bool bTravelingPreviewUntilOwnerEnds=false;
    UPROPERTY(Transient) TObjectPtr<UHeliosSSXEffectLibrary> Library;
};
