#include "Weapons/HeliosYunSummerMegatron.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosHandAnimInstance.h"
#include "Weapons/HeliosSSXVoice.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Pets/HeliosYunSummerMegatronPet.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterPresentationComponent.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioNPC.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"

namespace
{
const TCHAR* Root=TEXT("/Game/Helios/CFMYunSummerMegatron/");
FString SelectionPath(){return FPaths::ProjectSavedDir()/TEXT("SaveGames/HeliosYunSummer.v1.json");}
bool ValidForm(EHeliosYunSummerForm Form){return uint8(Form)<=uint8(EHeliosYunSummerForm::SP);}
void IgnoreFriendlies(UWorld* World,FCollisionQueryParams& Query,AActor* Source,APawn* Instigator)
{
    Query.AddIgnoredActor(Source);Query.AddIgnoredActor(Instigator);
    if(auto* Roster=World->GetSubsystem<UHeliosPetWorld>())Roster->IgnoreAll(Query);
    auto* BioMode=AHeliosBioHotelGameMode::Find(World);
    const int32 SourceId=BioMode?BioMode->ParticipantId(Instigator?static_cast<AActor*>(Instigator):Source):INDEX_NONE;
    for(TActorIterator<APawn> It(World);It;++It)
    {
        const int32 TargetId=BioMode?BioMode->ParticipantId(*It):INDEX_NONE;
        if(SourceId!=INDEX_NONE&&TargetId!=INDEX_NONE)
        {
            // Bio NPCs share the native third-person weapon tag with companions.
            // The current round's teams decide whether a participant blocks a shot.
            if(!BioMode->AreOpponents(SourceId,TargetId))Query.AddIgnoredActor(*It);
            continue;
        }
        if(It->IsPlayerControlled()||It->ActorHasTag(TEXT("Friendly"))||It->ActorHasTag(TEXT("HeliosPet")))Query.AddIgnoredActor(*It);
    }
}
}

