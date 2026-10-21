#pragma once
#include "CoreMinimal.h"
#include "Combat/HeliosKillFeedbackSubsystem.h"
#include "Components/ActorComponent.h"
#include "Player/HeliosLoadout.h"
#include "HeliosCharacterPresentationComponent.generated.h"
class AShooterWeapon;
class UAnimSequence;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UHeliosCharacterActionProfile;
class UHeliosSSXVoiceComponent;
class UAudioComponent;
class AHeliosSSXEffect;
struct FHeliosCharacterWeaponPresentation;
struct FHeliosCharacterGesture;

/** Owns selected human appearance for both player arms and companion bodies. */
UCLASS(ClassGroup=(Helios), meta=(BlueprintSpawnableComponent))
class HELIOSOUTPOST_API UHeliosCharacterPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UHeliosCharacterPresentationComponent();
    /** Asset pipeline finalizer; refuses original game assets. */
    UFUNCTION(BlueprintCallable, Category="Helios|Presentation Assets")
    static bool FinalizeDerivedAnimation(UAnimSequence* Animation);
    UFUNCTION(BlueprintCallable, Category="Helios|Presentation Assets")
    static bool SetDerivedFingertipSocket(USkeletalMesh* Mesh,FName SocketName,FName BoneName,FVector LocalLocation);
    /** Read-only editor geometry for measuring actual imported skin surfaces. */
    UFUNCTION(BlueprintCallable, Category="Helios|Presentation QA")
    static FString ExportSkinGeometryForQA(USkeletalMesh* Mesh);
    bool ConfigureFirstPerson(FName CharacterId,FName VariantId,FName WeaponId,USceneComponent* CameraRoot,AShooterWeapon* InWeapon);
    bool ConfigureThirdPerson(FName CharacterId,FName VariantId,FName WeaponId,USkeletalMeshComponent* Body);
    bool ApplyLoadout(USkeletalMeshComponent* Body,const FHeliosLoadout& Loadout);
    bool ApplyLoadout(USceneComponent* CameraRoot,const FHeliosLoadout& Loadout,AShooterWeapon* InWeapon);
    bool AttachWeapon(AShooterWeapon* InWeapon);
    void BindWeaponSource(AShooterWeapon* InWeapon);
    bool SetPose(FName Action,float Seconds,bool bLoop);
    bool PresentAction(FName Action,float Seconds,bool bLoop){return SetPose(Action,Seconds,bLoop);}
    bool PresentSourcePose(UAnimSequence* Source,float Seconds,bool bLoop);
    uint64 GetPoseCommitFrame() const{return PoseCommitFrame;}
    uint32 GetPoseCommitCount() const{return PoseCommitCount;}
    USkeletalMeshComponent* GetArmsMesh() const{return VisibleMesh;}
    USkeletalMeshComponent* GetWeaponMesh() const{return WeaponVisual;}
    FName GetPresentedWeaponId() const{return Equipped.PrimaryWeaponId;}
    UStaticMeshComponent* GetAlternateWeaponMesh() const{return AlternateWeaponVisual;}
    FTransform GetAnchorTransform(FName Anchor) const;
    float GetRecoveryDuration(FName Pose,float Fallback) const;
    void PlayCharacterCue(FName Cue);
    UHeliosSSXVoiceComponent* GetCharacterVoice() const{return Voice;}
    bool BeginCharacterGesture(FName Gesture);
    void CancelCharacterGesture();
    bool IsCharacterGesturePlaying() const{return !Gesture.IsNone();}
    float GetCharacterGestureAge() const{return GestureAge;}
    float GetCharacterGestureDuration() const{return GestureDuration;}
    int32 GetCharacterGestureAudioEventCount() const{return GestureAudioEvents;}
    int32 GetCharacterGestureEffectEventCount() const{return GestureEffectEvents;}
    USkeletalMeshComponent* GetGestureArmsMesh() const{return GestureArms;}
    USkeletalMeshComponent* GetGestureCupMesh() const{return GestureCup;}
    void SetVisible(bool bVisible);
    void SetUmbrellaOverride(bool Active);
    bool IsReady() const{return bReady;}
    bool IsSourceActionPlaying() const{return bSourceAction;}
    float GetRecoveryPoseLockRemaining() const{return PoseLockRemaining;}
    float GetFirstPersonViewScale() const;
    float GetFirstPersonNearClip() const{return .1f;}
    virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class AHeliosQingYaQA;
    void CommitThirdPersonPose();
    bool Configure(FName CharacterId,FName VariantId,FName WeaponId);
    void ConfigureVoice(FName CharacterId,FName VariantId);
    UAnimSequence* ResolvePose(FName Action) const;
    void UpdateThirdPersonWeaponMode(USkeletalMeshComponent* Driver);
    bool PrepareYunGesture();
    bool BeginProfileGesture(const FHeliosCharacterGesture& Definition);
    bool SampleGestureMesh(USkeletalMeshComponent* Mesh,UAnimSequence* Clip,float Seconds);
    void TickCharacterGesture(float Dt);
    UPROPERTY(Transient) TObjectPtr<UHeliosCharacterActionProfile> Profile;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> VisibleMesh;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> WeaponVisual;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> AlternateWeaponVisual;
    UPROPERTY(Transient) TObjectPtr<AShooterWeapon> SourceWeapon;
    UPROPERTY(Transient) TObjectPtr<UHeliosSSXVoiceComponent> Voice;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> CameraRoot;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> GestureArms;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> GestureCup;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> GestureArmsClip;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> GestureCupClip;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> GestureAudio;
    UPROPERTY(Transient) TObjectPtr<AHeliosSSXEffect> GestureEffect;
    TMap<TWeakObjectPtr<AActor>,bool> GestureActorVisibility;
    TWeakObjectPtr<USkeletalMeshComponent> GestureHiddenDriver;
    FHeliosKillChain QingYaLocalVoiceChain;
    double QingYaLastVoiceKillTime=-1.e30;
    int32 QingYaLastVoiceKillTotal=-1;
    FHeliosLoadout Equipped;
    const FHeliosCharacterWeaponPresentation* Pair=nullptr;
    bool bThirdPerson=false,bReady=false,bOwnsMesh=false,bSourceAction=false;
    bool bUmbrellaOverride=false,bUmbrellaBodyVisible=false,bUmbrellaWeaponVisible=false,bUmbrellaAlternateVisible=false;
    int32 ThirdPersonWeaponMode=0;
    FName Gesture;
    FName LockedPose;
    float PoseLockRemaining=0.f;
    FName LastMissingAction;
    FName PendingPose;
    float PendingPoseSeconds=0;
    bool bPendingPoseLoop=true,bCommittingPose=false,bResetPoseHistory=false;
    uint64 PoseCommitFrame=MAX_uint64;
    uint32 PoseCommitCount=0;
    float GestureAge=0.f;
    float GestureDuration=0.f;
    int32 GestureAudioEvents=0,GestureEffectEvents=0;
    bool bYunGesture=false,bGestureSoundPlayed=false;
    bool bProfileGesture=false;
    const FHeliosCharacterGesture* ActiveProfileGesture=nullptr;
    bool bSavedBodyVisible=false,bSavedWeaponVisible=false,bSavedAlternateVisible=false;
    bool bSavedDriverVisible=false;
};
