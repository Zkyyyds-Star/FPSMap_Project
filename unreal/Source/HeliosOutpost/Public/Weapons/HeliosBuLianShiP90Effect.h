#pragma once
#include "CoreMinimal.h"
#include "Weapons/HeliosSSXEffect.h"
#include "HeliosBuLianShiP90Effect.generated.h"

/** Playback of converted source meshes, particle samples and material animation. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBuLianShiP90Effect : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBuLianShiP90Effect();
    static AHeliosBuLianShiP90Effect* Spawn(AActor* Owner,const FString& Name,const FVector& At,const FRotator& Rotation,bool FP,float PlaybackRate=1.f,const FString& LibraryPath=TEXT("/Game/Helios/CFMBuLianShiP90/Effects/DA_P90_Effects.DA_P90_Effects"));
    virtual void Tick(float Dt) override;
    // Traveling projectile geometry follows its emitter; other world-space
    // particle instances retain their authored birth transforms.
    void FollowTravelingEmitter();
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
    TArray<FHeliosSSXEffectTrack> Tracks;
    TWeakObjectPtr<class AHeliosArenaCharacter> WaveViewPlayer;
    FVector WaveViewBirthEye=FVector::ZeroVector;
    bool bFollowLocalViewTranslation=false;
    bool bSoftTransient=false,bMuzzle=false,bFirstPerson=false;
    float ShotRoll=0;
    UPROPERTY(Transient) TObjectPtr<UHeliosSSXEffectLibrary> Library;
};
