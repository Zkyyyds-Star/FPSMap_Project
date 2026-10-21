#if WITH_DEV_AUTOMATION_TESTS
#include "Weapons/HeliosSunShangXiang.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Pets/HeliosSunShangXiangPet.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Combat/CombatDamageSettings.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/Material.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosSSXTest,"Helios.SunShangXiang.CombatLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosSSXTest::RunTest(const FString&)
{
    FTestWorldWrapper Wrapper;if(!Wrapper.CreateTestWorld(EWorldType::Game))return false;
    UWorld* World=Wrapper.GetTestWorld();World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!Wrapper.BeginPlayInTestWorld())return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=World->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
    auto* PC=World->SpawnActor<APlayerController>();PC->Possess(P);PC->SetControlRotation(FRotator::ZeroRotator);
    const float PlayerHP=P->GetMaxHealth(),Speed=P->GetCharacterMovement()->MaxWalkSpeed;
    auto* D=UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_sunshangxiang"));if(!TestNotNull(TEXT("Registered weapon"),D)||!P->EquipWeaponDefinition(D))return false;
    auto* W=Cast<AHeliosSunShangXiangWeapon>(P->GetEquippedWeapon());if(!TestNotNull(TEXT("Original weapon class"),W))return false;
    TestFalse(TEXT("No ammo consumption"),W->UsesAmmunition());TestEqual(TEXT("Player health preserved"),P->GetMaxHealth(),PlayerHP);TestEqual(TEXT("Player speed preserved"),P->GetCharacterMovement()->MaxWalkSpeed,Speed);
    TestNotNull(TEXT("Original first person mesh"),W->GetFirstPersonMesh()->GetSkeletalMeshAsset());
    TestTrue(TEXT("All eleven original FP clips loaded"),W->ResidentAnimations.Num()==11&&!W->ResidentAnimations.Contains(nullptr));
    TestTrue(TEXT("All three decoded original sounds loaded"),W->ResidentSounds.Num()==3&&!W->ResidentSounds.Contains(nullptr));
    TestNotNull(TEXT("Original effect library"),LoadObject<UHeliosSSXEffectLibrary>(nullptr,TEXT("/Game/Helios/CFMSunShangXiang/Effects/DA_SSX_Effects")));
    W->Cancel();W->Timeline.Begin(HeliosSSX::EAttack::Light);W->Tick(.01f);
    TestEqual(TEXT("Light attack instantiates the original trail"),W->Effects.Num(),1);
    if(!W->Effects.IsEmpty())
    {
        TArray<UStaticMeshComponent*> Components;W->Effects[0]->GetComponents(Components);
        TestEqual(TEXT("Both source trail renderers are present"),Components.Num(),2);
        for(auto* Component:Components){TestTrue(TEXT("Effect mesh loaded"),Component->GetStaticMesh()!=nullptr);TestNotNull(TEXT("Effect material loaded"),Component->GetMaterial(0));}
    }
    W->Cancel();W->Timeline.Begin(HeliosSSX::EAttack::Skill);W->Tick(.01f);
    TestEqual(TEXT("First skill phase instantiates source effect"),W->Effects.Num(),1);
    if(!W->Effects.IsEmpty())
    {
        TArray<UStaticMeshComponent*> Components;W->Effects[0]->GetComponents(Components);
        TestEqual(TEXT("Original skill mesh and particle tracks are present"),Components.Num(),22);
        for(auto* Component:Components)
        {
            if(!Component->GetStaticMesh()->GetName().Contains(TEXT("SourceBillboard"))||!Component->IsVisible())continue;
            const FVector Born=Component->GetComponentLocation();W->Effects[0]->AddActorWorldOffset(FVector(100,0,0));W->Effects[0]->Tick(.01f);
            TestTrue(TEXT("World particles do not follow a moving emitter"),FVector::Dist(Born,Component->GetComponentLocation())<25.f);break;
        }
    }
    W->Cancel();
    auto* Pet=World->SpawnActor<AHeliosSunShangXiangPet>(FVector(0,500,100),FRotator::ZeroRotator,Spawn);Pet->CompanionOwner=P;
    TestTrue(TEXT("Independent companion alongside weapon"),Pet->bModelReady&&P->GetWeaponId()==TEXT("cfm_sunshangxiang"));TestEqual(TEXT("Companion health"),Pet->Settings.MaxHealth,600.f);
    TestEqual(TEXT("Old enum SwordDog preserved"),int32(EHeliosPetKind::SwordDog),0);TestEqual(TEXT("Old enum Ultimatum preserved"),int32(EHeliosPetKind::UltimatumGR),1);
    for(FName Voice:{FName(TEXT("Kill")),FName(TEXT("Hurt")),FName(TEXT("Death"))})TestNotNull(TEXT("Original SP voice asset"),LoadObject<USoundBase>(nullptr,*(TEXT("/Game/Helios/CFMSunShangXiang/Audio/VO_SSX_")+Voice.ToString())));
    TestEqual(TEXT("Player has all eight original kill voices"),W->Voice->GetKillPoolSize(),8);
    TestEqual(TEXT("Companion has all eight original kill voices"),Pet->Voice->GetKillPoolSize(),8);
    const TArray<FVector> Directions={FVector(520,0,0),FVector(0,520,0),FVector(-520,0,0),FVector(0,-520,0)};
    const TArray<FName> Moves={TEXT("Run_Fwd"),TEXT("Run_RT"),TEXT("Run_Bwd"),TEXT("Run_LT")};
    for(int32 I=0;I<4;++I)
    {
        Pet->GetCharacterMovement()->Velocity=Directions[I];Pet->UpdateLocomotion(.1f);Pet->SetPose(TEXT("Move"));
        auto* Anim=Cast<UHeliosHandAnimInstance>(Pet->GetMesh()->GetAnimInstance());
        TestTrue(TEXT("Correct original directional clip"),Anim&&Anim->Sequence==HeliosSSX::Animation(false,Moves[I]));
        TestTrue(TEXT("Direction change preserves gait phase"),Pet->MoveAge>=float(I+1)*.1f-.001f);
    }
    Pet->MoveAge=0;Pet->GetCharacterMovement()->Velocity=FVector(260,0,0);Pet->UpdateLocomotion(.1f);TestEqual(TEXT("Half movement speed uses half gait rate"),Pet->MoveAge,.05f);
    Pet->GetCharacterMovement()->StopMovementImmediately();Pet->UpdateLocomotion(.1f);Pet->SetPose(TEXT("Idle"));TestEqual(TEXT("Stationary gait clock resets"),Pet->MoveAge,0.f);
    W->Cancel();W->IdleAge=0;W->Pose();
    TestTrue(TEXT("Hands immediately evaluate after equipment reset"),W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation().Z>-25.f);
    const FVector IdleWrist=W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_LeftHand"),RTS_Component).GetLocation();
    W->Timeline.Begin(HeliosSSX::EAttack::Light);W->Timeline.Age=.13f;W->Pose();
    W->GetFirstPersonMesh()->TickAnimation(UHeliosHandAnimInstance::BlendSeconds+.01f,false);W->GetFirstPersonMesh()->RefreshBoneTransforms();
    TestTrue(TEXT("First-person light actually moves the displayed wrist from idle"),FVector::Dist(IdleWrist,W->GetFirstPersonMesh()->GetSocketTransform(TEXT("b_LeftHand"),RTS_Component).GetLocation())>1.f);W->Cancel();
    FVector Eye;FRotator R;PC->GetPlayerViewPoint(Eye,R);
    auto* Enemy=World->SpawnActor<AHeliosEnemyE01>(Eye+FVector(200,0,0),FRotator::ZeroRotator,Spawn);Enemy->StopShooting();Enemy->SetActorTickEnabled(false);Enemy->GetCharacterMovement()->DisableMovement();if(Enemy->GetController())Enemy->GetController()->Destroy();
    for(float FPS:{30.f,60.f,120.f})
    {
        for(auto Type:{HeliosSSX::EAttack::Light,HeliosSSX::EAttack::Heavy,HeliosSSX::EAttack::Skill})
        {
            W->Cancel();Enemy->CurrentHP=10000;W->Timeline.Begin(Type);const int Before=W->HitCount,BeforeFeedback=W->HitAudioGate.Contacts;
            for(int I=0;I<FMath::CeilToInt(3*FPS);++I)W->Tick(1/FPS);
            const int Phases=Type==HeliosSSX::EAttack::Skill?4:2;
            TestEqual(FString::Printf(TEXT("Exactly %d hits at %.0f FPS"),Phases,FPS),W->HitCount-Before,Phases);
            TestEqual(TEXT("Impact cue is submitted once per successful contact phase"),W->HitAudioGate.Contacts-BeforeFeedback,Phases);
            TestEqual(TEXT("Whole attack damage is not doubled by two swords"),10000-Enemy->CurrentHP,HeliosSSX::Damage(W,Type,false)*Phases);
        }
    }
    const float OriginalPlaybackSpeed=W->PlaybackSpeed;
    for(float FPS:{30.f,60.f,120.f})
    {
        for(float SpeedMultiplier:{.5f,1.f,2.f})
        {
            W->Cancel();W->PlaybackSpeed=SpeedMultiplier;Enemy->CurrentHP=10000;
            const int Before=W->HitCount,BeforeFeedback=W->HitAudioGate.Contacts;P->StartFireFromAutomation();
            int ElapsedFrames=0;
            for(int Round=0;Round<3;++Round)
            {
                const int TotalFrames=FMath::CeilToInt((Round+1)*.4f*FPS/SpeedMultiplier);
                for(;ElapsedFrames<TotalFrames;++ElapsedFrames)W->Tick(1/FPS);
                TestTrue(TEXT("Held primary starts the next light round"),W->Timeline.Attack==HeliosSSX::EAttack::Light);
                TestTrue(TEXT("Fast light retains frame overshoot without cadence drift"),FMath::IsNearlyEqual(W->Timeline.Age,ElapsedFrames/FPS*SpeedMultiplier-(Round+1)*.4f,.0002f));
                TestEqual(TEXT("Each completed light round hits twice"),W->HitCount-Before,(Round+1)*2);
                TestEqual(TEXT("Held light resets impact de-duplication each round"),W->HitAudioGate.Contacts-BeforeFeedback,(Round+1)*2);
                const int Trails=W->Effects.FilterByPredicate([W](const auto& Effect){return IsValid(Effect)&&Effect->GetAttachParentActor()==W;}).Num();
                TestEqual(TEXT("Each round presents the source two-blade effect and the next opening effect"),Trails,(Round+1)+1);
            }
            TestEqual(TEXT("Held light damage matches three complete rounds"),10000-Enemy->CurrentHP,HeliosSSX::Damage(W,HeliosSSX::EAttack::Light,false)*6);
            P->StopFireFromAutomation();
            TestTrue(TEXT("Release preserves the current light round"),W->Timeline.Attack==HeliosSSX::EAttack::Light);
            W->Tick((W->Timeline.Duration(true)+.01f)/SpeedMultiplier);
            TestEqual(TEXT("Released round still delivers both hits"),W->HitCount-Before,8);
            TestTrue(TEXT("Release prevents another round"),W->Timeline.Attack==HeliosSSX::EAttack::None);
            W->Tick(3/SpeedMultiplier);TestEqual(TEXT("Released primary stays stopped"),W->HitCount-Before,8);
            P->StartFireFromAutomation();P->StopFireFromAutomation();W->Tick(2/SpeedMultiplier);
            TestEqual(TEXT("A subsequent tap completes exactly one round"),W->HitCount-Before,10);
        }
    }
    W->PlaybackSpeed=OriginalPlaybackSpeed;
    // Exercise the displayed pose with positive animation ticks, not just the
    // action state. This catches the previous outgoing-idle freeze and hard cuts.
    for(float FPS:{30.f,60.f,120.f})
    {
        W->Cancel();W->PlaybackSpeed=1.f;Enemy->CurrentHP=10000;
        auto* Mesh=W->GetFirstPersonMesh();auto* Hand=Cast<UHeliosHandAnimInstance>(Mesh->GetAnimInstance());
        if(!TestNotNull(TEXT("Fast light uses the repaired displayed-pose clock"),Hand))return false;
        Mesh->TickAnimation(.1f,false);Mesh->RefreshBoneTransforms();
        const FVector StartWrist=Mesh->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation();
        FVector Before=StartWrist;UAnimSequence* LastClip=Hand->Sequence;float ClipAge=0;
        float MaxBoundaryJump=0,MaxSettledRawError=0,MaxMotion=0,MaxRightLengthError=0;int Boundaries=0,RawSamples=0;
        P->StartFireFromAutomation();
        // StartFire already enters the first clip at zero time. The next tick
        // advances that same clip; compare transitions from this displayed state.
        LastClip=Hand->Sequence;Before=Mesh->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation();
        for(int Frame=0;Frame<FMath::RoundToInt(2.f*FPS);++Frame)
        {
            W->Tick(1.f/FPS);
            if(Hand->Sequence!=LastClip)
            {
                const FVector Enter=Mesh->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation();
                MaxBoundaryJump=FMath::Max(MaxBoundaryJump,float(FVector::Dist(Before,Enter)));
                LastClip=Hand->Sequence;ClipAge=0;++Boundaries;
            }
            Mesh->TickAnimation(1.f/FPS,false);Mesh->RefreshBoneTransforms();ClipAge+=1.f/FPS;
            Before=Mesh->GetSocketTransform(TEXT("b_RightHand"),RTS_Component).GetLocation();
            MaxMotion=FMath::Max(MaxMotion,float(FVector::Dist(StartWrist,Before)));
            if(ClipAge>UHeliosHandAnimInstance::BlendSeconds+.001f)
            {
                const auto& Ref=Hand->Sequence->GetSkeleton()->GetReferenceSkeleton();TArray<FTransform> Raw;Raw.SetNum(Ref.GetNum());
                for(int I=0;I<Raw.Num();++I)
                {
                    Hand->Sequence->GetBoneTransform(Raw[I],FSkeletonPoseBoneIndex(I),FAnimExtractContext(Hand->SampleSeconds,false),true);
                    const int Parent=Ref.GetParentIndex(I);if(Parent!=INDEX_NONE)Raw[I]=Raw[I]*Raw[Parent];
                }
                // Native FP preserves both arms, wrists and blade bindings.
                for(FName Bone:{FName(TEXT("b_LeftArm")),FName(TEXT("b_LeftForeArm")),FName(TEXT("b_LeftHand")),FName(TEXT("b_RightArm")),FName(TEXT("b_RightForeArm")),FName(TEXT("b_RightHand")),FName(TEXT("Bone01")),FName(TEXT("Bone02"))})
                {
                    const int I=Ref.FindBoneIndex(Bone);if(I==INDEX_NONE)continue;
                    MaxSettledRawError=FMath::Max(MaxSettledRawError,float(FVector::Dist(Raw[I].GetLocation(),Mesh->GetSocketTransform(Bone,RTS_Component).GetLocation())));
                }
                const FName Chain[]={TEXT("b_RightArm"),TEXT("b_RightForeArm"),TEXT("b_RightHand")};
                for(int32 J=0;J<2;++J)
                {const int A=Ref.FindBoneIndex(Chain[J]),B=Ref.FindBoneIndex(Chain[J+1]);if(A>=0&&B>=0)
                    MaxRightLengthError=FMath::Max(MaxRightLengthError,FMath::Abs(float(FVector::Dist(Raw[A].GetLocation(),Raw[B].GetLocation())-FVector::Dist(Mesh->GetSocketTransform(Chain[J],RTS_Component).GetLocation(),Mesh->GetSocketTransform(Chain[J+1],RTS_Component).GetLocation()))));}
                ++RawSamples;
            }
        }
        P->StopFireFromAutomation();
        UE_LOG(LogTemp,Display,TEXT("SSX_FAST_DISPLAY fps=%.0f boundaries=%d boundary_jump_cm=%.6f settled_raw_error_cm=%.6f movement_cm=%.3f raw_samples=%d"),FPS,Boundaries,MaxBoundaryJump,MaxSettledRawError,MaxMotion,RawSamples);
        // Five .4s pairs each cross a stroke/recovery and next-pair boundary.
        TestEqual(TEXT("Later-reference light crosses ten clip boundaries in two seconds"),Boundaries,10);
        TestTrue(TEXT("Native light visibly moves at every tested frame rate"),MaxMotion>10.f);
        TestTrue(TEXT("Clip changes retain the displayed wrist instead of jumping"),MaxBoundaryJump<.01f);
        TestTrue(TEXT("After transition both arms and both blades retain original raw trajectory"),RawSamples>10&&MaxSettledRawError<.3f);
        TestTrue(TEXT("Native right sweep preserves original upper/forearm lengths"),MaxRightLengthError<.01f);
    }
    W->PlaybackSpeed=OriginalPlaybackSpeed;
    W->Cancel();Enemy->CurrentHP=10000;
    const int TapBefore=W->HitCount;
    P->StartFireFromAutomation();P->StopFireFromAutomation();W->Tick(.1f);
    P->StartFireFromAutomation();P->StopFireFromAutomation();W->Tick(1.1f);
    TestEqual(TEXT("A rapid second tap queues one complete light pair"),W->HitCount-TapBefore,4);
    TestTrue(TEXT("Buffered tap finishes and returns to idle"),W->Timeline.Attack==HeliosSSX::EAttack::None&&!W->bQueuedPrimary);
    W->Cancel();Enemy->CurrentHP=10000;
    const int HitchBefore=W->HitCount;
    P->StartFireFromAutomation();W->Tick(1.2f);P->StopFireFromAutomation();
    TestEqual(TEXT("A held hitch crosses three fast pairs without losing contacts"),W->HitCount-HitchBefore,6);
    TestTrue(TEXT("A held hitch begins the next pair at its exact boundary"),W->Timeline.Attack==HeliosSSX::EAttack::Light&&FMath::IsNearlyZero(W->Timeline.Age,.0002f));
    W->Cancel();
    for(auto Type:{HeliosSSX::EAttack::Equip,HeliosSSX::EAttack::Heavy,HeliosSSX::EAttack::Skill})
    {
        W->Cancel();W->Timeline.Begin(Type);const float Duration=W->Timeline.Duration(true);
        P->StartFireFromAutomation();
        TestTrue(TEXT("Held primary does not interrupt an existing action"),W->Timeline.Attack==Type);
        W->Tick(.01f/W->PlaybackSpeed);
        TestTrue(TEXT("Existing action continues before completion"),W->Timeline.Attack==Type);
        W->Tick(Duration/W->PlaybackSpeed);
        TestTrue(TEXT("Held primary follows the completed action with light"),W->Timeline.Attack==HeliosSSX::EAttack::Light);
        W->Cancel();W->Timeline.Begin(Type);P->StartFireFromAutomation();P->StopFireFromAutomation();
        W->Tick((Duration+.01f)/W->PlaybackSpeed);
        TestTrue(TEXT("Releasing during an action prevents queued light"),W->Timeline.Attack==HeliosSSX::EAttack::None);
    }
    W->Cancel();P->StartFireFromAutomation();P->CancelAutomationCombat();W->Tick(3);
    TestTrue(TEXT("Cancelling combat clears held primary and pending light"),!W->bPrimaryDown&&W->Timeline.Attack==HeliosSSX::EAttack::None);
    W->Cancel();Enemy->CurrentHP=10000;W->Timeline.Begin(HeliosSSX::EAttack::Skill);W->Tick(2.5f);
    TestEqual(TEXT("Frame hitch still processes all crossed phases exactly once"),10000-Enemy->CurrentHP,HeliosSSX::Damage(W,HeliosSSX::EAttack::Skill,false)*4);
    Enemy->SetActorLocation(Eye+FVector(2000,0,0));W->Cancel();
    const int32 MissFeedback=W->HitAudioGate.Contacts;W->Timeline.Begin(HeliosSSX::EAttack::Light);W->Tick(2.5f);
    TestEqual(TEXT("Empty swing submits no impact cue"),W->HitAudioGate.Contacts,MissFeedback);
    // Keep both targets in the sweep without one target occluding the other.
    Enemy->SetActorLocation(Eye+FVector(200,-50,0));W->Cancel();
    auto* Other=World->SpawnActor<AHeliosEnemyE01>(Eye+FVector(200,50,0),FRotator::ZeroRotator,Spawn);Other->CurrentHP=10000;Other->StopShooting();Other->SetActorTickEnabled(false);Other->GetCharacterMovement()->DisableMovement();
    if(Other->GetController())Other->GetController()->Destroy();
    const int32 MultiFeedback=W->HitAudioGate.Contacts;Enemy->CurrentHP=10000;W->Timeline.Begin(HeliosSSX::EAttack::Light);W->Tick(2.5f);
    TestTrue(TEXT("Light sweep damages both nearby targets"),Enemy->CurrentHP<10000&&Other->CurrentHP<10000);
    TestEqual(TEXT("Multiple targets still submit two impact cues per light attack"),W->HitAudioGate.Contacts-MultiFeedback,2);Other->Destroy();Enemy->SetActorLocation(Eye+FVector(200,0,0));
    auto* Wall=World->SpawnActor<AStaticMeshActor>(Eye+FVector(90,0,0),FRotator::ZeroRotator,Spawn);auto* Mesh=Wall->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetWorldScale3D(FVector(.05f,4,4));Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    W->Cancel();Enemy->CurrentHP=10000;const int32 WallFeedback=W->HitAudioGate.Contacts;W->Timeline.Begin(HeliosSSX::EAttack::Skill);W->Tick(2.5f);TestEqual(TEXT("Thin wall blocks every phase"),Enemy->CurrentHP,10000.f);TestEqual(TEXT("Blocked hit submits no impact cue"),W->HitAudioGate.Contacts,WallFeedback);Wall->Destroy();
    const float FriendHP=Pet->Health;FDamageEvent Event;TestEqual(TEXT("Player cannot damage companion"),Pet->TakeDamage(100,Event,PC,W),0.f);TestEqual(TEXT("Companion HP unchanged"),Pet->Health,FriendHP);
    W->Cancel();Enemy->CurrentHP=10000;W->Timeline.Begin(HeliosSSX::EAttack::Heavy);P->StartFireFromAutomation();W->DeactivateWeapon();W->Tick(3);TestEqual(TEXT("Unequipping cancels pending hits"),Enemy->CurrentHP,10000.f);TestTrue(TEXT("Unequipping destroys effects"),W->Effects.IsEmpty());
    TestFalse(TEXT("Unequipping clears held primary"),W->bPrimaryDown);
    if(auto* S=HeliosSSX::Session(P))
    {
        W->ActivateWeapon(TEXT("Player"));P->StartFireFromAutomation();
        S->PlayerRemaining=7;P->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("rifle")));P->EquipWeaponDefinition(D);
        auto* Again=Cast<AHeliosSunShangXiangWeapon>(P->GetEquippedWeapon());TestEqual(TEXT("Cooldown survives destroying and respawning weapon"),Again->CooldownRemaining(),7.f);
        Again->Tick(3/Again->PlaybackSpeed);TestTrue(TEXT("Switching back does not resume held light"),!Again->bPrimaryDown&&Again->Timeline.Attack==HeliosSSX::EAttack::None);
        Again->Cancel();Again->StartSpecialFire();TestTrue(TEXT("G cannot bypass cooldown"),Again->Timeline.Attack==HeliosSSX::EAttack::None);
        World->GetSubsystem<UHeliosSSXClock>()->Tick(2);TestEqual(TEXT("Cooldown advances while unequipped"),S->PlayerRemaining,5.f);
    }
    Pet->Restore(0,3);TestTrue(TEXT("Original death pose enters sleeping state"),Pet->IsSleeping());TestEqual(TEXT("Restored sleep timer"),Pet->SleepRemaining,3.f);Pet->Restore(200,0);TestEqual(TEXT("Companion health restores"),Pet->Health,200.f);
    W=Cast<AHeliosSunShangXiangWeapon>(P->GetEquippedWeapon());
    for(int I=0;I<100;++I)World->Tick(LEVELTICK_TimeOnly,.1f);
    W->Cancel();P->StartFireFromAutomation();
    P->TakeDamage(P->GetMaxHealth(),Event,nullptr,Enemy);TestTrue(TEXT("Lethal damage kills the player"),P->IsDead());
    const int HitsAtDeath=W->HitCount;W->Tick(3);
    TestTrue(TEXT("Death clears held primary and pending light"),!W->bPrimaryDown&&W->Timeline.Attack==HeliosSSX::EAttack::None);
    TestEqual(TEXT("Death prevents pending light damage"),W->HitCount,HitsAtDeath);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosSSXGaitTest,"Helios.SunShangXiang.OriginalGait",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosSSXGaitTest::RunTest(const FString&)
{
    FTestWorldWrapper Wrapper;if(!Wrapper.CreateTestWorld(EWorldType::Game))return false;
    UWorld* World=Wrapper.GetTestWorld();World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!Wrapper.BeginPlayInTestWorld())return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Owner=World->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
    auto* Pet=World->SpawnActor<AHeliosSunShangXiangPet>(FVector(0,500,100),FRotator::ZeroRotator,Spawn);Pet->CompanionOwner=Owner;
    if(!TestTrue(TEXT("Third-person original model is ready"),Pet->bModelReady))return false;
    auto* Mesh=Pet->GetMesh();auto* Anim=Cast<UHeliosHandAnimInstance>(Mesh->GetAnimInstance());
    if(!TestNotNull(TEXT("Runtime timeline animation instance"),Anim))return false;
    const TArray<FVector> Directions={FVector(520,0,0),FVector(0,520,0),FVector(-520,0,0),FVector(0,-520,0)};
    const TArray<FName> Moves={TEXT("Run_Fwd"),TEXT("Run_RT"),TEXT("Run_Bwd"),TEXT("Run_LT")};
    const TArray<FName> Joints={TEXT("b_LeftLegUpper"),TEXT("b_LeftLeg"),TEXT("b_LeftAnkle"),TEXT("b_LeftToe"),
        TEXT("b_RightLegUpper"),TEXT("b_RightLeg"),TEXT("b_RightAnkle"),TEXT("b_RightToe"),TEXT("b_LeftHand"),TEXT("b_RightHand")};
    FString Checks;bool AllPassed=true;
    for(float FPS:{30.f,60.f,120.f})
    {
        const float Dt=1.f/FPS;float WorstScale=0,WorstStep=0,GaitStep=0,RawLightError=0,RawLightStep=0;bool Finite=true,DirectionsMatch=true;
        Pet->CancelAttack();Pet->GetCharacterMovement()->StopMovementImmediately();Pet->UpdateLocomotion(Dt);Pet->SetPose(TEXT("Idle"));
        TArray<FVector> Previous;int32 Sample=0,WorstSample=0;FName WorstJoint;FString WorstPose;
        UAnimSequence* PreviousClip=nullptr;float PreviousTime=0;
        auto Evaluate=[&]()
        {
            Mesh->TickAnimation(Dt,false);Mesh->RefreshBoneTransforms();
            for(const FTransform& Bone:Mesh->GetBoneSpaceTransforms())Finite&=!Bone.ContainsNaN()&&Bone.GetRotation().IsNormalized();
            TArray<FVector> Positions;
            for(FName Joint:Joints)
            {
                if(Mesh->GetBoneIndex(Joint)==INDEX_NONE){Finite=false;continue;}
                Positions.Add(Mesh->GetSocketLocation(Joint));
            }
            const bool SourceStroke=Anim->Sequence&&Anim->Sequence==PreviousClip&&Anim->Sequence->GetName().Contains(TEXT("FireSlight"))&&Anim->SampleSeconds>.12f;
            if(SourceStroke)
            {
                // The authored 30 FPS light stroke moves faster than walking.
                // Compare it to the original raw pose instead of applying a gait limit.
                const auto& Ref=Anim->Sequence->GetSkeleton()->GetReferenceSkeleton();
                auto RawPositions=[&](float Time)
                {
                    TArray<FTransform> WorldPose;WorldPose.SetNum(Ref.GetNum());
                    for(int32 I=0;I<Ref.GetNum();++I)
                    {
                        Anim->Sequence->GetBoneTransform(WorldPose[I],FSkeletonPoseBoneIndex(I),FAnimExtractContext(Time,false),true);
                        const int32 Parent=Ref.GetParentIndex(I);if(Parent!=INDEX_NONE)WorldPose[I]=WorldPose[I]*WorldPose[Parent];
                    }
                    TArray<FVector> Points;for(FName Joint:Joints){const int32 I=Ref.FindBoneIndex(Joint);Points.Add(I!=INDEX_NONE?Mesh->GetComponentTransform().TransformPosition(WorldPose[I].GetLocation()):FVector::ZeroVector);}return Points;
                };
                const auto RawNow=RawPositions(Anim->SampleSeconds),RawBefore=RawPositions(PreviousTime);
                for(int32 I=0;I<Positions.Num();++I)
                {
                    RawLightError=FMath::Max(RawLightError,FVector::Dist(Positions[I],RawNow[I]));
                    RawLightStep=FMath::Max(RawLightStep,FVector::Dist(RawNow[I],RawBefore[I]));
                }
            }
            if(Previous.Num()==Positions.Num())for(int32 I=0;I<Positions.Num();++I)
            {
                const float Step=FVector::Dist(Previous[I],Positions[I]);
                if(!SourceStroke)GaitStep=FMath::Max(GaitStep,Step);
                if(Step>WorstStep){WorstStep=Step;WorstJoint=Joints[I];WorstSample=Sample;WorstPose=Anim->Sequence?Anim->Sequence->GetName():TEXT("None");}
            }
            ++Sample;
            PreviousClip=Anim->Sequence;PreviousTime=Anim->SampleSeconds;
            Previous=MoveTemp(Positions);
        };
        for(int32 Frame=0;Frame<FMath::CeilToInt(.12f*FPS);++Frame)Evaluate();
        for(int32 Direction=0;Direction<Directions.Num();++Direction)
        {
            for(int32 Frame=0;Frame<FMath::CeilToInt(1.6f*FPS);++Frame)
            {
                Pet->GetCharacterMovement()->Velocity=Directions[Direction];Pet->UpdateLocomotion(Dt);Pet->SetPose(TEXT("Move"));Evaluate();
                DirectionsMatch&=Anim->Sequence==HeliosSSX::Animation(false,Moves[Direction]);
                for(FName Joint:Joints)
                {
                    const int32 Index=Mesh->GetBoneIndex(Joint);
                    if(Index==INDEX_NONE){Finite=false;continue;}
                    // Wait for the existing 80 ms transition from the unchanged idle clip.
                    if(Frame*Dt>.12f)
                    {
                        const FVector Scale=Mesh->GetBoneSpaceTransforms()[Index].GetScale3D();
                        WorstScale=FMath::Max(WorstScale,(Scale-FVector::OneVector).GetAbsMax());
                    }
                }
            }
        }
        Pet->GetCharacterMovement()->StopMovementImmediately();Pet->UpdateLocomotion(Dt);Pet->SetPose(TEXT("Idle"));Evaluate();
        const bool StopCorrect=Pet->MoveAge==0&&Anim->Sequence==HeliosSSX::Animation(false,TEXT("Idle"));
        Pet->Timeline.Begin(HeliosSSX::EAttack::Light);Pet->AttackAge=0;
        for(int32 Frame=0;Frame<FMath::CeilToInt(1.3f*FPS);++Frame){Pet->UpdateAttack(Dt);Evaluate();}
        const bool AttackFinished=Pet->Timeline.Attack==HeliosSSX::EAttack::None;
        for(int32 Frame=0;Frame<FMath::CeilToInt(.2f*FPS);++Frame){Pet->GetCharacterMovement()->Velocity=Directions[0];Pet->UpdateLocomotion(Dt);Pet->SetPose(TEXT("Move"));Evaluate();}
        const bool Resumed=Anim->Sequence==HeliosSSX::Animation(false,TEXT("Run_Fwd"));
        UE_LOG(LogTemp,Display,TEXT("SSX_GAIT_DIAG fps=%.0f step=%.3f gait_step=%.3f raw_light_step=%.3f raw_light_error=%.3f joint=%s sample=%d pose=%s"),FPS,WorstStep,GaitStep,RawLightStep,RawLightError,*WorstJoint.ToString(),WorstSample,*WorstPose);
        const bool Pass=Finite&&DirectionsMatch&&StopCorrect&&AttackFinished&&Resumed&&WorstScale<.001f&&GaitStep<60.f&&RawLightError<.3f;
        TestTrue(FString::Printf(TEXT("Original walking poses remain finite and do not stretch at %.0f FPS"),FPS),Finite&&WorstScale<.001f);
        TestTrue(FString::Printf(TEXT("Starting, directional changes, stopping and moving after attack at %.0f FPS"),FPS),DirectionsMatch&&StopCorrect&&AttackFinished&&Resumed);
        TestTrue(FString::Printf(TEXT("Gait and clip transitions stay below 60cm per frame at %.0f FPS"),FPS),GaitStep<60.f);
        TestTrue(FString::Printf(TEXT("Fast light stroke follows the original raw joints within 3mm at %.0f FPS"),FPS),RawLightError<.3f);
        AllPassed&=Pass;if(!Checks.IsEmpty())Checks+=TEXT(",");
        Checks+=FString::Printf(TEXT("{\"fps\":%.0f,\"max_local_scale_error\":%.9f,\"max_joint_step_cm\":%.6f,\"max_gait_step_cm\":%.6f,\"original_light_step_cm\":%.6f,\"original_light_error_cm\":%.6f,\"finite\":%s,\"state_transitions_passed\":%s,\"passed\":%s}"),FPS,WorstScale,WorstStep,GaitStep,RawLightStep,RawLightError,Finite?TEXT("true"):TEXT("false"),DirectionsMatch&&StopCorrect&&AttackFinished&&Resumed?TEXT("true"):TEXT("false"),Pass?TEXT("true"):TEXT("false"));
    }
    const FString Out=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../qa/cfm_sunshangxiang/fixes/walk_pose/runtime_gait.json"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out),true);
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"all_passed\":%s,\"checks\":[%s]}"),AllPassed?TEXT("true"):TEXT("false"),*Checks),*Out,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return AllPassed;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosSSXVoiceTest,"Helios.SunShangXiang.VoiceShuffle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosSSXVoiceTest::RunTest(const FString&)
{
    auto* PlayerVoice=NewObject<UHeliosSSXVoiceComponent>();PlayerVoice->Initialize(false);
    auto* PetVoice=NewObject<UHeliosSSXVoiceComponent>();PetVoice->Initialize(true);
    if(!TestEqual(TEXT("Eight loaded kill voices"),PlayerVoice->GetKillPoolSize(),8))return false;
    TestEqual(TEXT("Eight spatial companion voices"),PetVoice->GetKillPoolSize(),8);
    TSet<USoundBase*> Pool;for(const auto& Sound:PlayerVoice->KillSounds)Pool.Add(Sound);
    TestEqual(TEXT("Distinct imported sound assets"),Pool.Num(),8);
    int32 Last=INDEX_NONE;
    for(int32 Round=0;Round<64;++Round)
    {
        TSet<int32> Heard;
        for(int32 Clip=0;Clip<8;++Clip)
        {
            TestNotNull(TEXT("Selected original audio"),PlayerVoice->NextKillSound());
            const int32 Index=PlayerVoice->GetLastPlayedKillIndex();
            TestTrue(TEXT("No adjacent repeat, including shuffle boundaries"),Index!=Last);
            Heard.Add(Index);Last=Index;
        }
        TestEqual(TEXT("Every shuffle round includes all eight voices"),Heard.Num(),8);
    }
    TestTrue(TEXT("Companion shuffle is independent"),PetVoice->KillBag.IsEmpty()&&PetVoice->LastKillIndex==INDEX_NONE);
    PlayerVoice->PendingKillRequests={1,2};PlayerVoice->Stop();
    TestEqual(TEXT("Explicit stop cancels all pending requests"),PlayerVoice->GetPendingKillCount(),0);
    PlayerVoice->Initialize(false);TestEqual(TEXT("Reinitialization resets the shuffle"),PlayerVoice->LastKillIndex,INDEX_NONE);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosSSXEffectVisibilityTest,"Helios.SunShangXiang.SkillEffectVisibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosSSXEffectVisibilityTest::RunTest(const FString&)
{
    FTestWorldWrapper Wrapper;if(!Wrapper.CreateTestWorld(EWorldType::Game))return false;
    UWorld* World=Wrapper.GetTestWorld();World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!Wrapper.BeginPlayInTestWorld())return false;
    auto* Owner=World->SpawnActor<AActor>();
    auto IsBorder=[](UStaticMeshComponent* C)
    {auto* M=C->GetMaterial(0);return M&&M->GetBaseMaterial()->GetName().StartsWith(TEXT("M_SSXFX_Line24_Trail_"));};
    for(bool FP:{true,false})for(int32 Phase=1;Phase<=4;++Phase)for(float FPS:{30.f,60.f,120.f})for(int32 Repeat=0;Repeat<2;++Repeat)
    {
        const FString Name=FString::Printf(TEXT("FX_%dP_SunShangXiang_Skill%d"),FP?1:3,Phase);
        auto* Effect=AHeliosSSXEffect::Spawn(Owner,Name,FVector::ZeroVector,FRotator::ZeroRotator,FP);
        if(!TestNotNull(Name+TEXT(" spawns"),Effect))return false;
        Effect->SetActorTickEnabled(false);TArray<UStaticMeshComponent*> Components;Effect->GetComponents(Components);
        int32 Borders=0;bool Hidden=true,OtherVisible=false;
        for(auto* C:Components)if(IsBorder(C)){++Borders;Hidden&=!C->IsVisible();}
        TestEqual(Name+TEXT(" retains one disabled border track"),Borders,1);
        // Include the returned spawn frame, every playback frame and expiry.
        for(int32 Frame=0;Frame<FMath::CeilToInt(2.f*FPS)&&IsValid(Effect);++Frame)
        {
            for(auto* C:Components)if(IsBorder(C))Hidden&=!C->IsVisible();else OtherVisible|=C->IsVisible();
            Effect->AddActorWorldOffset(FVector(1,2,0));Effect->AddActorWorldRotation(FRotator(0,1,0));Effect->Tick(1.f/FPS);
        }
        TestTrue(FString::Printf(TEXT("%s stays hidden from spawn through expiry at %.0f FPS, cast %d"),*Name,FPS,Repeat),Hidden);
        TestTrue(Name+TEXT(" keeps other effect layers visible"),OtherVisible);
        TestFalse(Name+TEXT(" expires normally"),IsValid(Effect));
    }
    for(bool FP:{true,false})for(int32 Trail=1;Trail<=3;++Trail)
    {
        const FString Name=FString::Printf(TEXT("FX_%dP_SunShangXiang_Trail%02d"),FP?1:3,Trail);
        auto* Effect=AHeliosSSXEffect::Spawn(Owner,Name,FVector::ZeroVector,FRotator::ZeroRotator,FP);
        if(!TestNotNull(Name+TEXT(" spawns"),Effect))return false;
        TArray<UStaticMeshComponent*> Components;Effect->GetComponents(Components);
        TestEqual(Name+TEXT(" retains both original sword trails"),Components.Num(),2);
        for(auto* C:Components)TestTrue(Name+TEXT(" defaults to enabled without a field"),C->IsVisible());
        Effect->Destroy();
    }
    return true;
}
#endif
