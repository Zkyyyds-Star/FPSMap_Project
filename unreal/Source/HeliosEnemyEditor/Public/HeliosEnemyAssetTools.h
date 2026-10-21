#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HeliosEnemyAssetTools.generated.h"
class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimSequence;
class USceneCaptureComponent2D;

UCLASS()
class HELIOSENEMYEDITOR_API UHeliosEnemyAssetTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Helios|Editor")
    static bool EvaluatePose(USkeletalMeshComponent* Component, UAnimSequence* Animation, float TimeSeconds);
    UFUNCTION(BlueprintCallable, Category="Helios|Editor")
    static bool CapturePose(USceneCaptureComponent2D* Capture);
    UFUNCTION(BlueprintCallable, Category="Helios|Editor")
    static int32 ApplyUpperBodyCorrection(UAnimSequence* Target, UAnimSequence* AuthoredSource);
    UFUNCTION(BlueprintCallable, Category="Helios|Editor")
    static FString DescribeSkeletalMesh(USkeletalMesh* Mesh);

    UFUNCTION(BlueprintCallable, Category="Helios|Editor")
    static bool EnsureSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, FVector Offset, FRotator Rotation);
};
