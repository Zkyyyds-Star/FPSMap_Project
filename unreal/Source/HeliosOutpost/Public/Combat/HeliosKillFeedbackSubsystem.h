#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HeliosKillFeedbackSubsystem.generated.h"

class AHeliosEnemyActor;
class APawn;
class AController;
class UTexture2D;
class USoundBase;
class UAudioComponent;

enum class EHeliosKillBadge : uint8 { Single, Double, Triple, Quad, Penta, Hexa, First, Boss, Count };

/** Pure game-time reducer, shared by runtime and boundary tests. */
struct HELIOSOUTPOST_API FHeliosKillChain
{
    int32 Streak=0, Total=0, DisplayStreak=0, Serial=0;
    double LastKill=-1.e30, PendingAt=0, StartedAt=-1.e30;
    bool bPending=false;
    EHeliosKillBadge Pending=EHeliosKillBadge::Single, Display=EHeliosKillBadge::Single;
    void Record(double Now, bool bBoss, double WindowSeconds=6.0);
    bool Flush(double Now);
    void Reset() { *this=FHeliosKillChain(); }
    static int32 Priority(EHeliosKillBadge Kind) { return int32(Kind); }
    double Duration() const { return Display==EHeliosKillBadge::Boss?2.0:1.4; }
    bool IsVisible(double Now) const { return Serial>0 && Now>=StartedAt && Now-StartedAt<Duration(); }
};

/** Local presentation only. Never changes combat health or director scoring. */
UCLASS()
class HELIOSOUTPOST_API UHeliosKillFeedbackSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    void ReportKill(AHeliosEnemyActor* Victim, AController* Instigator, AActor* Causer);
    void ResetRun();
    void SuspendAudio();
    void SetSoundEnabled(bool bEnabled);
    bool IsAnnouncing() const;
    float OtherVoiceGain() const { return IsAnnouncing()?0.18f:1.f; }
    bool IsVisible() const;
    const FHeliosKillChain& GetState() const { return Chain; }
    UTexture2D* GetTexture() const;
    bool HasAllAssets() const;
    static const TCHAR* AssetKey(EHeliosKillBadge Kind);
    static FString Label(EHeliosKillBadge Kind);
    static bool IsDirectPlayerDamage(const APawn* Player, const AController* Instigator, const AActor* Causer);
private:
    void Advance(double Now);
    bool IsLiveRun() const;
    FHeliosKillChain Chain;
    bool bSoundEnabled=true;
    int32 AudioPriority=-1;
    TSet<TWeakObjectPtr<AHeliosEnemyActor>> ReportedDeaths;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> Textures;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> Sounds;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Audio;
};
