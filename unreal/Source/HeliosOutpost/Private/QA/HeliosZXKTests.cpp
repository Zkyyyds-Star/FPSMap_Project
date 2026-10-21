#if WITH_DEV_AUTOMATION_TESTS
#include "Weapons/HeliosZhongXiaoKui.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosZXKProjectile.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosSunShangXiang.h"
#include "Pets/HeliosSunShangXiangPet.h"
#include "Combat/HeliosCombatTempoSettings.h"
#include "Combat/CombatDamageSettings.h"
#include "Misc/FileHelper.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Pets/HeliosZhongXiaoKuiPet.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Engine/World.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosZXKTest,"Helios.ZhongXiaoKui.WeaponAndCompanion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosZXKTest::RunTest(const FString&)
{
    FTestWorldWrapper Wrapper;if(!Wrapper.CreateTestWorld(EWorldType::Game))return false;
    UWorld* World=Wrapper.GetTestWorld();World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!Wrapper.BeginPlayInTestWorld())return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=World->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
    auto* PC=World->SpawnActor<APlayerController>();PC->Possess(P);PC->SetControlRotation(FRotator::ZeroRotator);
    const float HP=P->GetMaxHealth(),Speed=P->GetCharacterMovement()->MaxWalkSpeed;
    auto* D=UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_zhongxiaokui"));if(!TestNotNull(TEXT("Registered playable FP weapon"),D)||!P->EquipWeaponDefinition(D))return false;
    auto* W=Cast<AHeliosZhongXiaoKuiWeapon>(P->GetEquippedWeapon());if(!TestNotNull(TEXT("Equipped Zhong Xiao Kui weapon actor"),W))return false;
    TestTrue(TEXT("Equipped ZXK retains its source origin after generic gun attachment"),W->GetFirstPersonMesh()->GetRelativeLocation().IsNearlyZero());
    TestNotNull(TEXT("Original FP arms and blades mesh"),W->GetFirstPersonMesh()->GetSkeletalMeshAsset());
    TestFalse(TEXT("Melee consumes no ammunition"),W->UsesAmmunition());TestEqual(TEXT("Weapon preserves player HP"),P->GetMaxHealth(),HP);TestEqual(TEXT("Weapon preserves player movement"),P->GetCharacterMovement()->MaxWalkSpeed,Speed);
    for(int32 Id:{32384,32385,32396,32401,32402,32403,32404,32405,32406,32407})TestNotNull(FString::Printf(TEXT("Verified HunterOni original sound %d loads"),Id),HeliosZXK::OriginalSound(Id));
    TestEqual(TEXT("Original two kill voice variants initialized"),W->Voice->GetKillPoolSize(),2);
    bool Loaded=W->Clips.Num()>=40;for(const auto& Pair:W->Clips)Loaded&=Pair.Value!=nullptr;TestTrue(TEXT("Original A B C FP clips loaded"),Loaded);
    W->Cancel();W->Pose();const FVector Idle=W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_RightHand"));W->StartFiring();W->StopFiring();W->Age=W->Clips.FindRef(W->Action)->GetPlayLength()*.6f;W->Pose();W->GetFirstPersonMesh()->TickAnimation(.15f,false);W->GetFirstPersonMesh()->RefreshBoneTransforms();
    TestTrue(TEXT("FP attack visibly moves the actual wrist"),FVector::Dist(Idle,W->GetFirstPersonMesh()->GetSocketLocation(TEXT("b_RightHand")))>1);
    FVector Eye;FRotator R;P->GetActorEyesViewPoint(Eye,R);
    auto* E=World->SpawnActor<AHeliosEnemyE01>(Eye+FVector(200,0,0),FRotator::ZeroRotator,Spawn);E->StopShooting();E->SetActorTickEnabled(false);E->GetCharacterMovement()->DisableMovement();if(E->GetController())E->GetController()->Destroy();
    for(float FPS:{30.f,60.f,120.f})
    {
        for(bool Heavy:{false,true})
        {
            E->CurrentHP=10000;W->Cancel();const int32 Before=W->HitCount,AudioBefore=W->AudioEventCount;
            if(Heavy)W->StartSecondaryFire();else{W->StartFiring();W->StopFiring();}
            for(int32 I=0;I<FMath::CeilToInt((W->Duration()+.1f)*FPS);++I)W->Tick(1/FPS);
            TestEqual(TEXT("Single tap submits one melee contact"),W->HitCount-Before,1);
            TestEqual(TEXT("Heavy and light use their project damage once"),10000-E->CurrentHP,D->Damage*(Heavy?2.f:1.f));
            TestTrue(TEXT("Original attack and recovery finish"),W->Age<0);
            TestEqual(TEXT("Source attack audio event dispatches exactly once at every tested frame rate"),W->AudioEventCount-AudioBefore,1);
        }
    }
    E->SetActorLocation(Eye+FVector(2000,0,0));W->Cancel();const int32 Miss=W->HitCount;W->StartFiring();W->StopFiring();W->Tick(3);TestEqual(TEXT("Empty swing causes no contact"),W->HitCount,Miss);
    for(float FPS:{30.f,60.f,120.f})for(int32 Form:{0,1})
    {
        W->Cancel();W->Form=Form;W->PendingForm=Form;TArray<float> Lengths;
        const int32 Count=Form==0?3:5;
        for(int32 I=0;I<Count;++I){W->Age=-1;W->StartAttack(false);Lengths.Add(W->Duration());}
        // 8 seconds lands exactly on a source clip boundary. Sample between
        // source keys so floating-point endpoint ownership cannot mask clock drift.
        constexpr float SampleTime=8.125f;const int32 Frames=FMath::FloorToInt(SampleTime*FPS);
        W->Cancel();W->StartFiring();for(int32 Frame=0;Frame<Frames;++Frame){W->Tick(1.f/FPS);W->GetFirstPersonMesh()->TickAnimation(1.f/FPS,false);W->GetFirstPersonMesh()->RefreshBoneTransforms();}
        const float Tail=SampleTime-Frames/FPS;if(Tail>UE_SMALL_NUMBER)W->Tick(Tail);
        float Remaining=SampleTime;int32 Expected=0;while(Remaining>=Lengths[Expected%Count]){Remaining-=Lengths[Expected%Count];++Expected;}
        TestEqual(TEXT("Held A/B combo count is independent of frame rate"),W->Combo,Expected+1);
        TestTrue(TEXT("Held animation retains fractional overshoot at 30/60/120 Hz"),FMath::Abs(W->Age-Remaining)<.002f);
        W->Cancel();
    }
    W->Form=0;W->PendingForm=0;
    E->SetActorLocation(Eye+FVector(200,0,0));E->CurrentHP=10000;W->Cancel();W->StartFiring();for(int32 I=0;I<180;++I)W->Tick(1.f/60);TestTrue(TEXT("Held primary continues the three source light swings"),W->HitCount-Miss>=3);W->DeactivateWeapon();TestTrue(TEXT("Unequip cancels pending attack and held input"),W->Age<0&&!W->bHeld);
    W->ActivateWeapon(NAME_None);W->ToggleForm();TestEqual(TEXT("Source A to B transition starts"),W->Action,FName(TEXT("Change_AToB")));W->Tick(3);TestEqual(TEXT("B form becomes playable"),W->Form,1);
    for(int32 I=0;I<5;++I){W->StartFiring();W->StopFiring();TestTrue(TEXT("B uses original five-step attack clips"),W->Action.ToString().StartsWith(TEXT("B_FireSlight")));W->Tick(3);}
    W->StartSpecialFire();TestEqual(TEXT("B skill uses its original animation"),W->Action,FName(TEXT("B_Skill01")));W->Tick(3);W->StartSpecialFire();TestTrue(TEXT("Local skill cooldown prevents an immediate duplicate"),W->Age<0);
    W->StartSecondaryFire();W->StopSecondaryFire();TestEqual(TEXT("Q B-form right-click uses sword-wave source action"),W->Action,FName(TEXT("B_Skill02")));W->Tick(3);TestTrue(TEXT("Original sword wave action creates a damaging projectile"),!W->Projectiles.IsEmpty());W->Cancel();
    const int32 WaveStart=W->WaveShots;W->StartSecondaryFire();
    // This timing fixture emits 48 waves. Preserve the separate damage target
    // for the later projectile and companion tests rather than killing it here.
    const FVector TargetLocation=E->GetActorLocation();E->SetActorLocation(Eye+FVector(0,10000,0));
    // This synchronous test does not advance the engine frame counter: tick
    // world time for the real cooldown and drive the weapon exactly once/frame.
    W->SetActorTickEnabled(false);
    for(float FPS:{30.f,60.f,120.f})
    {
        TArray<float> ShotTimes;const int32 InitialShots=W->WaveShots;
        for(int32 I=0;I<int32(8*FPS);++I)
        {
            World->Tick(LEVELTICK_All,1.f/FPS);const int32 PreviousShots=W->WaveShots;
            // Exercise repeated taps and held input without restarting an active cast.
            if(I<int32(4*FPS)){W->StartSecondaryFire();W->StopSecondaryFire();}
            else W->StartSecondaryFire();
            W->Tick(1.f/FPS);
            if(W->WaveShots>PreviousShots)ShotTimes.Add(World->GetTimeSeconds());
        }
        TestTrue(FString::Printf(TEXT("3x sword-wave cadence emits at least 15 shots in 8 seconds at %.0f FPS"),FPS),W->WaveShots-InitialShots>=15);
        for(int32 I=1;I<ShotTimes.Num();++I)
            TestTrue(FString::Printf(TEXT("Actual sword-wave interval is 0.5 seconds within one frame at %.0f FPS"),FPS),FMath::Abs(ShotTimes[I]-ShotTimes[I-1]-.5f)<=1.f/FPS+.001f);
        W->StopSecondaryFire();W->Cancel();
    }
    W->SetActorTickEnabled(true);
    E->SetActorLocation(TargetLocation);
    W->StopSecondaryFire();TestTrue(TEXT("Held B secondary repeats waves across actual world cooldown"),W->WaveShots-WaveStart>=3);
    W->Cancel();W->ToggleForm();W->Tick(3);TestEqual(TEXT("Q returns to A after repeated waves"),W->Form,0);
    W->StartSecondaryFire();W->StopSecondaryFire();TestEqual(TEXT("Untransformed right-click retains heavy action"),W->Action,FName(TEXT("FireHeavy")));W->Tick(3);W->ToggleForm();W->Tick(3);
    W->ToggleRangedForm();W->Tick(3);TestEqual(TEXT("C flying dart form becomes playable"),W->Form,2);TestNotNull(TEXT("Original FP dart mesh loads"),W->Dart->GetStaticMesh().Get());TestTrue(TEXT("Original dart is visible in C form"),W->Dart->IsVisible());
    // Original C_Idle feidao world matrix has scale .75 (source_forms_diagnostic.json).
    // Preserve that source animation scale rather than forcing the raw 24.14 cm mesh length.
    W->GetFirstPersonMesh()->TickAnimation(.15f,false);W->GetFirstPersonMesh()->RefreshBoneTransforms();W->Dart->UpdateComponentToWorld();
    const FVector DartSize=W->Dart->GetStaticMesh()->GetBounds().BoxExtent*2*W->Dart->GetComponentScale().GetAbs();AddInfo(FString::Printf(TEXT("Original attached dart size=%s scale=%s"),*DartSize.ToString(),*W->Dart->GetComponentScale().ToString()));TestTrue(TEXT("Attached dart preserves original C_Idle .75 scale and 18.106 centimeter length"),FMath::Abs(DartSize.GetMax()-18.1062f)<.05f);
    W->StartFiring();W->StopFiring();W->Tick(3);TestEqual(TEXT("C throw consumes one local dart"),W->DartAmmo,19);TestEqual(TEXT("C throw creates actual projectile"),W->Projectiles.Num(),1);
    TArray<AActor*> DartEffects;W->Projectiles[0]->GetAttachedActors(DartEffects);TestEqual(TEXT("Original source dart particle trail attaches to the actual projectile"),DartEffects.Num(),1);
    for(float FPS:{30.f,60.f,120.f})for(bool Wave:{false,true})
    {
        auto* Projectile=World->SpawnActor<AHeliosZXKProjectile>(Eye+FVector(0,1000,0),FRotator::ZeroRotator,Spawn);
        Projectile->Initialize(W,FVector::ForwardVector,100,Wave,Eye);
        TArray<AActor*> Attached;Projectile->GetAttachedActors(Attached);
        TestEqual(TEXT("Traveling source geometry has one owned effect"),Attached.Num(),1);
        if(Attached.IsEmpty()){Projectile->Destroy();continue;}
        auto* Effect=Cast<AHeliosSSXEffect>(Attached[0]);
        Projectile->Tick(1/FPS);Effect->Tick(.1f);
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Effect);TArray<FVector> BeforeMeshes;
        for(auto* M:Meshes)BeforeMeshes.Add(M->GetComponentLocation());
        Projectile->Tick(1/FPS);Effect->Tick(0);
        for(int32 I=0;I<Meshes.Num();++I)TestTrue(TEXT("Displayed dart/wave geometry travels with projectile at 30/60/120 Hz"),FMath::Abs((Meshes[I]->GetComponentLocation()-BeforeMeshes[I]).X-6000/FPS)<.01f);
        if(Wave){Effect->Tick(1.8f);TestTrue(TEXT("Sword wave geometry remains visible near the end of configured travel lifetime"),Meshes.ContainsByPredicate([](const auto* M){return M->IsVisible();}));}
        Projectile->Tick(2.1f);TestFalse(TEXT("Expired projectile is destroyed"),IsValid(Projectile));
        TestFalse(TEXT("Expired projectile leaves no hovering effect actor"),IsValid(Effect));
    }
    const int32 ProjectileHits=W->HitCount;for(int32 I=0;I<20;++I)if(IsValid(W->Projectiles[0]))W->Projectiles[0]->Tick(1.f/60);TestEqual(TEXT("Projectile damages each enemy at most once"),W->HitCount-ProjectileHits,1);
    W->RequestSourceReload();W->Tick(3);TestEqual(TEXT("Original reload animation restores local dart clip"),W->DartAmmo,20);
    W->DeactivateWeapon();TestEqual(TEXT("Unequip cleans up active projectiles"),W->Projectiles.Num(),0);W->ActivateWeapon(NAME_None);
    W->ToggleRangedForm();W->Tick(3);W->Tick(3);TestEqual(TEXT("A ranged shortcut completes both original transitions"),W->Form,2);W->DeactivateWeapon();
    auto* Pet=World->SpawnActor<AHeliosZhongXiaoKuiPet>(Eye+FVector(0,0,-40),FRotator::ZeroRotator,Spawn);Pet->CompanionOwner=P;Pet->Target=E;
    TestTrue(TEXT("Visible independent original companion"),Pet->bModelReady&&Pet->GetMesh()->GetSkeletalMeshAsset()!=W->GetFirstPersonMesh()->GetSkeletalMeshAsset());
    TestEqual(TEXT("Old pet enum values remain stable"),int32(EHeliosPetKind::SunShangXiang),2);
    E->CurrentHP=10000;for(int32 I=0;I<150;++I)Pet->UpdateAttack(1.f/60);
    TestTrue(TEXT("Companion source attack damages a nearby enemy"),E->CurrentHP<10000&&Pet->Contacts>0);
    Pet->GetCharacterMovement()->Velocity=FVector(520,0,0);Pet->UpdateLocomotion(.2f);Pet->SetPose(TEXT("Move"));TestTrue(TEXT("Companion uses source locomotion clock"),Pet->MoveAge>0);
    Pet->CancelAttack();Pet->Target.Reset();Pet->HitPoseRemaining=0;
    Pet->GetCharacterMovement()->Velocity=FVector(471.1924f,0,0);Pet->SetActorRotation(FRotator::ZeroRotator);
    Pet->UpdateLocomotion(.2f);Pet->UpdateAttack(.2f);
    Pet->GetMesh()->TickAnimation(.15f,false);Pet->GetMesh()->RefreshBoneTransforms();
    TestEqual(TEXT("Following without a target applies moving pose every frame"),Pet->CurrentPose,FName(TEXT("Move")));
    const FVector Foot=Pet->GetMesh()->GetSocketLocation(TEXT("b_LeftAnkle"));
    Pet->UpdateLocomotion(.2f);Pet->UpdateAttack(.2f);Pet->GetMesh()->TickAnimation(.15f,false);Pet->GetMesh()->RefreshBoneTransforms();
    TestTrue(TEXT("Actual following ankle advances through source stride"),FVector::Dist(Foot,Pet->GetMesh()->GetSocketLocation(TEXT("b_LeftAnkle")))>1);
    Pet->GetCharacterMovement()->Velocity=FVector::ZeroVector;Pet->UpdateLocomotion(.1f);Pet->UpdateAttack(.1f);
    TestEqual(TEXT("Stopping returns to idle pose"),Pet->CurrentPose,FName(TEXT("Idle")));
    TestEqual(TEXT("ZXK does not inherit doubled companion speed"),Pet->Settings.MovementMultiplier,1.f);
    if(FParse::Param(FCommandLine::Get(),TEXT("HeliosHunterSettingsMutation")))
    {
        struct FRestoreHunterFiles
        {
            TMap<FString,TArray<uint8>> Originals;
            ~FRestoreHunterFiles(){for(const auto& Pair:Originals)FFileHelper::SaveArrayToFile(Pair.Value,*Pair.Key);}
        } Restore;
        for(bool ZXK:{false,true})
        {
            const FString Path=UCombatDamageSettings::Directory()/(ZXK?TEXT("钟小葵.ini"):TEXT("孙尚香.ini"));
            TArray<uint8> Bytes;if(!TestTrue(TEXT("Read exact editable hunter INI before mutation"),FFileHelper::LoadFileToArray(Bytes,*Path)))return false;
            Restore.Originals.Add(Path,Bytes);FString Text;FFileHelper::LoadFileToString(Text,*Path);
            FConfigFile C;C.ProcessInputFileContents(Text,Path);
            C.SetFloat(TEXT("武器"),ZXK?TEXT("轻击伤害"):TEXT("轻击整组伤害"),ZXK?137:124);
            C.SetFloat(TEXT("伙伴"),ZXK?TEXT("轻击伤害"):TEXT("轻击整组伤害"),ZXK?159:66);
            C.SetFloat(TEXT("伙伴"),TEXT("生命"),ZXK?702:701);
            if(ZXK){C.SetFloat(TEXT("武器"),TEXT("剑气速度"),1234);C.SetFloat(TEXT("武器"),TEXT("剑气冷却"),.75f);C.SetFloat(TEXT("武器"),TEXT("剑气动作倍率"),2.f);C.SetString(TEXT("武器"),TEXT("飞镖速度"),TEXT("-5"));}
            if(!TestTrue(TEXT("Write concrete changed hunter test values"),C.Write(Path)))return false;
        }
        TestEqual(TEXT("Existing world retains loaded SSX snapshot after text edit"),HeliosSSX::Damage(P,HeliosSSX::EAttack::Light,false),50.f);
        FTestWorldWrapper NewWorld;if(!NewWorld.CreateTestWorld(EWorldType::Game))return false;
        auto* NW=NewWorld.GetTestWorld();NW->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();if(!NewWorld.BeginPlayInTestWorld())return false;
        auto* NP=NW->SpawnActor<AHeliosArenaCharacter>(FVector(0,0,100),FRotator::ZeroRotator,Spawn);
        auto* NPC=NW->SpawnActor<APlayerController>();NPC->Possess(NP);NPC->SetControlRotation(FRotator::ZeroRotator);
        auto* SP=NW->SpawnActor<AHeliosSunShangXiangPet>(FVector(0,1000,100),FRotator::ZeroRotator,Spawn);auto* ZP=NW->SpawnActor<AHeliosZhongXiaoKuiPet>(FVector(0,-1000,100),FRotator::ZeroRotator,Spawn);
        TestEqual(TEXT("New map SSX actual companion health reads edited file"),SP->Health,701.f);
        TestEqual(TEXT("New map ZXK actual companion health reads edited file"),ZP->Health,702.f);
        TestEqual(TEXT("SSX weapon actual damage resolver reads edited pair value"),HeliosSSX::Damage(NP,HeliosSSX::EAttack::Light,false),62.f);
        TestEqual(TEXT("SSX companion actual damage resolver reads independent edited pair value"),HeliosSSX::Damage(SP,HeliosSSX::EAttack::Light,true),33.f);
        TestEqual(TEXT("ZXK companion strike reads edited independent damage"),UHeliosCombatTempoSettings::Hunter(ZP,true,true,TEXT("轻击伤害"),100),159.f);
        NP->EquipWeaponDefinition(D);auto* ZW=Cast<AHeliosZhongXiaoKuiWeapon>(NP->GetEquippedWeapon());
        FVector NEye;FRotator NR;NP->GetActorEyesViewPoint(NEye,NR);
        auto* NE=NW->SpawnActor<AHeliosEnemyE01>(NEye+NR.Vector()*200,FRotator::ZeroRotator,Spawn);
        NE->CurrentHP=10000;NE->SetActorTickEnabled(false);NE->StopShooting();NE->GetCharacterMovement()->DisableMovement();if(NE->GetController())NE->GetController()->Destroy();
        ZW->StartFiring();ZW->StopFiring();ZW->Tick(3);TestEqual(TEXT("ZXK live weapon strike applies concrete edited137 damage"),10000-NE->CurrentHP,137.f);
        auto* Shot=NW->SpawnActor<AHeliosZXKProjectile>(NEye,NR,Spawn);Shot->Initialize(ZW,NR.Vector(),200,true,NEye);const FVector From=Shot->GetActorLocation();Shot->Tick(.1f);
        TestTrue(TEXT("Actual sword-wave movement reads edited1234 cm/s"),FMath::IsNearlyEqual(FVector::Distance(From,Shot->GetActorLocation()),123.4f,.01f));
        AddExpectedError(TEXT("飞镖速度 raw='-5' invalid/missing"),EAutomationExpectedErrorFlags::Contains,1);
        TestEqual(TEXT("Invalid negative projectile setting uses safe6000 fallback"),UHeliosCombatTempoSettings::Hunter(ZW,true,false,TEXT("飞镖速度"),6000,10,50000),6000.f);
        ZW->Cancel();ZW->ToggleForm();ZW->Tick(3);
        ZW->StartSecondaryFire();ZW->StopSecondaryFire();
        TestEqual(TEXT("Edited sword-wave cooldown schedules actual next cast at .75 seconds"),ZW->WaveCooldownEnd-NW->GetTimeSeconds(),.75);
        ZW->Tick(.05f);
        TestTrue(TEXT("Edited sword-wave rate2 advances actual source clock by .1 seconds"),FMath::IsNearlyEqual(ZW->Age,.1f,.0001f));
        ZW->Cancel();
        AddInfo(TEXT("Mutation RAII restores both original INI byte arrays on every exit."));
    }
    return true;
}
#endif
