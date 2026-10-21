#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "HeliosGuanXiaoYuAbilityComponent.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimSequence;
class USoundBase;
class AShooterWeapon;
class UHeliosCharacterPresentationComponent;
class UHeliosSSXEffectLibrary;
class AHeliosSSXEffect;
struct FDamageEvent;

UCLASS(BlueprintType)
class HELIOSOUTPOST_API UHeliosGuanXiaoYuUmbrellaProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> FirstPersonMesh;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USkeletalMesh> ThirdPersonMesh;
    UPROPERTY(EditAnywhere) TMap<FName,TSoftObjectPtr<UAnimSequence>> FirstPersonClips;
    UPROPERTY(EditAnywhere) TMap<FName,TSoftObjectPtr<UAnimSequence>> ThirdPersonClips;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<UHeliosSSXEffectLibrary> EffectLibrary;
    UPROPERTY(EditAnywhere) FString LightEffect;
    UPROPERTY(EditAnywhere) FString HeavyEffect;
    // Reuse the original world-space shield in FP; Unity's 1P projection
    // places its shader-warped geometry hundreds of metres from the camera.
    UPROPERTY(EditAnywhere) FString FirstPersonShieldEffect = TEXT("FX_3P_San_GuanXiaoYuSP_HuDun");
    UPROPERTY(EditAnywhere) FString ThirdPersonShieldEffect = TEXT("FX_3P_San_GuanXiaoYuSP_HuDun");
    UPROPERTY(EditAnywhere) FVector FirstPersonShieldOffset = FVector(120,0,-20);
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> LightSound;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> HeavySound;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> ThirdPersonLightSound;
    UPROPERTY(EditAnywhere) TSoftObjectPtr<USoundBase> ThirdPersonHeavySound;
    UPROPERTY(EditAnywhere) float FirstPersonLightSoundTime = .0361738f;
    UPROPERTY(EditAnywhere) float FirstPersonHeavySoundTime = .2609344f;
    UPROPERTY(EditAnywhere) float ThirdPersonLightSoundTime = .0584348f;
    UPROPERTY(EditAnywhere) float ThirdPersonHeavySoundTime = .0994783f;
    // Original clip tip reaches the target at frames 32/34 and 65/66 (120 Hz).
    UPROPERTY(EditAnywhere) float FirstPersonLightContact = 32.f / 120.f;
    UPROPERTY(EditAnywhere) float ThirdPersonLightContact = 34.f / 120.f;
    UPROPERTY(EditAnywhere) float HeavyContact = 65.f / 120.f;
    UPROPERTY(EditAnywhere) float ThirdPersonHeavyContact = 66.f / 120.f;
    UPROPERTY(EditAnywhere) float FirstPersonLightRecovery = .2333336f;
    UPROPERTY(EditAnywhere) FTransform FirstPersonTransform = FTransform(FRotator(0,-90,0));
};

/** Pawn-owned clocks survive form/weapon presentation changes. Primary actors stay alive. */
UCLASS(ClassGroup=(Helios),meta=(BlueprintSpawnableComponent))
class HELIOSOUTPOST_API UHeliosGuanXiaoYuAbilityComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosGuanXiaoYuAbilityComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Fn) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool ToggleUmbrella();
    bool StartLightAttack();
    bool StartHeavyAttack();
    void StopLightAttack(){bLightHeld=false;}
    void Cancel(bool RestorePrimary=true);
    bool TryAbsorbDamage(float Amount,const FDamageEvent& Event,AController* Instigator,AActor* Causer);
    bool IsUsingUmbrella() const{return bUsingUmbrella;}
    bool IsAttacking() const{return !Action.IsNone();}
    USkeletalMeshComponent* GetVisibleMesh()const{return VisibleMesh;}
    AHeliosSSXEffect* GetShieldEffect()const{return ShieldEffect;}
    uint64 GetPoseCommitFrame()const{return PoseCommitFrame;}
    uint32 GetPoseCommitCount()const{return PoseCommitCount;}
    float ShieldRemaining() const;
    float CooldownRemaining() const;
    float AttackRecoveryRemaining() const;
    FString StatusText() const;
    FString LastError;
    int32 Strikes=0,Hits=0,Blocks=0,HeavyAttacks=0;
    float LightDamage=60,HeavyDamage=120,Range=260,SweepRadius=40,ShieldDuration=2,ShieldCooldown=10,ShieldAngle=120;
private:
    double Now() const;
    bool CanUse() const;
    bool Enter();
    bool BeginAttack(bool Heavy);
    void Strike();
    void Sample();
    void ShowShield(bool Visible);
    AHeliosSSXEffect* SpawnEffect(const FString& Name);
    void PlaySound(USoundBase* Sound);
    UAnimSequence* Clip(FName Name) const;
    AShooterWeapon* Primary() const;
    UHeliosCharacterPresentationComponent* Presentation() const;
    UPROPERTY(Transient) TObjectPtr<UHeliosGuanXiaoYuUmbrellaProfile> Profile;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> VisibleMesh;
    UPROPERTY(Transient) TObjectPtr<AHeliosSSXEffect> ShieldEffect;
    UPROPERTY(Transient) TArray<TObjectPtr<AHeliosSSXEffect>> Effects;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> HiddenDriver;
    TMap<TWeakObjectPtr<AActor>,bool> HiddenActors;
    FName Variant,Action;
    double AttackStarted=0,NextAttack=0,ShieldUntil=0,NextShield=0;
    float ContactTime=0,ActionDuration=0;
    uint64 PoseCommitFrame=MAX_uint64;
    uint32 PoseCommitCount=0;
    FTransform LastMeshTransform;
    bool bResetPoseHistory=true;
    bool bUsingUmbrella=false,bThirdPerson=false,bContactCommitted=false,bLightHeld=false,bDriverWasVisible=false,bAttackSoundPlayed=false;
};
