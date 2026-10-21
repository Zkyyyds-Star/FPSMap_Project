#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/HeliosLoadout.h"
#include "HeliosBioIterationQA.generated.h"
class AHeliosBioHotelGameMode;
class AHeliosBioSupply;
class AShooterWeapon;
class UHeliosBioSupplyWeaponDefinition;
class FJsonObject;
class FJsonValue;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AHeliosBioMovingPlatform;
class UPrimitiveComponent;

/** Isolated camera/inventory transaction regression. No new-weapon readiness claim. */
UCLASS()
class HELIOSOUTPOST_API AHeliosBioIterationQA : public AActor
{
    GENERATED_BODY()
public:
    AHeliosBioIterationQA();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY() TObjectPtr<AHeliosBioHotelGameMode> Mode;
    UPROPERTY() TObjectPtr<AHeliosBioSupply> FixtureSupply;
    UPROPERTY() TObjectPtr<UHeliosBioSupplyWeaponDefinition> UnreadyDefinition;
    UPROPERTY() TObjectPtr<UHeliosBioSupplyWeaponDefinition> WrongDefinition;
    TWeakObjectPtr<AShooterWeapon> OriginalWeapon;
    FHeliosLoadout OriginalLoadout;
    FString OriginalFP,OriginalTP,Out,ExpectedMap,Blocker;
    FVector StandingFeet=FVector::ZeroVector;
    TArray<TSharedPtr<FJsonValue>> Checks,Evidence;
    double Started=0,Next=0,StageAt=0;
    int32 Stage=0,Failures=0,RestartSerial=0,InitialWidth=0,InitialHeight=0,OriginalMagazineAmmo=0,RestartFailuresBefore=0;
    bool bFinished=false,bFloating=false,bCameraVerified=false,bInventoryVerified=false,bRestartVerified=false;
    void Check(const FString& Name,bool Passed);
    void FreezeIsolation();
    FVector Feet() const;
    TSharedRef<FJsonObject> Snapshot(const TCHAR* Event) const;
    bool CheckView(const TCHAR* Event,float ExpectedEye,int32 ExpectedWidth=0,int32 ExpectedHeight=0);
    bool PrepareSupplyFixture();
    bool CheckRejectedPickup(const TCHAR* Case,UHeliosBioSupplyWeaponDefinition* Definition,bool TestPickup);
    void TestInventory();
    void Finish(const FString& Why=FString());
    bool bWaterMode=false,bFootMode=false,bWaterLandingMode=false,bWaterInitialized=false;
    bool bSolesMeasured=false,bSolesAligned=false;
    int32 VisualStage=0,VisualViewIndex=0,VisualPhase=0,WaterMaterialSlot=INDEX_NONE,SoleSamples=0;
    double VisualNext=0,VisualAt=0,FlowTimeStart=0;
    TArray<FVector> WaterVertices;
    TArray<float> WaterRed;
    TArray<FIntVector> WaterTriangles;
    FVector WaterCollisionMin=FVector::ZeroVector,WaterCollisionMax=FVector::ZeroVector;
    mutable TArray<TSharedPtr<FJsonValue>> VisualDiagnostics;
    mutable int32 WaterRayLogged=0;
    mutable int32 WaterClearRays=0,WaterTerminalRays=0,WaterOpaqueRays=0;
    int32 LastVisualStage=INDEX_NONE,LastVisualView=INDEX_NONE,LastVisualPhase=INDEX_NONE;
    double LastVisualLog=0;
    double LastWaterSupportLog=0;
    TArray<TSharedPtr<FJsonValue>> SoleEvidence,WaterViews;
    TSharedPtr<FJsonObject> CurrentWaterView;
    TWeakObjectPtr<UStaticMeshComponent> WaterComponent;
    UPROPERTY() TObjectPtr<UMaterialInterface> OriginalWaterMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WaterMaterialOverride;
    UPROPERTY() TObjectPtr<AHeliosBioMovingPlatform> VisualPlatform;
    bool bPlatformWasRunning=false;
    FVector VisualTarget=FVector::ZeroVector;
    FString PendingVisualFile;
    void TickVisualFixture(float Dt);
    bool LoadWaterWitness();
    bool PlaceWaterCamera(int32 View);
    TSharedRef<FJsonObject> WaterSupportEvidence(const TCHAR* Event) const;
    int32 VisibleWaterCount(const FVector& Eye,FVector& BestTarget) const;
    bool OriginalWaterRayVisible(const FVector& Eye,const FVector& Target) const;
    void CaptureWaterFrame(const TCHAR* Phase);
    void SampleActualSoles();
    void RestoreVisualFixture();
    void FinishVisualFixture(const FString& Why=FString());
    TArray<FVector> WaterLandingPoints;
    TWeakObjectPtr<UPrimitiveComponent> WaterLandingSupport;
    TArray<TSharedPtr<FJsonValue>> WaterLandingEvidence;
    FVector WaterLandingFeet=FVector::ZeroVector;
    float WaterLandingStartZ=0,WaterLandingMaxZ=0;
    int32 WaterLandingIndex=0;
    double WaterLandingStarted=0;
    bool bWaterInitialFalling=false,bWaterJumpAirborne=false;
    void TickWaterLanding(float Dt);
    void FinishWaterLanding(const FString& Why=FString());
};
