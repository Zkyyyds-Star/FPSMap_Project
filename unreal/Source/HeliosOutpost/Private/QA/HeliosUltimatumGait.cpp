#include "QA/HeliosUltimatumDemo.h"
#include "Pets/HeliosUltimatumPet.h"
#include "Pets/HeliosRifleAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimSequence.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

void AHeliosUltimatumDemo::GaitTick(float Dt)
{
    const bool BeforeGait=FParse::Param(FCommandLine::Get(),TEXT("GaitBefore"));
    int32 FPS=30;FParse::Value(FCommandLine::Get(),TEXT("FPS="),FPS);
    const FString Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/gait_revision")/FString::Printf(TEXT("%s_%d"),BeforeGait?TEXT("before"):TEXT("after"),FPS));
    auto* Anim=Cast<UHeliosRifleAnimInstance>(Pet->GetMesh()->GetAnimInstance());
    if(!Anim){Check(TEXT("gait_animation_instance"),false);Finish();return;}
    const float T=GaitSamples*Dt;
    const int32 Shot=FMath::Min(9,int32(T/3));
    const float Segment=T-Shot*3;
    const float TurnSpeed=FParse::Param(FCommandLine::Get(),TEXT("GaitRecord"))?90.f:180.f;
    FVector Velocity(160,0,0);float Yaw=0;
    if(Shot==3)Velocity=FVector(-130,0,0);
    if(Shot==4)Velocity=FVector(0,-130,0);
    if(Shot==5)Velocity=FVector(0,130,0);
    if(Shot==6)Velocity=FVector(420,0,0);
    if(Shot==7)Velocity=FVector(750,0,0);
    if(Shot==8){Velocity=FVector::ZeroVector;Yaw=Segment*TurnSpeed;}
    if(Shot==9){const float Speed=420*FMath::Sin(PI*Segment/3);Velocity=FVector(FMath::Max(0.f,Speed),0,0);}
    if(Segment<Dt*.5){Pet->SetActorLocation(Anchor);GaitLastFoot=FVector::ZeroVector;}
    Pet->SetActorLocation(Pet->GetActorLocation()+Velocity*Dt);
    Pet->SetActorRotation(FRotator(0,Yaw,0));Pet->GetCharacterMovement()->Velocity=Velocity;
    Pet->TurnRate=Shot==8?TurnSpeed:0;
    if(BeforeGait)
    {
        FName Name=Velocity.Size2D()<20?TEXT("Idle"):Velocity.X<-20?TEXT("BackWalk"):Velocity.Size2D()<150?TEXT("Walk"):TEXT("Run");
        Anim->BodyPose=Pet->Poses.FindRef(FName(*(TEXT("Before")+Name.ToString())));
        Anim->PoseRate=Name==TEXT("Walk")||Name==TEXT("BackWalk")?Velocity.Size2D()/96.f:Name==TEXT("Run")?Velocity.Size2D()/300.f:1;
        Anim->ExplicitTime=-1;Anim->bLoopPose=true;Anim->bLocomotion=false;Anim->SidePose=nullptr;Anim->bAim=false;
    }
    else Pet->SetPose(Velocity.Size2D()>20?TEXT("Move"):TEXT("Idle"));
    Pet->GetMesh()->TickAnimation(Dt,false);Pet->GetMesh()->RefreshBoneTransforms();Pet->PostPose(Dt);
    const FVector Foot=Pet->GetMesh()->GetSocketLocation(TEXT("M-bone-L-Foot"))-Pet->GetActorLocation();
    if(!GaitLastFoot.IsZero())GaitWorstStep=FMath::Max(GaitWorstStep,FVector::Dist(Foot,GaitLastFoot));
    GaitLastFoot=Foot;
    for(const auto& Pair:{TPair<FName,FName>(TEXT("M-bone-R-Hand"),TEXT("rifle_grip")),TPair<FName,FName>(TEXT("M-bone-L-Hand"),TEXT("rifle_left"))})
        GaitWorstGrip=FMath::Max(GaitWorstGrip,FVector::Dist(Pet->GetMesh()->GetSocketLocation(Pair.Key),Pet->GetMesh()->GetSocketLocation(Pair.Value)));
    if(!BeforeGait&&Anim->bLocomotion&&Segment>.3)
    {
        const float Delta=FMath::Fmod(Anim->LocomotionPhase-GaitLastPhase+1.f,1.f);
        if(Delta>.15)++Failures;
    }
    GaitLastPhase=Anim->LocomotionPhase;
    const float Stance=Shot==6?.36f:Shot==7?.25f:Shot<=2?.56f:.60f;
    const bool Support=!BeforeGait&&Shot<9&&Segment>.4f&&Anim->LocomotionPhase>Stance*.2f&&Anim->LocomotionPhase<Stance*.7f;
    if(Support)
    {
        const FVector WorldFoot=Pet->GetMesh()->GetSocketLocation(TEXT("M-bone-L-Foot"));
        if(!bGaitSupporting)GaitSupportOrigin=WorldFoot;
        const float Drift=FVector::Dist(WorldFoot,GaitSupportOrigin);
        if(Drift>GaitSupportDrift)UE_LOG(LogTemp,Display,TEXT("GAIT_DRIFT shot=%d phase=%.3f drift=%.3f offset=%s"),Shot,Anim->LocomotionPhase,Drift,*(WorldFoot-GaitSupportOrigin).ToString());
        GaitSupportDrift=FMath::Max(GaitSupportDrift,Drift);
    }
    bGaitSupporting=Support;
    const FVector Focus=Pet->GetActorLocation()+FVector(0,0,-4);
    const FVector Offset=Shot==0?FVector(620,0,45):Shot==2?FVector(-620,0,45):FVector(70,-620,45);
    Camera->SetActorLocation(Focus+Offset);Camera->SetActorRotation((Focus-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->SetFieldOfView(38);Camera->GetCameraComponent()->bConstrainAspectRatio=false;
    if(FParse::Param(FCommandLine::Get(),TEXT("GaitRecord")))
    {
        IFileManager::Get().MakeDirectory(*(Dir/TEXT("frames")),true);
        FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("frames/frame_%05d.png"),GaitSamples),false,false);
    }
    else if(FMath::Abs(Segment-1.5f)<Dt*.5f)
    {IFileManager::Get().MakeDirectory(*Dir,true);FScreenshotRequest::RequestScreenshot(Dir/FString::Printf(TEXT("shot_%02d.png"),Shot),false,false);}
    ++GaitSamples;
    if(GaitSamples>=FPS*30)
    {
        Check(TEXT("gait_grip_within_2_5cm"),GaitWorstGrip<2.5f);
        Check(TEXT("gait_no_bone_position_pop"),GaitWorstStep<35.f);
        if(!BeforeGait)Check(TEXT("gait_world_support_drift_under_3cm"),GaitSupportDrift<3.f);
        IFileManager::Get().MakeDirectory(*Dir,true);
        FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"passed\":%s,\"fps\":%d,\"samples\":%d,\"max_grip_cm\":%.4f,\"max_foot_frame_step_cm\":%.4f,\"support_drift_cm\":%.4f,\"failures\":%d}"),Failures?TEXT("false"):TEXT("true"),FPS,GaitSamples,GaitWorstGrip,GaitWorstStep,GaitSupportDrift,Failures),*(Dir/TEXT("report.json")));
        Stage=99;FPlatformMisc::RequestExit(false);
    }
}
