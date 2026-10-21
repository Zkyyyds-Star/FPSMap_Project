#include "Bio/HeliosBioFourSlashComponent.h"
#include "Bio/HeliosBioRoleDefinition.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioClone.h"
#include "Weapons/HeliosSSXEffect.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

UHeliosBioFourSlashComponent::UHeliosBioFourSlashComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
}
bool UHeliosBioFourSlashComponent::ContextIsValid(AHeliosBioHotelGameMode* Mode) const
{
    if(!Mode||!IsValid(GetOwner())||!ActiveMesh.IsValid()||Mode->RoundSerial!=ExpectedRound||Mode->Model.GetPhase()!=EHeliosBioPhase::Active)return false;
    const auto* P=Mode->Model.FindParticipant(ActiveParticipantId);
    return P&&P->bAlive&&P->FrozenSeconds<=0&&P->IsHunter()&&Mode->ParticipantActor(ActiveParticipantId)==GetOwner();
}
bool UHeliosBioFourSlashComponent::BeginSourceSequence(int32 ParticipantId,UHeliosBioRoleDefinition* Role,USkeletalMeshComponent* ViewMesh,bool FP)
{
    if(bBusy||!IsValid(Role)||!Role->bIdentityVerified||!(Role->Ability==EHeliosBioAbility::FourSlash||Role->SourceIdentity.Contains(TEXT("SunShangXiang"),ESearchCase::IgnoreCase))||!IsValid(ViewMesh)||ViewMesh->GetOwner()!=GetOwner())return false;
    const auto& Chain=FP?Role->FPSkillChain:Role->TPSkillChain;
    if(Chain.Num()!=4||StageDamage.Num()!=4||StageHitFraction.Num()!=4||!FMath::IsFinite(PlaybackRate)||PlaybackRate<=0.f||!FMath::IsFinite(MeleeRange)||MeleeRange<=0.f||!FMath::IsFinite(ConeDot))return false;
    TArray<TObjectPtr<UAnimSequence>> Loaded;
    for(const auto& Source:Chain)
    {
        auto* Clip=Source.LoadSynchronous();
        if(!Clip||!FMath::IsFinite(Clip->GetPlayLength())||Clip->GetPlayLength()<=0.f||!ViewMesh->GetSkeletalMeshAsset()||Clip->GetSkeleton()!=ViewMesh->GetSkeletalMeshAsset()->GetSkeleton())return false;
        Loaded.Add(Clip);
    }
    for(int32 Index=0;Index<4;++Index)if(!FMath::IsFinite(StageDamage[Index])||StageDamage[Index]<0.f||!FMath::IsFinite(StageHitFraction[Index]))return false;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());
    ActiveParticipantId=ParticipantId;ExpectedRound=Mode?Mode->RoundSerial:INDEX_NONE;ActiveMesh=ViewMesh;
    if(!ContextIsValid(Mode)){ActiveMesh.Reset();ActiveParticipantId=INDEX_NONE;return false;}
    ActiveRole=Role;Clips=MoveTemp(Loaded);bFirstPerson=FP;ActiveRate=PlaybackRate;
    CompletedStages=HitCallbacks=0;Stage=0;bBusy=true;SetComponentTickEnabled(true);StartStage();
    return true;
}
void UHeliosBioFourSlashComponent::StartStage()
{
    StageAge=0.f;bStageHitDispatched=false;
    auto* Mesh=ActiveMesh.Get();if(!Mesh||!Clips.IsValidIndex(Stage)){Cancel();return;}
    Mesh->PlayAnimation(Clips[Stage],false);
    if(auto* Instance=Mesh->GetSingleNodeInstance())Instance->SetPlayRate(ActiveRate);
    // Existing SSX weapon source playback proves these exact four library keys.
    if(bPlayVerifiedSourceEffects&&ActiveRole&&ActiveRole->SourceIdentity.Contains(TEXT("SunShangXiang"),ESearchCase::IgnoreCase))
    {
        const FString Name=FString::Printf(TEXT("FX_%s_SunShangXiang_Skill%d"),bFirstPerson?TEXT("1P"):TEXT("3P"),Stage+1);
        FRotator Rotation=GetOwner()->GetActorRotation();
        if(auto* Pawn=Cast<APawn>(GetOwner()))Rotation=Pawn->GetBaseAimRotation();
        const FVector Origin=bFirstPerson?Mesh->GetComponentLocation():GetOwner()->GetActorLocation()+FVector(0,0,30);
        if(auto* Effect=AHeliosSSXEffect::Spawn(GetOwner(),Name,Origin,Rotation,bFirstPerson,ActiveRate))
        {
            Effect->AttachToComponent(bFirstPerson?static_cast<USceneComponent*>(Mesh):GetOwner()->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
            Effects.Add(Effect);
        }
        else UE_LOG(LogTemp,Warning,TEXT("BIO_FOUR_SLASH_SOURCE_FX_OMITTED key=%s"),*Name);
    }
    UE_LOG(LogTemp,Display,TEXT("BIO_FOUR_SLASH_STAGE participant=%d stage=%d native_clip=%s duration=%f rate=%f hit_fraction_standalone=%f"),ActiveParticipantId,Stage+1,*Clips[Stage]->GetPathName(),Clips[Stage]->GetPlayLength(),ActiveRate,StageHitFraction[Stage]);
}
void UHeliosBioFourSlashComponent::DispatchStageHit(AHeliosBioHotelGameMode* Mode)
{
    if(bStageHitDispatched||!ContextIsValid(Mode))return;
    bStageHitDispatched=true;++HitCallbacks;
    FVector Origin=GetOwner()->GetActorLocation()+FVector(0,0,30);FRotator Rotation=GetOwner()->GetActorRotation();
    if(auto* Pawn=Cast<APawn>(GetOwner()))
    {
        Rotation=Pawn->GetBaseAimRotation();
        if(bFirstPerson)Pawn->GetActorEyesViewPoint(Origin,Rotation);
    }
    const FVector Forward=Rotation.Vector();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BioFourSlashLOS),false,GetOwner());
    TArray<AActor*> Attached;GetOwner()->GetAttachedActors(Attached,true,true);for(auto* Actor:Attached)Query.AddIgnoredActor(Actor);
    TSet<AActor*> HitActors;
    auto Hit=[&](AActor* Victim,int32 VictimId,AHeliosBioClone* Clone)
    {
        if(!ContextIsValid(Mode)||!IsValid(Victim)||HitActors.Contains(Victim))return;
        const FVector Target=Victim->GetActorLocation()+FVector(0,0,25),Delta=Target-Origin;
        if(Delta.SizeSquared()>FMath::Square(FMath::Max(1.f,MeleeRange))||FVector::DotProduct(Delta.GetSafeNormal(),Forward)<FMath::Clamp(ConeDot,-1.f,1.f))return;
        FHitResult Obstruction;
        if(GetWorld()->LineTraceSingleByChannel(Obstruction,Origin,Target,ECC_Visibility,Query)&&Obstruction.GetActor()!=Victim)return;
        HitActors.Add(Victim);
        if(Clone)Clone->ApplyBioDamage(ActiveParticipantId,StageDamage[Stage]);
        else Mode->DamageParticipant(VictimId,ActiveParticipantId,StageDamage[Stage],true);
    };
    for(const auto& Other:Mode->Model.GetParticipants())if(Other.bAlive&&Mode->AreOpponents(ActiveParticipantId,Other.Id))Hit(Mode->ParticipantActor(Other.Id),Other.Id,nullptr);
    for(auto Clone:Mode->Clones)if(IsValid(Clone)&&Clone->IsActiveClone()&&Mode->AreOpponents(ActiveParticipantId,Clone->GetMotherId()))Hit(Clone,INDEX_NONE,Clone);
    UE_LOG(LogTemp,Display,TEXT("BIO_FOUR_SLASH_HIT_CALLBACK participant=%d stage=%d callback_count=%d targets=%d damage_standalone=%f"),ActiveParticipantId,Stage+1,HitCallbacks,HitActors.Num(),StageDamage[Stage]);
}
void UHeliosBioFourSlashComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,TickType,TickFunction);
    if(!bBusy)return;
    auto* Mode=AHeliosBioHotelGameMode::Find(GetWorld());if(!ContextIsValid(Mode)){Cancel();return;}
    float Remaining=FMath::Max(0.f,Dt)*ActiveRate;
    // A long frame may cross several clips, but every stage resolves at most once.
    while(bBusy&&Remaining>0.f)
    {
        const float Duration=Clips[Stage]->GetPlayLength();
        const float Step=FMath::Min(Remaining,FMath::Max(0.f,Duration-StageAge));
        StageAge+=Step;Remaining-=Step;
        if(!bStageHitDispatched&&StageAge>=Duration*FMath::Clamp(StageHitFraction[Stage],0.f,1.f))DispatchStageHit(Mode);
        if(!ContextIsValid(Mode)){Cancel();return;}
        if(StageAge+KINDA_SMALL_NUMBER<Duration)break;
        ++CompletedStages;
        if(++Stage>=4){Cancel();return;}
        StartStage();
    }
}
void UHeliosBioFourSlashComponent::Cancel()
{
    const bool WasBusy=bBusy;bBusy=false;SetComponentTickEnabled(false);
    if(WasBusy)UE_LOG(LogTemp,Display,TEXT("BIO_FOUR_SLASH_END participant=%d completed_stages=%d hit_callbacks=%d completed=%d"),ActiveParticipantId,CompletedStages,HitCallbacks,CompletedStages==4?1:0);
    if(WasBusy)if(auto* Mesh=ActiveMesh.Get())if(auto* Instance=Mesh->GetSingleNodeInstance())
    {
        // A presentation change may already have selected a death/recovery clip.
        if(Clips.IsValidIndex(Stage)&&Instance->GetCurrentAsset()==Clips[Stage])Instance->SetPlaying(false);
    }
    for(auto Effect:Effects)if(Effect.IsValid())Effect->Destroy();Effects.Reset();
    Clips.Reset();ActiveRole=nullptr;ActiveMesh.Reset();Stage=INDEX_NONE;ActiveParticipantId=INDEX_NONE;ExpectedRound=INDEX_NONE;StageAge=0.f;
}
void UHeliosBioFourSlashComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Cancel();Super::EndPlay(Reason);
}