namespace HeliosYunSummer
{
const TCHAR* FormCode(EHeliosYunSummerForm Form)
{return Form==EHeliosYunSummerForm::GR?TEXT("GR"):Form==EHeliosYunSummerForm::BL?TEXT("BL"):TEXT("SP");}
const TCHAR* FormLabel(EHeliosYunSummerForm Form)
{return Form==EHeliosYunSummerForm::GR?TEXT("保卫者"):Form==EHeliosYunSummerForm::BL?TEXT("潜伏者"):TEXT("灵魂武者");}
FString PreviewPath(EHeliosYunSummerForm Form)
{const FString Name=FString::Printf(TEXT("T_YUN_%s_Preview"),FormCode(Form));return FString(Root)+TEXT("Textures/")+Name+TEXT(".")+Name;}
FString IconPath(EHeliosYunSummerForm Form)
{const FString Name=FString::Printf(TEXT("T_YUN_%s_Icon"),FormCode(Form));return FString(Root)+TEXT("Textures/")+Name+TEXT(".")+Name;}
float Setting(bool Pet,const TCHAR* Key,float Default,float Min,float Max)
{
    const FString Path=FPaths::ProjectContentDir()/TEXT("Settings/战斗数值/云悠悠威震天.ini");
    FString Text,Value;FConfigFile File;double Number=Default;
    if(FFileHelper::LoadFileToString(Text,*Path))File.ProcessInputFileContents(Text,Path);
    if(File.GetString(Pet?TEXT("伙伴"):TEXT("玩家"),Key,Value))
    {double Parsed=0;if(LexTryParseString(Parsed,*Value)&&FMath::IsFinite(Parsed))Number=Parsed;}
    return FMath::Clamp(float(Number),Min,Max);
}
FHeliosMegatronTuning Tuning(bool Pet)
{
    FHeliosMegatronTuning S;
    S.Damage=Setting(Pet,TEXT("爆炸中心伤害"),Pet?240:720,0,1000000);
    S.MinimumDamage=Setting(Pet,TEXT("爆炸外缘伤害"),Pet?80:240,0,S.Damage);
    S.Radius=Setting(Pet,TEXT("爆炸半径"),450,10,5000);S.Speed=Setting(Pet,TEXT("榴弹飞行速度"),7000,100,100000);
    S.Range=Setting(Pet,TEXT("射程"),12000,100,50000);S.Interval=Setting(Pet,TEXT("射击间隔"),.5666667f,.1f,10);
    S.Reload=Setting(Pet,TEXT("换弹时间"),Pet?2.933333f:2.f,.3f,20);S.Magazine=FMath::RoundToInt(Setting(Pet,TEXT("弹匣容量"),20,1,100));
    return S;
}
UAnimSequence* Animation(const FString& Prefix,FName Clip)
{const FString Name=Prefix+Clip.ToString();return LoadObject<UAnimSequence>(nullptr,*(FString(Root)+TEXT("Animations/")+Name+TEXT(".")+Name));}
USoundBase* Sound(int32 Id)
{const FString Name=FString::Printf(TEXT("SFX_MEGATRON_%d"),Id);return LoadObject<USoundBase>(nullptr,*(FString(Root)+TEXT("Audio/")+Name+TEXT(".")+Name));}
void InitializeVoice(UHeliosSSXVoiceComponent* Voice,EHeliosYunSummerForm Form,bool Spatial)
{
    if(!Voice)return;
    TArray<USoundBase*> Pool;TMap<FName,USoundBase*> Cues;
    auto Load=[&](int32 Id){const FString Name=FString::Printf(TEXT("VO_YUN_%s_%d"),FormCode(Form),Id);return LoadObject<USoundBase>(nullptr,*(FString(Root)+TEXT("Audio/")+Name));};
    const int32 Begin=Form==EHeliosYunSummerForm::GR?41247:Form==EHeliosYunSummerForm::BL?41203:41312;
    const int32 End=Form==EHeliosYunSummerForm::SP?41318:Begin+10;
    for(int32 Id=Begin;Id<=End;++Id)if(auto* S=Load(Id))Pool.Add(S);
    const int32 Hurt=Form==EHeliosYunSummerForm::GR?41245:Form==EHeliosYunSummerForm::BL?41201:41310;
    const int32 Death=Form==EHeliosYunSummerForm::GR?41243:Form==EHeliosYunSummerForm::BL?41199:41308;
    Cues.Add(TEXT("Hurt"),Load(Hurt+(Spatial?1:0)));Cues.Add(TEXT("Death"),Load(Death+(Spatial?1:0)));
    Voice->Stop();Voice->InitializeSources(Spatial,Cues,Pool);
}
AHeliosSSXEffect* Effect(AActor* Owner,const TCHAR* Name,const FVector& At,const FRotator& Rotation,bool FP,float Rate)
{return AHeliosSSXEffect::Spawn(Owner,Name,At,Rotation,FP,Rate,FString(Root)+TEXT("Effects/DA_MEGATRON_Effects.DA_MEGATRON_Effects"));}
AHeliosSSXEffect* AttachedEffect(AActor* Owner,USceneComponent* Parent,const TCHAR* Name,bool FP,FName Socket,float Rate)
{
    if(!Parent)return nullptr;
    auto* E=Effect(Owner,Name,Parent->GetSocketLocation(Socket),Parent->GetSocketRotation(Socket),FP,Rate);
    if(E){E->AttachToComponent(Parent,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Socket);E->SetActorRelativeRotation(FRotator(0,90,0));E->SetActorRelativeScale3D(FVector(.01f));E->ResetBirthAnchors();E->AddTickPrerequisiteComponent(Parent);E->SetTickGroup(TG_PostUpdateWork);}
    return E;
}
bool IsFriendlyExplosion(const FDamageEvent& Event,AController* Instigator,AActor* Causer)
{
    if(!Event.IsOfType(FRadialDamageEvent::ClassID))return false;
    const APawn* Pawn=nullptr;
    if(Instigator)Pawn=Instigator->GetPawn();else if(Causer)Pawn=Causer->GetInstigator();
    return (Pawn&&(Pawn->IsPlayerControlled()||Pawn->ActorHasTag(TEXT("Friendly"))||Pawn->ActorHasTag(TEXT("HeliosPet"))))
        ||(Causer&&(Causer->ActorHasTag(TEXT("Friendly"))||Causer->ActorHasTag(TEXT("HeliosPet"))));
}
AHeliosMegatronGrenade* Launch(AActor* Source,APawn* Instigator,const FVector& View,const FVector& Direction,const FVector& Muzzle,const FHeliosMegatronTuning& Tuning,bool FirstPerson)
{
    if(!Source||!Source->GetWorld())return nullptr;
    auto* World=Source->GetWorld();FCollisionQueryParams Q(SCENE_QUERY_STAT(MegatronMuzzle),true);IgnoreFriendlies(World,Q,Source,Instigator);
    const FVector Dir=Direction.GetSafeNormal();FHitResult ViewHit,Obstruction;
    World->LineTraceSingleByChannel(ViewHit,View,View+Dir*Tuning.Range,ECC_Visibility,Q);
    const bool Blocked=World->SweepSingleByChannel(Obstruction,View,Muzzle,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(5),Q);
    const FVector SpawnAt=Blocked?Obstruction.Location:Muzzle;
    const FVector AimAt=ViewHit.bBlockingHit?ViewHit.ImpactPoint+Dir*4:View+Dir*Tuning.Range;
    FActorSpawnParameters P;P.Instigator=Instigator;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Grenade=World->SpawnActor<AHeliosMegatronGrenade>(SpawnAt,(AimAt-SpawnAt).Rotation(),P);
    if(Grenade){Grenade->Initialize(Source,Instigator,(AimAt-SpawnAt).GetSafeNormal(),Tuning,FirstPerson);if(Blocked)Grenade->Explode(Obstruction.ImpactPoint,Obstruction.ImpactNormal);}
    return Grenade;
}
}

void UHeliosYunSummerSession::Initialize(FSubsystemCollectionBase& Collection)
{Super::Initialize(Collection);LoadSelection(SelectionPath());}
EHeliosYunSummerForm UHeliosYunSummerSession::GetPetForm(int32 Slot) const
{return PetForms.IsValidIndex(Slot)?PetForms[Slot]:EHeliosYunSummerForm::SP;}
void UHeliosYunSummerSession::SetPlayerForm(EHeliosYunSummerForm Form)
{
    if(!ValidForm(Form)||PlayerForm==Form)return;PlayerForm=Form;SaveSelection();
    if(auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(),0)))
        if(auto* W=Cast<AHeliosYunSummerMegatronWeapon>(Player->GetEquippedWeapon()))W->SetForm(Form);
}
void UHeliosYunSummerSession::SetPetForm(int32 Slot,EHeliosYunSummerForm Form)
{if(!PetForms.IsValidIndex(Slot)||!ValidForm(Form)||PetForms[Slot]==Form)return;PetForms[Slot]=Form;SaveSelection();}
void UHeliosYunSummerSession::SaveSelection() const {SaveSelection(SelectionPath());}
bool UHeliosYunSummerSession::SaveSelection(const FString& Path) const
{
    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("version"),1);J->SetNumberField(TEXT("player"),uint8(PlayerForm));
    TArray<TSharedPtr<FJsonValue>> Slots;for(auto F:PetForms)Slots.Add(MakeShared<FJsonValueNumber>(uint8(F)));J->SetArrayField(TEXT("pets"),Slots);
    FString Text;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    const FString Temp=Path+TEXT(".tmp");return FFileHelper::SaveStringToFile(Text,*Temp)&&IFileManager::Get().Move(*Path,*Temp,true,true);
}
bool UHeliosYunSummerSession::LoadSelection(const FString& Path)
{
    FString Text;TSharedPtr<FJsonObject> J;if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J.IsValid())return false;
    double P=2;const TArray<TSharedPtr<FJsonValue>>* Slots=nullptr;
    if(!J->TryGetNumberField(TEXT("player"),P)||!FMath::IsFinite(P)||P!=FMath::FloorToDouble(P)||P<0||P>2||!J->TryGetArrayField(TEXT("pets"),Slots)||Slots->Num()!=4)return false;
    TArray<EHeliosYunSummerForm> Parsed;
    for(auto& V:*Slots){double F;if(!V->TryGetNumber(F)||!FMath::IsFinite(F)||F!=FMath::FloorToDouble(F)||F<0||F>2)return false;Parsed.Add(EHeliosYunSummerForm(uint8(F)));}
    PlayerForm=EHeliosYunSummerForm(uint8(P));PetForms=MoveTemp(Parsed);return true;
}

