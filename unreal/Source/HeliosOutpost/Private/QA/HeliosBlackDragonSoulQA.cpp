#include "QA/HeliosBlackDragonSoulQA.h"
#include "QA/HeliosAsyncFrameCapture.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/HeliosBlackDragonSoul.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Pets/HeliosHumanoidPet.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "UI/HeliosAdventureHUD.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Boss/HeliosEnemyBoss.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif
#if WITH_DEV_AUTOMATION_TESTS
extern TFunction<void(bool,bool)> HeliosMenuQANavigate;
#endif
namespace
{
bool Label(const TSharedRef<SWidget>& W,const FString& Text)
{if(W->GetTypeAsString()==TEXT("STextBlock")&&StaticCastSharedRef<STextBlock>(W)->GetText().ToString()==Text)return true;auto* C=W->GetChildren();for(int I=0;C&&I<C->Num();++I)if(Label(C->GetChildAt(I),Text))return true;return false;}
TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& W,const FString& Text,const FString& Type)
{if(!W->GetVisibility().IsVisible())return nullptr;if(W->GetTypeAsString()==Type&&Label(W,Text))return W;auto* C=W->GetChildren();for(int I=0;C&&I<C->Num();++I)if(auto R=Find(C->GetChildAt(I),Text,Type))return R;return nullptr;}
TSharedPtr<SWidget> Find(const FString& Text,const FString& Type)
{if(FSlateApplication::IsInitialized())for(const auto& W:FSlateApplication::Get().GetInteractiveTopLevelWindows())if(auto R=Find(W->GetContent(),Text,Type))return R;return nullptr;}
bool Click(const FString& Text)
{auto B=Find(Text,TEXT("SButton"));if(!B)return false;auto& S=FSlateApplication::Get();FWidgetPath P;if(!S.GeneratePathToWidgetUnchecked(B.ToSharedRef(),P))return false;auto G=B->GetCachedGeometry();auto At=G.LocalToAbsolute(G.GetLocalSize()*.5f);TSet<FKey> Down;Down.Add(EKeys::LeftMouseButton);TSet<FKey> Up;FPointerEvent Press(FSlateApplication::CursorPointerIndex,At,At,Down,EKeys::LeftMouseButton,0,FModifierKeysState()),Release(FSlateApplication::CursorPointerIndex,At,At,Up,EKeys::LeftMouseButton,0,FModifierKeysState());S.ReleaseAllPointerCapture();B->OnMouseEnter(G,Press);auto A=B->OnMouseButtonDown(G,Press);S.ProcessReply(P,A,&P,&Press);auto R=B->OnMouseButtonUp(G,Release);S.ProcessReply(P,R,&P,&Release);return A.IsEventHandled()&&R.IsEventHandled();}
}
AHeliosBlackDragonSoulQA::AHeliosBlackDragonSoulQA(){PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;}
void AHeliosBlackDragonSoulQA::Check(const TCHAR* Name,bool Pass)
{auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Name);R->SetBoolField(TEXT("passed"),Pass);Checks.Add(MakeShared<FJsonValueObject>(R));if(!Pass)++Failures;UE_LOG(LogTemp,Display,TEXT("HLH_QA %s %s"),Name,Pass?TEXT("PASS"):TEXT("FAIL"));}
void AHeliosBlackDragonSoulQA::Capture(const TCHAR* Name){FScreenshotRequest::RequestScreenshot(Out/(FString(Name)+TEXT(".png")),true,false);}
AHeliosEnemyActor* AHeliosBlackDragonSoulQA::SpawnTarget(float Distance,bool Boss)
{
 if(IsValid(Target))Target->Destroy();FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 Target=GetWorld()->SpawnActor<AHeliosEnemyActor>(Boss?AHeliosEnemyBoss::StaticClass():AHeliosEnemyE01::StaticClass(),Player->GetActorLocation()+FVector(0,Distance,0),FRotator(0,-90,0),P);
 if(Target){Target->CurrentHP=100000;Target->SetActorTickEnabled(false);Target->StopShooting();Target->GetCharacterMovement()->DisableMovement();if(auto* AI=Cast<AAIController>(Target->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("short HLH fixture"));}}
 FVector Eye;FRotator Aim;Player->GetActorEyesViewPoint(Eye,Aim);Player->GetController()->SetControlRotation((Target->GetActorLocation()-Eye).Rotation());return Target;
}
void AHeliosBlackDragonSoulQA::ClearFixtures()
{for(const auto& Actor:Fixtures)if(IsValid(Actor))Actor->Destroy();Fixtures.Reset();Ordinary.Reset();}
AHeliosEnemyActor* AHeliosBlackDragonSoulQA::PassiveEnemy(UClass* Type,const FVector& At,float HP)
{
 FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* E=GetWorld()->SpawnActor<AHeliosEnemyActor>(Type,At,FRotator(0,-90,0),Params);
 if(!E)return nullptr;if(HP>0)E->CurrentHP=HP;E->SetActorTickEnabled(false);E->StopShooting();E->GetCharacterMovement()->DisableMovement();
 if(auto* AI=Cast<AAIController>(E->GetController())){AI->StopMovement();if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("bounded cannon fixture"));}
 Fixtures.Add(E);return E;
}
void AHeliosBlackDragonSoulQA::BlastGroup()
{
 ClearFixtures();SpawnTarget(1500);const FVector At=Target->GetActorLocation();
 UClass* Types[]={AHeliosEnemyE01::StaticClass(),AHeliosEnemyE02::StaticClass(),AHeliosEnemyE03::StaticClass(),AHeliosEnemyE04::StaticClass()};
 const FVector Positions[]={FVector(240,0,0),FVector(-240,180,0),FVector(-380,-120,0),FVector(420,100,0)};
 for(int I=0;I<4;++I)Ordinary.Add(PassiveEnemy(Types[I],At+Positions[I]));
 Edge=PassiveEnemy(AHeliosEnemyE01::StaticClass(),At+FVector(-760,0,0),100000);
 Outside=PassiveEnemy(AHeliosEnemyE01::StaticClass(),At+FVector(950,-300,0),100000);
 Blocked=PassiveEnemy(AHeliosEnemyE01::StaticClass(),At+FVector(650,0,0),100000);
 Friendly=PassiveEnemy(AHeliosEnemyE01::StaticClass(),At+FVector(-560,200,0),100000);Friendly->Tags.Add(TEXT("Friendly"));
 auto* Wall=GetWorld()->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);
 Box->SetBoxExtent(FVector(25,160,180));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Box->RegisterComponent();Wall->SetActorLocation(At+FVector(520,0,0));Fixtures.Add(Wall);
}
void AHeliosBlackDragonSoulQA::Finish()
{
 HeliosAsyncFrameCapture::Finish(Out);
 UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("runtime_audio"),Out);
 auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("passed"),Failures==0);R->SetArrayField(TEXT("checks"),Checks);R->SetNumberField(TEXT("center_damage"),NearDamage);R->SetNumberField(TEXT("edge_damage"),FarDamage);R->SetNumberField(TEXT("companion_shots"),Pet?Pet->ShotsFired:0);R->SetNumberField(TEXT("explosion_radius_cm"),800);R->SetStringField(TEXT("scope"),TEXT("Bounded real-rendering cannon smoke: actual ordinary enemy types, radial damage/cover/friends, source flight/explosion VFX, reload/switch/pause, boss and companion. Passive targets and paused wave director."));FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/TEXT("report.json")));Stage=99;Next=FPlatformTime::Seconds()+2;
}
void AHeliosBlackDragonSoulQA::Tick(float Dt)
{
 Super::Tick(Dt);const double Wall=FPlatformTime::Seconds();if(Stage==99){if(Wall>=Next)FPlatformMisc::RequestExit(false);return;}
 if(!Started){Started=Wall;FParse::Value(FCommandLine::Get(),TEXT("HLHQAOut="),Out);if(Out.IsEmpty())Out=FPaths::ProjectDir()/TEXT("../qa/cfm_heilonghun/runtime");IFileManager::Get().MakeDirectory(*Out,true);}
 if(Wall-Started>(FParse::Param(FCommandLine::Get(),TEXT("HeliosBlackDragonSoulMotionQA"))?180:100)){Check(TEXT("timeout"),false);Finish();return;}
 auto* PC=UGameplayStatics::GetPlayerController(this,0);auto* H=PC?Cast<AHeliosAdventureHUD>(PC->GetHUD()):nullptr;auto* D=AHeliosAdventureDirector::Find(GetWorld());Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(this,0));if(!H||!D||!Player||Wall-Started<5)return;
 Age+=Dt;if(Wall<Next)return;auto Advance=[&](int S){Stage=S;Age=0;Next=Wall+.5;};auto* W=Cast<AHeliosBlackDragonSoulWeapon>(Player->GetEquippedWeapon());
 switch(Stage){
 case 0:
#if WITH_DEV_AUTOMATION_TESTS
  if(HeliosMenuQANavigate)HeliosMenuQANavigate(true,false);
#endif
  Advance(1);return;
 case 1:Check(TEXT("weapon_tab"),Click(TEXT("玩家主武器")));Advance(101);return;
 case 101:{auto B=Find(TEXT("黑龙魂"),TEXT("SButton"));auto S=Find(TEXT("黑龙魂"),TEXT("SScrollBox"));if(B&&S)StaticCastSharedPtr<SScrollBox>(S)->ScrollDescendantIntoView(B,true,EDescendantScrollDestination::Center);Advance(2);return;}
 case 2:Check(TEXT("menu_button"),Click(TEXT("黑龙魂")));Check(TEXT("menu_selection"),H->GetPendingWeaponId()==TEXT("cfm_heilonghun"));Capture(TEXT("01_menu"));Advance(3);return;
 case 3:H->StartSelectedRun();D->SetActorTickEnabled(false);D->CleanupOwnedActors();Player->SetCanBeDamaged(false);Player->SetActorLocation(FVector(0,-2800,102),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(FRotator(0,90,0));Advance(4);return;
 case 4:
  if(Age<4)return;Check(TEXT("weapon_and_selected_appearance"),W&&Player->GetCharacterPresentation()->IsReady());if(!W){Finish();return;}for(TActorIterator<AHeliosHumanoidPet> It(GetWorld());It;++It){It->SetActorTickEnabled(false);if(auto* Source=It->GetEquippedWeapon())Source->StopFiring();}UAudioMixerBlueprintLibrary::StartRecordingOutput(this,65);Capture(TEXT("02_first_person"));BlastGroup();Advance(41);return;
 case 41:
  {const TCHAR* Names[]={TEXT("P_Gunfire_1P_SanDan_HeiLong"),TEXT("P_Gunfire_3P_SanDan_HeiLong"),TEXT("FX_HLH_Projectile1P"),TEXT("FX_HLH_Projectile3P"),TEXT("FX_HLH_Explosion")};
   for(const auto* Name:Names)if(auto* E=AHeliosSSXEffect::Spawn(Player,Name,Player->GetActorLocation()+FVector(0,500,0),FRotator::ZeroRotator,FString(Name).StartsWith(TEXT("P_Gunfire_1P")),1,TEXT("/Game/Helios/CFMBlackDragonSoul/Effects/DA_HLH_Effects")))Fixtures.Add(E);
  }Stage=409;Next=Wall+2;return;
 case 409:
#if WITH_EDITOR
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
#endif
  // Looping warm-up actors have served their purpose; preserve damage fixtures.
  for(const auto& A:Fixtures)if(auto* E=Cast<AHeliosSSXEffect>(A.Get()))E->Destroy();
  Stage=410;Next=FPlatformTime::Seconds()+1;return;
 case 410:Player->StartFireFromAutomation();Player->StopFireFromAutomation();Check(TEXT("one_shell_one_projectile"),W->GetBulletCount()==0&&W->ShotsFired==1&&IsValid(W->LastProjectile));Projectile=W->LastProjectile;Stage=4101;Next=Wall+.02;Age=0;return;
 // Freeze only while saving screenshots, so PNG readback cannot skip flight.
 case 4101:UGameplayStatics::SetGamePaused(this,true);Capture(TEXT("03_fire"));Stage=4102;Next=Wall+.4;return;
 case 4102:UGameplayStatics::SetGamePaused(this,false);PC->SetControlRotation(FRotator(0,105,0));Stage=411;Next=Wall+.04;return;
 case 411:Check(TEXT("original_flight_visible"),IsValid(Projectile)&&Projectile->FlightEffect&&Projectile->FlightEffect->VisibleTrackCount()>0);UGameplayStatics::SetGamePaused(this,true);Capture(TEXT("04_flight"));Stage=4112;Next=Wall+.4;return;
 case 4112:UGameplayStatics::SetGamePaused(this,false);Stage=412;Next=Wall+.1;return;
 case 412:if(IsValid(Projectile))return;Stage=4121;Next=Wall+.08;return;
 case 4121:UGameplayStatics::SetGamePaused(this,true);Capture(TEXT("05_explosion"));Stage=4122;Next=Wall+.4;return;
 case 4122:UGameplayStatics::SetGamePaused(this,false);Advance(42);return;
 case 42:{auto* State=UHeliosBlackDragonSoulState::Get(Player);NearDamage=100000-Target->CurrentHP;FarDamage=100000-Edge->CurrentHP;
  Check(TEXT("center_damage_1200"),FMath::IsNearlyEqual(NearDamage,1200.f,.1f));Check(TEXT("radial_edge_falloff"),FarDamage>=600&&FarDamage<660);
  Check(TEXT("ordinary_group_cleared"),Ordinary.Num()==4&&Ordinary.ContainsByPredicate([](const auto& E){return !E||!E->IsEnemyDead();})==false);
  Check(TEXT("outside_radius_safe"),Outside->CurrentHP==100000);Check(TEXT("cover_blocks_blast"),Blocked->CurrentHP==100000);Check(TEXT("friendly_ignored"),Friendly->CurrentHP==100000);
  Check(TEXT("single_explosion_unique_damage"),State->Explosions==1&&State->EnemyHits==6);Check(TEXT("one_knockback_per_survivor"),State->Knockbacks==2);
  bool Visible=false;for(const auto& A:State->WorldActors)if(auto* E=Cast<AHeliosSSXEffect>(A.Get());E&&E->VisibleTrackCount()>0)Visible=true;Check(TEXT("original_world_explosion_visible"),Visible);}Advance(5);return;
 case 5:Check(TEXT("automatic_reload"),W&&W->ReloadRemaining()>0);Check(TEXT("switch_away"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_chan_zeus"))));Advance(6);return;
 case 6:if(Age<.7f)return;Check(TEXT("switch_back"),Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_heilonghun"))));Advance(7);return;
 case 7:Check(TEXT("reload_progress_preserved"),W&&W->GetBulletCount()==0&&W->ReloadRemaining()>0&&W->ReloadRemaining()<3.44f);RemainingBeforePause=W?W->ReloadRemaining():0;UGameplayStatics::SetGamePaused(this,true);Next=Wall+.7;Stage=8;return;
 case 8:Check(TEXT("pause_freezes_reload"),W&&FMath::IsNearlyEqual(W->ReloadRemaining(),RemainingBeforePause,.01f));UGameplayStatics::SetGamePaused(this,false);Advance(89);return;
 case 89:Capture(TEXT("06_reload"));Advance(9);return;
 case 9:if(W&&W->ReloadRemaining()>0)return;Check(TEXT("restored_shell"),W&&W->GetBulletCount()==1);ClearFixtures();if(IsValid(Target))Target->Destroy();PC->SetControlRotation(FRotator(89,90,0));Advance(91);return;
 case 91:Player->StartFireFromAutomation();Player->StopFireFromAutomation();Projectile=W->LastProjectile;Check(TEXT("switch_during_flight"),IsValid(Projectile)&&Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_chan_zeus"))));PausedProjectile=Projectile?Projectile->GetActorLocation():FVector::ZeroVector;UGameplayStatics::SetGamePaused(this,true);Stage=92;Next=Wall+.6;return;
 case 92:Check(TEXT("paused_projectile_frozen"),IsValid(Projectile)&&Projectile->GetActorLocation().Equals(PausedProjectile,.01));UGameplayStatics::SetGamePaused(this,false);Check(TEXT("flight_survives_weapon_replace"),IsValid(Projectile)&&Player->EquipWeaponDefinition(UHeliosWeaponRegistry::FindWeapon(TEXT("cfm_heilonghun"))));Advance(93);return;
 case 93:if(IsValid(Projectile))return;Check(TEXT("max_range_airburst_once"),Projectile&&Projectile->bExploded&&FMath::IsNearlyEqual(Projectile->Travelled,3000.f,.1f));Advance(10);return;
 case 10:if(W&&W->ReloadRemaining()>0)return;UHeliosBlackDragonSoulState::Get(Player)->Ammo=0;Advance(110);return;
 case 110:Player->StartReloadFromAutomation();Check(TEXT("manual_reload"),W&&W->ReloadRemaining()>0);Advance(111);return;
 case 111:if(W&&W->ReloadRemaining()>0)return;SpawnTarget(650,true);Target->CurrentHP=3600;Player->SetCanBeDamaged(true);BeforeKnockbacks=UHeliosBlackDragonSoulState::Get(Player)->Knockbacks;Advance(112);return;
 case 112:Player->StartFireFromAutomation();Player->StopFireFromAutomation();Advance(113);return;
 case 113:Check(TEXT("boss_high_damage_survives_no_knockback"),FMath::IsNearlyEqual(Target->CurrentHP,2400.f,.1f)&&UHeliosBlackDragonSoulState::Get(Player)->Knockbacks==BeforeKnockbacks);Check(TEXT("owner_not_damaged"),Player->GetHealth()==Player->GetMaxHealth());Advance(11);return;
 case 11:if(W&&W->ReloadRemaining()>0)return;SpawnTarget(650);{auto L=Player->GetLoadout();L.CharacterId=TEXT("cfm_chan");L.CharacterVariantId=NAME_None;L.PrimaryWeaponId=TEXT("cfm_heilonghun");Pet=GetWorld()->SpawnActorDeferred<AHeliosHumanoidPet>(AHeliosHumanoidPet::StaticClass(),FTransform(FRotator(0,90,0),Player->GetActorLocation()+FVector(150,0,0)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Pet->CompanionOwner=Player;Check(TEXT("companion_loadout"),Pet->ApplyLoadout(L));UGameplayStatics::FinishSpawningActor(Pet,Pet->GetActorTransform());}Camera=GetWorld()->SpawnActor<ACameraActor>();Camera->SetActorLocation(Pet->GetActorLocation()+FVector(500,-300,100));Camera->SetActorRotation((Pet->GetActorLocation()-Camera->GetActorLocation()).Rotation());Camera->GetCameraComponent()->FieldOfView=55;PC->SetViewTarget(Camera);Advance(12);return;
 case 12:if(FParse::Param(FCommandLine::Get(),TEXT("HeliosBlackDragonSoulMotionQA"))){StartMotionCase();Stage=122;Next=Wall;return;}if(Age<8)return;Check(TEXT("companion_holds_and_fires"),Pet&&Pet->GetCharacterPresentation()->IsReady()&&Pet->ShotsFired>0&&Cast<AHeliosBlackDragonSoulWeapon>(Pet->GetEquippedWeapon()));Check(TEXT("companion_blast_hits"),UHeliosBlackDragonSoulState::Get(Pet)->Explosions>0&&Target->CurrentHP<100000);Capture(TEXT("07_companion"));Advance(121);return;
 case 122:if(TickMotion(Dt))Advance(121);return;
 case 121:PC->SetViewTarget(Player);PC->SetControlRotation(FRotator(89,90,0));if(W&&W->ReloadRemaining()<=0){Player->StartFireFromAutomation();Player->StopFireFromAutomation();}Player->ResetForTest();{auto* State=UHeliosBlackDragonSoulState::Get(Player,false);Check(TEXT("new_run_clears_chamber_and_world_effects"),State&&State->Ammo==1&&State->ReloadEnd<0&&State->WorldActors.IsEmpty());}Finish();return;
 default:return;
 }
}

void AHeliosBlackDragonSoulQA::StartMotionCase()
{
 if(IsValid(Pet))Pet->Destroy();
 SpawnTarget(1500,true);
 const FName Characters[]={TEXT("cfm_bulianshi"),TEXT("cfm_chan"),TEXT("cfm_guanxiaoyu")};
 const FName Forms[]={TEXT("SP"),NAME_None,TEXT("GR")};
 auto L=Player->GetLoadout();L.CharacterId=Characters[MotionCase];L.CharacterVariantId=Forms[MotionCase];L.PrimaryWeaponId=TEXT("cfm_heilonghun");
 const FTransform Transform(FRotator(0,90,0),Player->GetActorLocation()+FVector(150,0,0));
 Pet=GetWorld()->SpawnActorDeferred<AHeliosHumanoidPet>(AHeliosHumanoidPet::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 Pet->CompanionOwner=Player;Check(*FString::Printf(TEXT("motion_%d_loadout"),MotionCase),Pet->ApplyLoadout(L));
 UGameplayStatics::FinishSpawningActor(Pet,Transform);Pet->Target=Target;
 PrimaryActorTick.TickGroup=TG_PostUpdateWork;AddTickPrerequisiteComponent(Pet->GetCharacterPresentation());
 Player->GetCharacterPresentation()->SetVisible(false);
 Camera->SetActorLocation(Pet->GetActorLocation()+FVector(440,-270,75));
 Camera->SetActorRotation((Pet->GetActorLocation()+FVector(0,0,25)-Camera->GetActorLocation()).Rotation());
 Camera->GetCameraComponent()->FieldOfView=50;
 MotionAge=MotionCaptureAge=MotionPhaseAge=0;MotionPhase=0;MotionFrames=MotionImages=0;MotionLastCommit=Pet->GetCharacterPresentation()->GetPoseCommitCount();
 MotionSingleCommit=MotionSourcesHidden=MotionGunFollows=true;MotionMoved=false;MotionSamples.Reset();
 IFileManager::Get().MakeDirectory(*(Out/FString::Printf(TEXT("motion_%d"),MotionCase)),true);
}

bool AHeliosBlackDragonSoulQA::TickMotion(float Dt)
{
 MotionAge+=Dt;MotionPhaseAge+=Dt;MotionCaptureAge+=Dt;
 auto* Presentation=Pet->GetCharacterPresentation();auto* Body=Presentation->GetArmsMesh();auto* Gun=Presentation->GetWeaponMesh();
 auto* Weapon=Cast<AHeliosBlackDragonSoulWeapon>(Pet->GetEquippedWeapon());
 auto* Source=Weapon?Cast<UHeliosHandAnimInstance>(Weapon->GetFirstPersonMesh()->GetAnimInstance()):nullptr;
 if(!Body||!Gun||!Weapon||!Source){Check(TEXT("motion_components"),false);return true;}
 const FString Clip=Source->Sequence?Source->Sequence->GetName():TEXT("None");
 const uint32 Count=Presentation->GetPoseCommitCount(),Commits=Count-MotionLastCommit;
 if(MotionFrames>0)MotionSingleCommit&=(Commits==1)&&!Body->IsComponentTickEnabled();MotionLastCommit=Count;
 MotionSourcesHidden&=Weapon->GetFirstPersonMesh()->bHiddenInGame&&Weapon->GetThirdPersonMesh()->bHiddenInGame;
 const float GunError=FVector::Distance(Body->GetSocketLocation(TEXT("b_RightWeapon")),Gun->GetSocketLocation(TEXT("b_RightWeapon")));
 MotionGunFollows&=Gun->LeaderPoseComponent.Get()==Body&&GunError<.1f;
 auto Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("seconds"),MotionAge);Sample->SetNumberField(TEXT("dt"),Dt);Sample->SetStringField(TEXT("source"),Clip);Sample->SetNumberField(TEXT("source_seconds"),Source->SampleSeconds);Sample->SetNumberField(TEXT("pose_commits"),Commits);Sample->SetNumberField(TEXT("gun_error_cm"),GunError);Sample->SetNumberField(TEXT("speed"),Pet->GetVelocity().Size2D());
 auto Bones=MakeShared<FJsonObject>();
 for(const TCHAR* Name:{TEXT("b_LeftArm"),TEXT("b_LeftForeArm"),TEXT("b_LeftHand"),TEXT("b_RightArm"),TEXT("b_RightForeArm"),TEXT("b_RightHand")})
 {
  const FTransform T=Body->GetSocketTransform(Name,RTS_Component);const FVector P=T.GetLocation();const FQuat Q=T.GetRotation();
  TArray<TSharedPtr<FJsonValue>> V;for(double N:{P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W})V.Add(MakeShared<FJsonValueNumber>(N));Bones->SetArrayField(Name,V);
 }
 Sample->SetObjectField(TEXT("bones"),Bones);MotionSamples.Add(MakeShared<FJsonValueObject>(Sample));++MotionFrames;
 // Short consecutive GPU readbacks at normal temporal AA / motion blur settings.
 const bool CaptureAction=(Pet->ShotsFired<=2||MotionPhase==2)&&((Clip.Contains(TEXT("Fire"))&&Source->SampleSeconds<.65f)||(Clip.Contains(TEXT("ChangeClip"))&&Source->SampleSeconds<.9f));
 if(MotionCaptureAge>=1.f/30&&(CaptureAction||MotionPhase==1))
 {MotionCaptureAge=0;const FString Path=Out/FString::Printf(TEXT("motion_%d/frame_%04d.jpg"),MotionCase,MotionImages++);Sample->SetStringField(TEXT("image"),Path);HeliosAsyncFrameCapture::Request(Path);}
 if(MotionPhase==0&&Pet->Reloads>=2)
 {Target->SetActorLocation(Pet->GetActorLocation()+FVector(0,2500,0));MotionPhase=1;MotionPhaseAge=0;}
 else if(MotionPhase==1)
 {MotionMoved|=Pet->GetVelocity().Size2D()>20;if(MotionPhaseAge>1.2f){Target->SetActorLocation(Pet->GetActorLocation()+FVector(0,1500,0));MotionShotsBefore=Pet->ShotsFired;MotionPhase=2;MotionPhaseAge=0;}}
 else if(MotionPhase==2&&Pet->ShotsFired>=3&&Pet->ShotsFired>MotionShotsBefore&&Pet->Reloads>=Pet->ShotsFired)
 {
  auto* State=UHeliosBlackDragonSoulState::Get(Pet);
  Check(*FString::Printf(TEXT("motion_%d_two_cycles_and_moving_then_shoot"),MotionCase),MotionMoved&&Pet->ShotsFired>=3&&Pet->Reloads>=3);
  Check(*FString::Printf(TEXT("motion_%d_single_pose_commit"),MotionCase),MotionSingleCommit);
  Check(*FString::Printf(TEXT("motion_%d_source_arms_hidden"),MotionCase),MotionSourcesHidden);
  Check(*FString::Printf(TEXT("motion_%d_gun_follows_body"),MotionCase),MotionGunFollows);
  Check(*FString::Printf(TEXT("motion_%d_cannon_explodes_and_hits"),MotionCase),State->Explosions>=3&&Target->CurrentHP<100000);
  auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("character"),Pet->GetLoadout().CharacterId.ToString());R->SetStringField(TEXT("form"),Pet->GetLoadout().CharacterVariantId.ToString());R->SetNumberField(TEXT("shots"),Pet->ShotsFired);R->SetNumberField(TEXT("reloads"),Pet->Reloads);R->SetArrayField(TEXT("frames"),MotionSamples);
  for(const TCHAR* CVar:{TEXT("r.AntiAliasingMethod"),TEXT("r.MotionBlurQuality"),TEXT("r.DefaultFeature.MotionBlur")})if(auto* V=IConsoleManager::Get().FindConsoleVariable(CVar))R->SetNumberField(CVar,V->GetInt());
  FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*(Out/FString::Printf(TEXT("motion_%d/poses.json"),MotionCase)));
  if(++MotionCase>=3)return true;StartMotionCase();
 }
 return false;
}
