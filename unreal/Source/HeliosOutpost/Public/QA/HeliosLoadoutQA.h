#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/HeliosLoadout.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Pets/HeliosHumanoidPet.h"
#include "HeliosLoadoutQA.generated.h"

class AHeliosArenaCharacter;
class AHeliosEnemyActor;
class AHeliosHumanoidPet;
class UHeliosWeaponDefinition;
class UHeliosCharacterDefinition;
class FJsonValue;
class UDamageType;
class AController;
class AShooterWeapon;
class UTexture2D;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;

/** Preserves the enemy damage chain and exposes its applied damage to the QA observer. */
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosLoadoutQATarget : public AHeliosEnemyE01
{
    GENERATED_BODY()
public:
    virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;
};

/** Read-only access to the inherited production readiness values for this fixture. */
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosLoadoutQAPet : public AHeliosHumanoidPet
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;
    bool bQAWaitForIdle = false;
    float QAHitPoseRemaining() const { return HitPoseRemaining; }
    float QAAttackAge() const { return AttackAge; }
    int32 QAAttackSerial() const { return AttackSerial; }
};

/** Runs the real player/companion combination matrix in an isolated QA project. */
UCLASS(NotBlueprintable)
class HELIOSOUTPOST_API AHeliosLoadoutQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosLoadoutQA();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    void Check(const FString& Name, bool bPass);
    void Capture(const TCHAR* Phase);
    void DeferAfterCapture(TFunction<void()> Mutation);
    void DiagnoseCompanionReadiness(bool bFinal);
    void DiagnoseModeTransforms();
    void NextCombination();
    void Finish();
    void CleanupHolderResidue(AActor* Holder);
    void PauseCompanion();
    bool IsCompanionActuallyIdle() const;
    bool IsNativeDriverHidden() const;
    void WarmCaptureResources();
    void DiagnoseIdleWait(const TCHAR* Stage, bool bFinal);
    void CompleteZXKModeFixture();
    bool CaptureThirdPersonHands(const FString& State);
    void BeginPlayerPrimary();
    void BeginCompanionReloadTest();
    void FinishCompanionReloadTest();
    void BeginHunterBasicTest(bool bHeavy);
    void TickHunterBasicTest(bool bHeavy);
    bool IsHunterSourcePose(bool bHeavy) const;
    void BeginZXKModeTest();
    void HideModeTargetForPhotography();
    void RestoreModeTargetVisibility();
    void StartZXKModePhase(int32 Stage);
    void TickZXKModeTest();
    void FinishZXKModeTest();
    void FinishPairTests();
    void BeginCharacterFormTest();
    void TickCharacterFormTest();
    void BeginYunCheersTest();
    void TickYunCheersTest();
    void AimAtFixtureTarget();
    void DiagnoseYunCheers(const TCHAR* Stage);
    bool HasSelectedFormVoice(AActor* Holder,const FHeliosLoadout& Loadout) const;
    double WeaponCooldownStamp(AShooterWeapon* Weapon) const;
    bool HasZXKThirdPersonClock() const;
    bool HasZXKThirdPersonPose(const FString& Fragment) const;
    bool HasZXKThirdPersonDartContact() const;
    FHeliosLoadout DifferentCharacterLoadout(const FHeliosLoadout& Current) const;
    UFUNCTION() void OnTargetDamage(AActor* Damaged,float Damage,const UDamageType* Type,AController* InstigatorController,AActor* Causer);
    UPROPERTY() TObjectPtr<AHeliosArenaCharacter> Player;
    UPROPERTY() TObjectPtr<AHeliosEnemyActor> Target;
    UPROPERTY() TObjectPtr<AHeliosHumanoidPet> Companion;
    UPROPERTY() TArray<TObjectPtr<UHeliosCharacterDefinition>> Characters;
    UPROPERTY() TArray<TObjectPtr<UHeliosWeaponDefinition>> Weapons;
    UPROPERTY() TArray<FHeliosLoadout> CharacterForms;
    UPROPERTY() TArray<TObjectPtr<UObject>> WarmedCaptureAssets;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> WarmedCaptureTextures;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> HandsRenderTarget;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> HandsCapture;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    TFunction<void()> DeferredCaptureMutation;
    FString PendingCaptureName;
    uint64 PendingCaptureFrame=0,ResourcesReadyFrame=0;
    double NextIdleWaitDiagnosticAt=0;
    double PendingCaptureReadyDeadline=0;
    bool bInitialRendererWarm=false;
    uint64 CaptureReleaseFrame=0;
    double CaptureReleaseTime=0,NextReadinessLogAt=0,CompanionReloadCaptureAt=0,NextCheersDiagnosticAt=0;
    int32 ObservedCompanionAttackStarts=0,ObservedCompanionDamageHits=0;
    FName LastObservedCompanionAction;
    bool bCompanionReloadCaptured=false,bMainSkillIssued=false;
    bool bModeTransformFailureRecorded=false;
    FString Out;
    FString CaseName;
    int32 CharacterIndex=0, WeaponIndex=-1, Phase=-1, Failures=0, Completed=0;
    float Age=0;
    float CompanionDamage=0,PlayerDamage=0,UnattributedDamage=0,PlayerPrimaryDamageStart=0;
    float HunterDamage=0,HunterTimeout=5.f,ReloadTimeout=30.f;
    int32 CompanionInitialAmmo=0,PlayerPrimaryInitialAmmo=0,HunterContacts=0;
    int32 NativeCompanionMagazine=0,CompanionReloadsStart=0;
    bool bHunterStarted=false,bHunterContactDuringCooldown=false,bHunterPoseCaptured=false,bReloadSwapAttempted=false;
    TWeakObjectPtr<AShooterWeapon> ReloadPrimary;
    TWeakObjectPtr<AShooterWeapon> ModePrimary;
    TMap<TWeakObjectPtr<AActor>,bool> ModeTargetVisibility;
    float ModeDamageStart=0;
    int32 ModeDartAmmo=0;
    bool bModePoseSeen=false,bModeContactSeen=false,bModeClockSeen=false,bModeIssued=false;
    int32 ModeCompleted=0;
    int32 FormCompleted=0,CheersCompleted=0,FormCycle=0;
    int32 FormAmmo=0,FormPetAmmo=0,GestureAudioStart=0,GestureEffectStart=0,PetGestureAudioStart=0,PetGestureEffectStart=0;
    int32 NaturalGestureAudio=0,NaturalGestureEffect=0,NaturalPetGestureAudio=0,NaturalPetGestureEffect=0;
    float FormHealth=0,FormPetHealth=0,FormRefire=0,FormPetRefire=0;
    double FormCooldown=0,FormPetCooldown=0;
    TWeakObjectPtr<AShooterWeapon> FormPrimary,FormPetPrimary;
    bool bFormReloadIssued=false,bFormReloadStarted=false,bGestureCaptured=false;
    FHeliosLoadout PairLoadout;
    bool bFinished=false;
    FHeliosLoadout OriginalLoadout;
};