AHeliosMegatronGrenade::AHeliosMegatronGrenade()
{PrimaryActorTick.bCanEverTick=true;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Grenade flight")));SetActorEnableCollision(false);}
void AHeliosMegatronGrenade::Initialize(AActor* Source,APawn* InInstigator,const FVector& Direction,const FHeliosMegatronTuning& Tuning,bool FirstPerson)
{
    DamageSource=Source;DamageInstigator=InInstigator;SetInstigator(InInstigator);Settings=Tuning;Velocity=Direction.GetSafeNormal()*Settings.Speed;
    if(auto* BioMode=AHeliosBioHotelGameMode::Find(GetWorld()))
    {
        const int32 Id=BioMode->ParticipantId(InInstigator?static_cast<AActor*>(InInstigator):Source);
        if(const auto* Participant=BioMode->Model.FindParticipant(Id))
        {BioLaunchRound=BioMode->RoundSerial;BioLaunchParticipant=Id;BioLaunchTeam=int32(Participant->Team);}
    }
    Tags.AddUnique(TEXT("Friendly"));
    if(Source&&Source->ActorHasTag(TEXT("HeliosPet")))Tags.AddUnique(TEXT("HeliosPet"));
    if(Source&&Source->ActorHasTag(TEXT("HeliosSummon")))Tags.AddUnique(TEXT("HeliosSummon"));
    FlightEffect=HeliosYunSummer::Effect(this,TEXT("FX_1P_MG36_WeiZhenTian_Aoe_Fly"),GetActorLocation(),Velocity.Rotation(),false);
    if(FlightEffect){FlightEffect->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);FlightEffect->FollowTravelingEmitter();FlightEffect->MatchTravelLifetime(Settings.Range/Settings.Speed);}
    SetLifeSpan(FMath::Max(2.f,Settings.Range/Settings.Speed+1.f));
}
bool AHeliosMegatronGrenade::IsBioLaunchContextCurrent() const
{
    if(BioLaunchParticipant==INDEX_NONE)return true;
    const auto* BioMode=AHeliosBioHotelGameMode::Find(GetWorld());
    const auto* Participant=BioMode?BioMode->Model.FindParticipant(BioLaunchParticipant):nullptr;
    // A weapon's old in-flight shot cannot acquire a new team or a new round.
    return BioMode&&BioMode->RoundSerial==BioLaunchRound&&Participant&&Participant->bAlive&&
        int32(Participant->Team)==BioLaunchTeam&&BioMode->Model.GetPhase()!=EHeliosBioPhase::RoundOver;
}
void AHeliosMegatronGrenade::Tick(float Dt)
{
    Super::Tick(Dt);if(bExploded||!GetWorld()||GetWorld()->IsPaused())return;
    if(!IsBioLaunchContextCurrent()){Destroy();return;}
    Age+=Dt;const FVector Start=GetActorLocation(),End=Start+Velocity*Dt;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(MegatronFlight),true,this);IgnoreFriendlies(GetWorld(),Q,DamageSource.Get(),DamageInstigator.Get());
    FHitResult H;const bool Hit=GetWorld()->SweepSingleByChannel(H,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(5),Q);
    if(Hit){Explode(H.ImpactPoint,H.ImpactNormal);return;}SetActorLocation(End);Travel+=Velocity.Size()*Dt;
    if(Travel>=Settings.Range)Explode(End,-Velocity.GetSafeNormal());
}
void AHeliosMegatronGrenade::Explode(const FVector& At,const FVector& Normal)
{
    if(bExploded||!GetWorld())return;bExploded=true;
    if(!IsBioLaunchContextCurrent()){Destroy();return;}
    if(IsValid(FlightEffect))FlightEffect->Destroy();FlightEffect=nullptr;
    HeliosYunSummer::Effect(this,TEXT("FX_1P_MG36_WeiZhenTian_Aoe_Boom"),At,Normal.Rotation(),false);
    if(auto* Sound=HeliosYunSummer::Sound(22545))HeliosAudio::PlaySoundAtLocation(this,Sound,At,.7f);
    FCollisionQueryParams SurfaceQuery(SCENE_QUERY_STAT(MegatronImpactMark),true,this);
    IgnoreFriendlies(GetWorld(),SurfaceQuery,DamageSource.Get(),DamageInstigator.Get());
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)SurfaceQuery.AddIgnoredActor(*It);
    const FVector SurfaceNormal=Normal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);FHitResult SurfaceHit;
    if(GetWorld()->LineTraceSingleByChannel(SurfaceHit,At+SurfaceNormal*8,At-SurfaceNormal*8,ECC_Visibility,SurfaceQuery))
        HeliosYunSummer::Effect(this,TEXT("FX_SM_WeiZhenTian_Plane_Inst"),SurfaceHit.ImpactPoint+SurfaceHit.ImpactNormal*.1f,FRotationMatrix::MakeFromZ(SurfaceHit.ImpactNormal).Rotator(),false);
    // Only enemy actors are eligible; generic damage avoids point-hit head multipliers.
    FCollisionQueryParams Sight(SCENE_QUERY_STAT(MegatronExplosionSight),true,this);
    IgnoreFriendlies(GetWorld(),Sight,DamageSource.Get(),DamageInstigator.Get());
    // Enemy bodies do not shield a crowd from the blast; scene geometry still does.
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)Sight.AddIgnoredActor(*It);
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        auto* Enemy=*It;if(Enemy->IsEnemyDead())continue;
        auto* BioMode=AHeliosBioHotelGameMode::Find(GetWorld());
        const int32 SourceId=BioMode?BioMode->ParticipantId(DamageInstigator.IsValid()?static_cast<AActor*>(DamageInstigator.Get()):DamageSource.Get()):INDEX_NONE;
        const int32 VictimId=BioMode?BioMode->ParticipantId(Enemy):INDEX_NONE;
        if(SourceId!=INDEX_NONE&&VictimId!=INDEX_NONE)
        {if(!BioMode->AreOpponents(SourceId,VictimId))continue;}
        else if(Enemy->ActorHasTag(TEXT("Friendly")))continue;
        FVector Closest=Enemy->GetActorLocation();float Distance=FVector::Distance(At,Closest);
        if(auto* Primitive=Cast<UPrimitiveComponent>(Enemy->GetRootComponent())){const float Surface=Primitive->GetClosestPointOnCollision(At,Closest);if(Surface>=0)Distance=Surface;}
        if(Distance>Settings.Radius)continue;
        const FVector Center=Enemy->GetActorLocation();
        FHitResult H;const FVector Origin=At+Normal*6.f;
        if(GetWorld()->LineTraceSingleByChannel(H,Origin,Center,ECC_Visibility,Sight))continue;
        const float Damage=FMath::Lerp(Settings.Damage,Settings.MinimumDamage,FMath::Clamp(Distance/Settings.Radius,0.f,1.f));
        const float Before=Enemy->CurrentHP;
        const float Applied=UGameplayStatics::ApplyDamage(Enemy,Damage,DamageInstigator.IsValid()?DamageInstigator->GetController():nullptr,DamageSource.IsValid()?DamageSource.Get():this,UDamageType::StaticClass());
        if(Applied>0&&Enemy->CurrentHP<Before)
        {
            ++Hits;
            if(Enemy->IsEnemyDead())
            {
                ++Kills;
                if(auto* W=Cast<AHeliosYunSummerMegatronWeapon>(DamageSource.Get()))W->RecordKillEffect(Center);
                if(auto* P=Cast<AHeliosYunSummerMegatronPet>(DamageSource.Get()))P->RecordKillEffect(Center);
            }
        }
    }
    if(auto* W=Cast<AHeliosYunSummerMegatronWeapon>(DamageSource.Get()))W->RecordImpact(Hits,Kills);
    if(auto* P=Cast<AHeliosYunSummerMegatronPet>(DamageSource.Get()))P->RecordImpact(Hits,Kills);
    UE_LOG(LogTemp,Display,TEXT("YUN_MEGATRON_IMPACT hits=%d kills=%d at=%s"),Hits,Kills,*At.ToCompactString());SetLifeSpan(12.f);
}
void AHeliosMegatronGrenade::EndPlay(const EEndPlayReason::Type Reason)
{if(IsValid(FlightEffect))FlightEffect->Destroy();Super::EndPlay(Reason);}

