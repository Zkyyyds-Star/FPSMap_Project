#include "QA/HeliosSSXDemo.h"
#include "Player/HeliosArenaCharacter.h"
#include "UI/HeliosAdventureHUD.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Pets/HeliosPetDog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "InputKeyEventArgs.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Animation/AnimSequence.h"

void AHeliosSSXDemo::TickInputControlQA(APlayerController* PC,AHeliosAdventureHUD* HUD)
{
    if(!PC||!HUD)return;
    const bool SSX=FParse::Param(FCommandLine::Get(),TEXT("HeliosSSXInputControl"));
    FString Profile;FParse::Value(FCommandLine::Get(),TEXT("HeliosControlAction="),Profile);
    const bool AForm=Profile.IsEmpty()?FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKAControl")):Profile.StartsWith(TEXT("A"));
    const int32 DesiredForm=Profile==TEXT("CDart")?2:AForm?0:1;
    const bool LeftAttack=Profile.IsEmpty()?(AForm||FParse::Param(FCommandLine::Get(),TEXT("HeliosZXKLeftControl"))):(Profile.EndsWith(TEXT("Light"))||Profile==TEXT("CDart"));
    const FKey AttackKey=Profile.EndsWith(TEXT("Skill"))?EKeys::G:LeftAttack?EKeys::LeftMouseButton:EKeys::RightMouseButton;
    const bool Extended=FParse::Param(FCommandLine::Get(),TEXT("HeliosControlExtended"));
    // Feed the game's own input queue; never issue operating-system input or
    // replace attack/animation clocks with a manual pose evaluation.
    auto Key=[&](FKey K,bool Down){PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,Down?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));};
    auto Release=[&](){for(FKey K:{EKeys::W,EKeys::A,EKeys::S,EKeys::D,EKeys::RightMouseButton,EKeys::LeftMouseButton,EKeys::G,EKeys::R,EKeys::Q,EKeys::V})Key(K,false);};
    auto* Z=Cast<AHeliosZhongXiaoKuiWeapon>(Player->GetEquippedWeapon());
    auto* S=Cast<AHeliosSunShangXiangWeapon>(Player->GetEquippedWeapon());
    if(Stage==0&&Age>2){OriginalHP=Player->GetMaxHealth();OriginalSpeed=Player->GetCharacterMovement()->MaxWalkSpeed;HUD->SelectWeapon(SSX?TEXT("cfm_sunshangxiang"):TEXT("cfm_zhongxiaokui"));HUD->SelectPet(SSX?EHeliosPetKind::SunShangXiang:EHeliosPetKind::ZhongXiaoKui);Stage=500;Age=0;return;}
    if(Stage==500&&Age>2){Capture(TEXT("input_control_menu"));HUD->StartSelectedRun();Stage=501;Age=0;return;}
    if(Stage==501&&Age>3)
    {
        Check(TEXT("input_control_selected_weapon"),SSX?S!=nullptr:Z!=nullptr);if(SSX?!S:!Z){Finish();return;}
        Check(TEXT("input_control_original_health_speed"),OriginalHP==Player->GetMaxHealth()&&OriginalSpeed==Player->GetCharacterMovement()->MaxWalkSpeed);
        if(auto* D=AHeliosAdventureDirector::Find(GetWorld()))D->SetActorTickEnabled(false);
        for(TActorIterator<AHeliosEnemyActor> E(GetWorld());E;++E)E->Destroy();
        for(TActorIterator<AHeliosPetDog> P(GetWorld());P;++P){P->SetActorHiddenInGame(true);P->SetActorTickEnabled(false);P->GetCharacterMovement()->DisableMovement();}
        float ViewPitch=0.f;FParse::Value(FCommandLine::Get(),TEXT("HeliosControlPitch="),ViewPitch);
        Player->SetCanBeDamaged(false);PC->SetControlRotation(FRotator(ViewPitch,0,0));Player->SetActorLocation(FParse::Param(FCommandLine::Get(),TEXT("HeliosNearCoverControl"))?FVector(-1600,0,100):FVector(-1500,-1300,100));
        if(Z&&DesiredForm>0)Key(DesiredForm==2?EKeys::V:EKeys::Q,true);Stage=502;Age=0;return;
    }
    if(Stage==502&&Age>2)
    {
        if(Z&&(Z->Form!=DesiredForm||Z->Age>=0)&&Age<8)return;
        Release();Check(TEXT("input_control_ready"),SSX?S!=nullptr:Z&&Z->Form==DesiredForm&&Z->Age<0);
        FollowStart=Player->GetActorLocation();Stage=510;Age=0;Before=0;FeedbackFrame=0;FeedbackLastSample=-1;FollowDistance=0;return;
    }
    if(Stage<510)return;
    // Wait out the real skill cooldown between movement cases. Do not reset it
    // or count a rejected key press as a tested skill animation.
    if(Profile.EndsWith(TEXT("Skill"))&&Z&&Before==0&&GetWorld()->GetTimeSeconds()<Z->SkillCooldownEnd){Age=0;return;}
    if(Profile==TEXT("CDart")&&Z&&Before==0&&Z->DartAmmo<20){Key(EKeys::R,true);Key(EKeys::R,false);Age=0;return;}
    const int32 Mode=Stage-510;
    if(Before==0)
    {
        InputSeenActions.Reset();
        InputAttackSerialBefore=Z?Z->DamageAttackSerial:0;InputWaveShotsBefore=Z?Z->WaveShots:0;
        if(Mode==1||Mode==2||Mode>=5)Key(EKeys::W,true);
        if(Mode==2||Mode==3)Key(EKeys::D,true);
        if(Mode==4)Key(EKeys::S,true);
        Before=1;
    }
    if(Age>=.8f&&Before==1){Key(AttackKey,true);Before=2;}
    if(Mode==5)
    {
        if(Age>=.9f&&Before==2){Key(AttackKey,false);Before=3;}
        if(Age>=1.4f&&Before==3){Key(AttackKey,true);Before=4;}
        if(Age>=1.5f&&Before==4){Key(AttackKey,false);Before=5;}
        if(Age>=2.f&&Before==5){Key(AttackKey,true);Before=6;}
        if(Age>=2.1f&&Before==6){Key(AttackKey,false);Before=7;}
    }
    else if(Mode==6)
    {
        if(Age>=1.25f&&Before==2){Key(EKeys::W,false);Key(AttackKey,false);Before=3;}
        if(Age>=1.9f&&Before==3){Key(EKeys::W,true);Key(AttackKey,true);Before=4;}
        if(Age>=2.5f&&Before==4){Key(AttackKey,false);Before=5;}
    }
    else if(Mode==7)
    {
        if(Age>=1.2f&&Before==2){Key(AttackKey,false);Before=3;}
        if(Before==3&&Z&&Z->Age<0){Key(EKeys::V,true);InputSwitchAge=Age;Before=4;}
        if(Before==4&&Age>InputSwitchAge+.05f){Key(EKeys::V,false);Before=5;}
        if(Before==5&&Z&&Z->Form==2&&Z->Age<0){Key(EKeys::LeftMouseButton,true);InputSwitchAge=Age;Before=6;}
        if(Before==6&&Age>InputSwitchAge+.2f){Key(EKeys::LeftMouseButton,false);Before=7;}
        if(Before==7&&Z&&Z->Age<0){Key(EKeys::V,true);InputSwitchAge=Age;Before=8;}
        if(Before==8&&Age>InputSwitchAge+.05f){Key(EKeys::V,false);Before=9;}
        if(Before==9&&Z&&Z->Form==1&&Z->Age<0){Key(EKeys::RightMouseButton,true);InputSwitchAge=Age;Before=10;}
        if(Before==10&&Age>InputSwitchAge+.6f){Key(EKeys::RightMouseButton,false);Before=11;}
    }
    else if(Age>=(Profile.EndsWith(TEXT("Light"))?4.3f:2.3f)&&Before==2){Key(AttackKey,false);Before=3;}
    auto* Mesh=Z?Z->GetFirstPersonMesh():S?S->GetFirstPersonMesh():nullptr;if(!Mesh){Check(TEXT("input_control_mesh"),false);Release();Finish();return;}
    const auto* Anim=Cast<UHeliosHandAnimInstance>(Mesh->GetAnimInstance());
    if(Z&&Z->Age>=0&&Z->ActionKind==0)InputSeenActions.Add(Z->Action);
    const FTransform Camera=Player->FirstPersonCamera->GetComponentTransform();
    for(FName Bone:{FName(TEXT("b_RightHand")),FName(TEXT("b_LeftHand")),FName(TEXT("Jian"))})
        if(Mesh->DoesSocketExist(Bone))UE_LOG(LogTemp,Display,TEXT("FP_SPATIAL stage=%d age=%.6f sample=%.6f bone=%s world=%s camera_relative=%s local=%s absolute_location=%d parent=%s"),Stage,Age,Anim?Anim->SampleSeconds:-1,*Bone.ToString(),*Mesh->GetSocketLocation(Bone).ToString(),*Camera.InverseTransformPosition(Mesh->GetSocketLocation(Bone)).ToString(),*Mesh->GetSocketTransform(Bone,RTS_Component).GetLocation().ToString(),int32(Mesh->IsUsingAbsoluteLocation()),*GetNameSafe(Mesh->GetAttachParent()));
    for(TActorIterator<AHeliosSSXEffect> E(GetWorld());E;++E)if(E->GetOwner()==Player->GetEquippedWeapon())
    {
        TInlineComponentArray<UStaticMeshComponent*> Parts;E->GetComponents(Parts);
        for(int32 I=0;I<Parts.Num();++I)if(Parts[I]->IsVisible())UE_LOG(LogTemp,Display,TEXT("FX_SPATIAL stage=%d age=%.6f effect=%u effect_age=%.6f part=%d root_world=%s world=%s camera_relative=%s parent_relative=%s absolute_location=%d"),Stage,Age,E->GetUniqueID(),E->GetGameTimeSinceCreation(),I,*E->GetActorLocation().ToString(),*Parts[I]->GetComponentLocation().ToString(),*Camera.InverseTransformPosition(Parts[I]->GetComponentLocation()).ToString(),*Parts[I]->GetRelativeLocation().ToString(),int32(Parts[I]->IsUsingAbsoluteLocation()));
    }
    FollowDistance=FMath::Max(FollowDistance,float(Player->GetVelocity().Size2D()));
    UE_LOG(LogTemp,Display,TEXT("INPUT_CONTROL character=%s mode=%d age=%.6f speed=%.3f clock=%.6f sequence=%s wave=%d late=%d depth=%d blend=%d near=%.3f mesh=%s camera=%s"),SSX?TEXT("SSX"):TEXT("ZXK"),Mode,Age,Player->GetVelocity().Size2D(),Anim?Anim->SampleSeconds:-1,*GetNameSafe(Anim?Anim->Sequence:nullptr),Z?Z->WaveShots:0,Z?int32(Z->bWaveLateView):0,Z?int32(Z->WaveSelfDepth->bRenderCustomDepth):0,int32(Mesh->GetMaterial(0)->GetBlendMode()),PC->PlayerCameraManager->GetCameraCacheView().PerspectiveNearClipPlane,*Mesh->GetComponentLocation().ToString(),*Player->FirstPersonCamera->GetComponentLocation().ToString());
    UE_LOG(LogTemp,Display,TEXT("MOTION_FRAME stage=%d age=%.6f dt=%.6f wall=%.9f action=%s"),Stage,Age,GetWorld()->GetDeltaSeconds(),FPlatformTime::Seconds(),*GetNameSafe(Anim?Anim->Sequence:nullptr));
    if(Age-FeedbackLastSample+KINDA_SMALL_NUMBER>=1.f/60.f){FeedbackLastSample=Age;Capture(*FString::Printf(TEXT("feedback_fp_%d_%03d"),Stage,FeedbackFrame++));}
    if(Age>(Mode==7?9.f:Mode<=4&&Profile.EndsWith(TEXT("Light"))?5.2f:3.2f))
    {
        Check(*FString::Printf(TEXT("input_control_movement_%d"),Mode),Mode==0?FollowDistance<1.f:FollowDistance>100.f);
        if(Z)Check(*FString::Printf(TEXT("input_control_action_started_%d"),Mode),Z->DamageAttackSerial>InputAttackSerialBefore);
        if(Z&&Mode<=4&&Profile.EndsWith(TEXT("Light")))Check(*FString::Printf(TEXT("input_control_all_combo_actions_%d"),Mode),InputSeenActions.Num()>=(DesiredForm==1?5:3));
        if(Z&&Profile==TEXT("CDart"))Check(*FString::Printf(TEXT("input_control_real_dart_%d"),Mode),Z->DartAmmo<20);
        if(Z&&(Profile==TEXT("BWave")||(Profile.IsEmpty()&&!LeftAttack)))Check(*FString::Printf(TEXT("input_control_real_wave_%d"),Mode),Z->WaveShots>InputWaveShotsBefore);
        if(Z&&Mode==7){Check(TEXT("input_control_returns_to_B_after_darts"),Z->Form==1&&Before==11);Check(TEXT("input_control_switch_real_dart"),Z->DartAmmo<20);Check(TEXT("input_control_switch_second_wave"),Z->WaveShots>InputWaveShotsBefore+1);}
        Release();if(Z)Z->Cancel();if(S)S->Cancel();Player->GetCharacterMovement()->StopMovementImmediately();Player->SetActorLocation(FollowStart);
        if(Mode==(Extended?6:(SSX||LeftAttack||!Profile.IsEmpty())?4:7)){Finish();return;}
        ++Stage;Age=0;Before=0;FeedbackFrame=0;FeedbackLastSample=-1;FollowDistance=0;
    }
}