UHeliosYunSummerMegatronDefinition::UHeliosYunSummerMegatronDefinition()
{
    WeaponId=TEXT("cfm_yun_summer_megatron");DisplayName=FText::FromString(TEXT("云悠悠·沐夏微醺 / 威震天"));
    Description=FText::FromString(TEXT("威震天挑战榴弹，三阵营造型。"));WeaponClass=AHeliosYunSummerMegatronWeapon::StaticClass();
    const auto S=HeliosYunSummer::Tuning(false);MagazineSize=S.Magazine;ReloadSeconds=S.Reload;Damage=S.Damage;MinimumDamage=S.MinimumDamage;Range=S.Radius;ProjectileSpeed=S.Speed;RefireSeconds=S.Interval;Recoil=.6f;
    // Source FP arms end above the elbow. Keep that open edge below the view,
    // moving the gun and cup together so authored hand contacts stay aligned.
    AttachmentLocation=FVector(0,0,-6);AttachmentRotation=FRotator(0,-90,0);AttachmentScale=FVector::OneVector;
    SkeletalWeaponMesh=TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FString(Root)+TEXT("SK_YUN_SP_FP.SK_YUN_SP_FP")));
    PreviewTexture=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(HeliosYunSummer::PreviewPath(EHeliosYunSummerForm::SP)));
}
AHeliosYunSummerMegatronWeapon::AHeliosYunSummerMegatronWeapon()
{
    PrimaryActorTick.bCanEverTick=true;SetExternalPresentation(true,TEXT("MuzzleFlashSocket"));bFullAuto=true;
    NativeTPMuzzleSource=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Helios/CFMYunSummerMegatron/SM_MEGATRON_TP.SM_MEGATRON_TP")));
    Megatron=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Original Megatron FP"));Megatron->SetupAttachment(GetFirstPersonMesh());
    Megatron->SetCollisionEnabled(ECollisionEnabled::NoCollision);Megatron->SetOnlyOwnerSee(true);Megatron->SetCastShadow(false);Megatron->SetCanEverAffectNavigation(false);Megatron->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    Cup=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Original YunSummer cup FP"));Cup->SetupAttachment(GetFirstPersonMesh());Cup->SetVisibility(false);Cup->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cup->SetOnlyOwnerSee(true);Cup->SetCanEverAffectNavigation(false);Cup->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    for(auto* M:{GetFirstPersonMesh(),Megatron.Get(),Cup.Get()}){M->SetBoundsScale(4);M->SetCastShadow(false);M->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());M->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;}
    Voice=CreateDefaultSubobject<UHeliosSSXVoiceComponent>(TEXT("Original YunSummer voice"));
}
void AHeliosYunSummerMegatronWeapon::ApplyDefinition(const UHeliosWeaponDefinition* Definition)
{
    Super::ApplyDefinition(Definition);ShotSettings=HeliosYunSummer::Tuning(false);RefireRate=ShotSettings.Interval;
    Megatron->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,*(FString(Root)+TEXT("SK_MEGATRON_FP"))));
    for(FName N:{FName("Idle"),FName("Move"),FName("Equip"),FName("ChangeClip"),FName("Fire"),FName("Show")})GunClips.Add(N,HeliosYunSummer::Animation(TEXT("A_MEGATRON_FP_"),N));
    if(auto* GI=GetGameInstance())if(auto* Session=GI->GetSubsystem<UHeliosYunSummerSession>())Form=Session->GetPlayerForm();
    ApplyForm();FirstPersonModel->SetVisibility(false);ThirdPersonModel->SetVisibility(false);
}
void AHeliosYunSummerMegatronWeapon::ApplyForm()
{
    const FString Code=HeliosYunSummer::FormCode(Form);GetFirstPersonMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,*(FString(Root)+TEXT("SK_YUN_")+Code+TEXT("_FP"))));
    ArmsClips.Reset();for(FName N:{FName("Idle"),FName("Move"),FName("Equip"),FName("ChangeClip"),FName("Fire"),FName("Show"),FName("Cheers")})ArmsClips.Add(N,HeliosYunSummer::Animation(TEXT("A_YUN_")+Code+TEXT("_FP_"),N));
    Cup->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,*(FString(Root)+TEXT("SK_YUN_")+Code+TEXT("_Cup_FP"))));CupCheers=HeliosYunSummer::Animation(TEXT("A_YUN_")+Code+TEXT("_CUP_FP_"),TEXT("Cheers"));
    for(auto* M:{GetFirstPersonMesh(),Megatron.Get(),Cup.Get()}){M->SetAnimInstanceClass(UHeliosHandAnimInstance::StaticClass());M->AddTickPrerequisiteActor(this);M->bEnableUpdateRateOptimizations=false;if(auto* A=Cast<UHeliosHandAnimInstance>(M->GetAnimInstance())){A->bPreserveTransitionVelocity=false;A->BlendDuration=.025f;}}
    HeliosYunSummer::InitializeVoice(Voice,Form,false);ApplyMovementBonus(bActive);Pose();
}
void AHeliosYunSummerMegatronWeapon::ApplyMovementBonus(bool Enabled)
{if(auto* P=Cast<AHeliosArenaCharacter>(PawnOwner))P->SetEquipmentMovementMultiplier(Enabled&&Form==EHeliosYunSummerForm::SP?HeliosYunSummer::Setting(false,TEXT("灵魂武者移速倍率"),1.2f,1,3):1.f);}
void AHeliosYunSummerMegatronWeapon::SetForm(EHeliosYunSummerForm InForm)
{
    if(!ValidForm(InForm))return;
    if(bReload){PendingForm=InForm;bFormPending=true;return;}
    if(Form==InForm)return;
    Super::StopFiring();ClearEffects(false);CancelSounds();Action=NAME_None;Form=InForm;ApplyForm();
    if(auto* GI=GetGameInstance())if(auto* Session=GI->GetSubsystem<UHeliosYunSummerSession>())Session->SetPlayerForm(Form);
    UE_LOG(LogTemp,Display,TEXT("YUN_FORM source=player form=%s ammo=%d"),HeliosYunSummer::FormCode(Form),CurrentBullets);
}
void AHeliosYunSummerMegatronWeapon::ToggleForm()
{const auto Base=bFormPending?PendingForm:Form;SetForm(EHeliosYunSummerForm((uint8(Base)+1)%3));}
void AHeliosYunSummerMegatronWeapon::ActivateWeapon(const FName& Tag)
{
    Super::ActivateWeapon(Tag);bActive=true;bReload=false;bFormPending=false;ShotSettings=HeliosYunSummer::Tuning(false);RefireRate=ShotSettings.Interval;
    if(auto* GI=GetGameInstance())if(auto* Session=GI->GetSubsystem<UHeliosYunSummerSession>())
        if(Session->GetPlayerForm()!=Form){Form=Session->GetPlayerForm();ApplyForm();}
    if(ActiveDefinition)GetFirstPersonMesh()->SetRelativeLocationAndRotation(ActiveDefinition->AttachmentLocation,ActiveDefinition->AttachmentRotation);
    Megatron->SetRelativeTransform(FTransform::Identity);Cup->SetRelativeTransform(FTransform::Identity);ApplyMovementBonus(true);
    for(const TCHAR* Bone:{TEXT("Bone005"),TEXT("Bone038"),TEXT("Bone040"),TEXT("Bone041"),TEXT("Weapon_Root")})
    {
        const FString Recipe=FString::Printf(TEXT("FX_1P_MG36_WeiZhenTian_%s_Idle_FP"),Bone);
        auto* E=HeliosYunSummer::AttachedEffect(this,Megatron,*Recipe,true,FName(Bone));
        if(E){E->FollowTravelingEmitter();Effects.Add(E);}
    }
    BeginAction(TEXT("Equip"),.833333f);
}
void AHeliosYunSummerMegatronWeapon::ClearEffects(bool All)
{Cup->SetVisibility(false);Megatron->SetVisibility(true);for(auto& E:Effects)if(IsValid(E))E->SetActorHiddenInGame(false);for(auto& E:ActionEffects)if(IsValid(E))E->Destroy();ActionEffects.Reset();if(All){for(auto& E:Effects)if(IsValid(E))E->Destroy();Effects.Reset();for(auto& E:KillEffects)if(IsValid(E))E->Destroy();KillEffects.Reset();}}
void AHeliosYunSummerMegatronWeapon::DeactivateWeapon()
{StopFiring();bActive=false;CancelReload();ApplyMovementBonus(false);ClearEffects(true);Voice->Stop();Super::DeactivateWeapon();}
void AHeliosYunSummerMegatronWeapon::EndPlay(const EEndPlayReason::Type Reason)
{ApplyMovementBonus(false);ClearEffects(true);CancelSounds();Voice->Stop();Super::EndPlay(Reason);}
void AHeliosYunSummerMegatronWeapon::StartFiring()
{bTriggerHeld=true;if(!bActive||bReload||Action==TEXT("Equip"))return;if(IsShowing()){Action=NAME_None;ClearEffects(false);CancelSounds();}Super::StartFiring();}
void AHeliosYunSummerMegatronWeapon::StopFiring(){bTriggerHeld=false;Super::StopFiring();}
void AHeliosYunSummerMegatronWeapon::StartSpecialFire()
{if(bActive&&!bReload){Super::StopFiring();BeginAction(TEXT("Show"),5.933333f);}}
void AHeliosYunSummerMegatronWeapon::StartCheers()
{if(bActive&&!bReload){Super::StopFiring();const auto A=ArmsClips.FindRef(TEXT("Cheers"));BeginAction(TEXT("Cheers"),A?A->GetPlayLength():4.f);}}
void AHeliosYunSummerMegatronWeapon::BeginReload(float Seconds)
{
    StopFiring();Super::BeginReload(Seconds);bReload=true;ReloadStartedAt=GetWorld()->GetTimeSeconds();BeginAction(TEXT("ChangeClip"),Seconds);
    UE_LOG(LogTemp,Display,TEXT("YUN_RELOAD_BEGIN duration=%.6f world_seconds=%.6f action_seconds=%.6f ammo=%d"),Seconds,ReloadStartedAt,ActionAge,CurrentBullets);
}
void AHeliosYunSummerMegatronWeapon::ReloadMagazine()
{
    const bool Completed=bReload;const bool ApplyPending=bFormPending;const auto NewForm=PendingForm;
    if(Completed)UE_LOG(LogTemp,Display,TEXT("YUN_RELOAD_COMPLETE duration=%.6f world_seconds=%.6f world_elapsed=%.6f action_seconds=%.6f pending=%d"),ActionDuration,GetWorld()->GetTimeSeconds(),GetWorld()->GetTimeSeconds()-ReloadStartedAt,ActionAge,ApplyPending);
    Super::ReloadMagazine();if(Completed)++Reloads;if(ApplyPending){bFormPending=false;SetForm(NewForm);}
}
void AHeliosYunSummerMegatronWeapon::CancelReload()
{bReload=false;bFormPending=false;Action=NAME_None;CancelSounds();ClearEffects(false);Super::CancelReload();Pose();}
bool AHeliosYunSummerMegatronWeapon::HandleOwnerCombatCue(FName Cue)
{if(Cue==TEXT("FatalHurt"))return true;if(Cue!=TEXT("Hurt")&&Cue!=TEXT("Death"))return false;Voice->Play(Cue);if(Cue==TEXT("Death")){StopFiring();CancelReload();ClearEffects(true);ApplyMovementBonus(false);}return true;}
void AHeliosYunSummerMegatronWeapon::CancelSounds()
{for(auto& C:SoundChannels)if(IsValid(C))C->Stop();SoundChannels.Reset();}
void AHeliosYunSummerMegatronWeapon::PlayActionSound(int32 Id)
{if(!ActionSounds.Contains(Id))ActionSounds.Add(Id,HeliosYunSummer::Sound(Id));if(auto* S=ActionSounds.FindRef(Id).Get())if(auto* C=HeliosAudio::SpawnSound2D(this,S,.75f))SoundChannels.Add(C);}
void AHeliosYunSummerMegatronWeapon::BeginAction(FName Clip,float Duration)
{
    ClearEffects(false);CancelSounds();Action=Clip;ActionAge=0;ActionDuration=Duration;EventMask=0;Pose();
    if(Clip==TEXT("Show"))ActionEffects.Add(HeliosYunSummer::AttachedEffect(this,Megatron,TEXT("FX_1P_MG36_WeiZhenTian_Show"),true));
    if(Clip==TEXT("Show")||Clip==TEXT("Equip")||Clip==TEXT("ChangeClip"))
    {
        const TArray<FName> Bones=Clip==TEXT("Show")?TArray<FName>{TEXT("Bone001"),TEXT("Bone005"),TEXT("Bone016"),TEXT("Bone038"),TEXT("Weapon_Root")}:
            Clip==TEXT("Equip")?TArray<FName>{TEXT("Bone005"),TEXT("Bone038"),TEXT("Weapon_Root")}:
            TArray<FName>{TEXT("Bone001"),TEXT("Bone005"),TEXT("Bone011"),TEXT("Bone038"),TEXT("Weapon_Root")};
        for(FName Bone:Bones)
        {
            const FString Recipe=FString::Printf(TEXT("FX_1P_MG36_WeiZhenTian_%s_%s"),*Bone.ToString(),*Clip.ToString());
            ActionEffects.Add(HeliosYunSummer::AttachedEffect(this,Megatron,*Recipe,true,Bone,Clip==TEXT("ChangeClip")?2.f/FMath::Max(.01f,Duration):1.f));
        }
    }
    if(Clip==TEXT("Cheers"))
    {
        Cup->SetVisibility(true);Megatron->SetVisibility(false);
        for(auto& E:Effects)if(IsValid(E))E->SetActorHiddenInGame(true);
        const TCHAR* Family=Form==EHeliosYunSummerForm::GR?TEXT("FamilyP"):Form==EHeliosYunSummerForm::BL?TEXT("FamilyB"):TEXT("FamilySP");
        const FString Recipe=FString::Printf(TEXT("FX_1P_%s_YunYouYou_JiuBei_FP"),Family);
        ActionEffects.Add(HeliosYunSummer::AttachedEffect(this,Cup,*Recipe,true,TEXT("B_Jiu")));
    }
}
void AHeliosYunSummerMegatronWeapon::Pose()
{
    const FName Clip=Action.IsNone()?(PawnOwner&&PawnOwner->GetVelocity().Size2D()>20?FName("Move"):FName("Idle")):Action;
    const float Time=Action.IsNone()?IdleAge:Action==TEXT("ChangeClip")?ActionAge*2.f/FMath::Max(.01f,ActionDuration):ActionAge;
    if(auto* A=Cast<UHeliosHandAnimInstance>(GetFirstPersonMesh()->GetAnimInstance()))A->SetTimelinePose(ArmsClips.FindRef(Clip),Time,Action.IsNone());
    if(auto* A=Cast<UHeliosHandAnimInstance>(Megatron->GetAnimInstance()))A->SetTimelinePose(GunClips.FindRef(Clip),Time,Action.IsNone());
    if(Action==TEXT("Cheers"))if(auto* A=Cast<UHeliosHandAnimInstance>(Cup->GetAnimInstance()))A->SetTimelinePose(CupCheers,Time,false);
}
void AHeliosYunSummerMegatronWeapon::Tick(float Dt)
{
    Super::Tick(Dt);if(!bActive||!GetWorld()||GetWorld()->IsPaused())return;IdleAge+=Dt;ActionAge+=Dt;
    auto Event=[&](uint32 Bit,float At,int32 Id){if(!(EventMask&Bit)&&ActionAge>=At){EventMask|=Bit;PlayActionSound(Id);}};
    if(Action==TEXT("ChangeClip")){const float Rate=ActionDuration/2.f;Event(1,.042f*Rate,22457);Event(2,.94f*Rate,22458);}
    if(Action==TEXT("Equip"))Event(1,.0075f,22459);
    if(Action==TEXT("Show")){Event(1,.0512048f,22460);Event(2,1.3279332f,22461);Event(4,3.6477139f,22462);}
    if(Action==TEXT("Cheers"))Event(1,.0644052f,41071);
    if(!Action.IsNone()&&ActionAge>=ActionDuration&&!bReload){Action=NAME_None;ClearEffects(false);CancelSounds();}
    Effects.RemoveAll([](const auto& E){return !IsValid(E);});KillEffects.RemoveAll([](const auto& E){return !IsValid(E);});SoundChannels.RemoveAll([](const auto& C){return !IsValid(C)||!C->IsPlaying();});Pose();
    if(bTriggerHeld&&!bIsFiring&&!bReload&&Action.IsNone()&&CurrentBullets>0)Super::StartFiring();
}
FString AHeliosYunSummerMegatronWeapon::Status() const
{return FString::Printf(TEXT("%s · %d / %d · 无限备弹%s"),HeliosYunSummer::FormLabel(Form),CurrentBullets,MagazineSize,bReload?TEXT(" · 换弹中"):TEXT(""));}
bool AHeliosYunSummerMegatronWeapon::IsKillEffectActive() const
{for(const auto& E:KillEffects)if(IsValid(E))return true;return false;}
bool AHeliosYunSummerMegatronWeapon::TryGetNativeThirdPersonMuzzle(FVector& Location) const
{
    if(!Cast<AHeliosBioNPC>(PawnOwner))return false;
    const auto* Presentation=PawnOwner->FindComponentByClass<UHeliosCharacterPresentationComponent>();
    auto* Visual=Presentation?Presentation->GetWeaponMesh():nullptr;
    auto* Mesh=Visual?Visual->GetSkeletalMeshAsset():nullptr;
    if(!Presentation||!Presentation->IsReady()||!Visual||!Mesh)return false;
    if(Visual->DoesSocketExist(NativeTPMuzzleSocket))
    {Location=Visual->GetSocketLocation(NativeTPMuzzleSocket);return !Location.ContainsNaN();}
    // The verified SK_TPW skin has no locator socket. Its geometry is the original
    // TP static, baked at 100x into b_RightWeapon (build_pairs.py:192-201).
    // Reproduce that reference point, then apply the same current skin delta.
    // Do not substitute the hand root, the source FP arms or the actor's eyes.
    if(Mesh->GetPathName()!=TEXT("/Game/Helios/LoadoutPresentation/MeshesV4/SK_TPW_cfm_yun_summer_megatron.SK_TPW_cfm_yun_summer_megatron"))return false;
    auto* Source=NativeTPMuzzleSource.LoadSynchronous();
    const auto* Locator=Source?Source->FindSocket(NativeTPMuzzleSocket):nullptr;
    const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
    const int32 Bone=Ref.FindBoneIndex(NativeTPMuzzleBone);
    if(!Locator||Bone==INDEX_NONE||!Visual->DoesSocketExist(NativeTPMuzzleBone))return false;
    FTransform Reference=Ref.GetRefBonePose()[Bone];
    for(int32 Parent=Ref.GetParentIndex(Bone);Parent!=INDEX_NONE;Parent=Ref.GetParentIndex(Parent))Reference=Reference*Ref.GetRefBonePose()[Parent];
    const FVector Scale=Reference.GetScale3D();
    if(Reference.ContainsNaN()||FMath::Abs(Scale.X)<SMALL_NUMBER||FMath::Abs(Scale.Y)<SMALL_NUMBER||FMath::Abs(Scale.Z)<SMALL_NUMBER)return false;
    const FTransform RigidReference(Reference.GetRotation(),Reference.GetTranslation(),FVector::OneVector);
    const FVector RefPoint=RigidReference.TransformPosition(Locator->RelativeLocation*100.f);
    const FVector BoneLocal=Reference.InverseTransformPosition(RefPoint);
    Location=Visual->GetSocketTransform(NativeTPMuzzleBone).TransformPosition(BoneLocal);
    return !Location.ContainsNaN();
}
FVector AHeliosYunSummerMegatronWeapon::MuzzleLocation() const
{
    if(Cast<AHeliosBioNPC>(PawnOwner))
    {FVector Native;return TryGetNativeThirdPersonMuzzle(Native)?Native:FVector::ZeroVector;}
    return Megatron->DoesSocketExist(TEXT("MuzzleFlashSocket"))?Megatron->GetSocketLocation(TEXT("MuzzleFlashSocket")):Megatron->GetComponentTransform().TransformPosition(FVector(55,0,0));
}
void AHeliosYunSummerMegatronWeapon::RecordImpact(int32 Hits,int32 Kills)
{HitsDelivered+=Hits;TrueKills+=Kills;if(bActive)for(int32 I=0;I<Kills;++I)Voice->Play(TEXT("Kill"));}
void AHeliosYunSummerMegatronWeapon::RecordKillEffect(const FVector& At)
{
    auto* Player=Cast<AHeliosArenaCharacter>(PawnOwner);
    if(!bActive||!Player||!Player->IsLocallyControlled()||!Player->FirstPersonCamera)return;
    auto* Camera=Player->FirstPersonCamera.Get();
    auto* E=HeliosYunSummer::Effect(this,TEXT("FX_Transformers_Decepticon_Kill"),Camera->GetComponentLocation(),Camera->GetComponentRotation(),true);
    if(!E)return;
    // Camera binding is the project's presentation adaptation, not a source UI attachment.
    E->AttachToComponent(Camera,FAttachmentTransformRules::KeepRelativeTransform);
    E->SetActorRelativeTransform(FTransform(FRotator(0,90,0),FVector(HeliosYunSummer::Setting(false,TEXT("击杀标识前距"),120,30,500),0,-HeliosYunSummer::Setting(false,TEXT("击杀标识下移"),35,0,200)),FVector(HeliosYunSummer::Setting(false,TEXT("击杀标识倍率"),.25f,.01f,1))));
    E->FollowTravelingEmitter();E->FollowLocalViewTranslation();
    for(auto& Old:KillEffects)if(IsValid(Old))Old->Destroy();KillEffects.Reset();KillEffects.Add(E);++KillEffectsSpawned;
    UE_LOG(LogTemp,Display,TEXT("YUN_KILL_EFFECT source=player count=%d target=%s"),KillEffectsSpawned,*At.ToCompactString());
}
void AHeliosYunSummerMegatronWeapon::FireProjectile(const FVector& Target)
{
    if(!bActive||bReload||IsShowing()||Action==TEXT("Equip")||!PawnOwner||!WeaponOwner||CurrentBullets<=0)return;
    FVector View;FRotator Aim;PawnOwner->GetActorEyesViewPoint(View,Aim);
    const bool NativeTP=Cast<AHeliosBioNPC>(PawnOwner)!=nullptr;FVector Muzzle;
    if(NativeTP&&!TryGetNativeThirdPersonMuzzle(Muzzle))
    {if(!bNativeTPMuzzleWarned){UE_LOG(LogTemp,Warning,TEXT("YUN_NATIVE_TP_MUZZLE_UNAVAILABLE owner=%s"),*GetNameSafe(PawnOwner));bNativeTPMuzzleWarned=true;}return;}
    if(!NativeTP)Muzzle=MuzzleLocation();bNativeTPMuzzleWarned=false;
    if(!HeliosYunSummer::Launch(this,PawnOwner,View,Aim.Vector(),Muzzle,ShotSettings,!NativeTP))return;
    ++ShotsFired;BeginAction(TEXT("Fire"),.5666667f);PlayActionSound(22454);
    Effects.Add(HeliosYunSummer::Effect(this,NativeTP?TEXT("FX_3P_Gunfire_MG36_WeiZhenTian_PVE"):TEXT("FX_1P_Gunfire_MG36_WeiZhenTian_PVE"),Muzzle,Aim,!NativeTP));
    --CurrentBullets;bShotCommitted=true;WeaponOwner->UpdateWeaponHUD(CurrentBullets,MagazineSize);WeaponOwner->AddWeaponRecoil(FiringRecoil);
    UE_LOG(LogTemp,Display,TEXT("YUN_MEGATRON_SHOT source=%s owner=%s shot=%d form=%s ammo=%d muzzle=%s eyes=%s"),NativeTP?TEXT("native_TP"):TEXT("player"),*GetNameSafe(PawnOwner),ShotsFired,HeliosYunSummer::FormCode(Form),CurrentBullets,*Muzzle.ToCompactString(),*View.ToCompactString());
}
