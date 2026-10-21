#include "Weapons/HeliosImpactPresentationSubsystem.h"
#include "Audio/HeliosAudio.h"
#include "Weapons/HeliosWeaponVFX.h"
#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

bool UHeliosImpactPresentationSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

TStatId UHeliosImpactPresentationSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(HeliosImpactPresentation, STATGROUP_Tickables);
}

void UHeliosImpactPresentationSubsystem::Preload()
{
    if (bPreloaded) return;
    bPreloaded = true;
    const TCHAR* Names[] = {TEXT("Default"),TEXT("Concrete"),TEXT("Metal"),TEXT("Wood"),TEXT("Soil"),TEXT("Cloth"),TEXT("Armor"),TEXT("Skin")};
    for (const TCHAR* Name : Names)
    {
        const FString Base = TEXT("/Game/Helios/Weapons/Realism/Impacts/");
        Effects.Add(LoadObject<UNiagaraSystem>(nullptr,*(Base+TEXT("NS_Impact_")+Name)));
        Decals.Add(LoadObject<UMaterialInterface>(nullptr,*(Base+TEXT("M_Mark_")+Name)));
        Sounds.Add(LoadObject<USoundBase>(nullptr,*(Base+TEXT("SFX_Impact_")+Name)));
    }
    LaserHotspot = LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Effects/NS_LaserHit"));
    LaserEnemyHit = LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/NS_LaserEnemyHit"));
    Tracer = LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/NS_BulletTracer"));
    ShotgunFlare=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/NS_ShotgunFlare"));
    Attenuation=NewObject<USoundAttenuation>(this);
    Attenuation->Attenuation.bAttenuate=true;
    Attenuation->Attenuation.bSpatialize=true;
    Attenuation->Attenuation.AttenuationShapeExtents=FVector(100.f);
    Attenuation->Attenuation.FalloffDistance=2400.f;
    // Small reusable world-space streaks stay visible for both hits and misses.
    // They follow real projectile positions, independently of the hidden legacy mesh.
    UStaticMesh* Shape=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere"));
    UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/M_BulletStreak"));
    UMaterialInterface* Halo=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/M_BulletHalo"));
    auto* ShotgunCore=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/M_ShotgunCore"));
    auto* ShotgunHalo=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/M_ShotgunHalo"));
    auto* ShotgunTail=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Helios/Weapons/Realism/Impacts/M_ShotgunTail"));
    AActor* FlightActor=nullptr;
    if(Shape&&Material&&Halo)
    {
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
        FlightActor=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,Params);
        if(FlightActor){FlightActor->SetActorEnableCollision(false);FlightActor->SetActorTickEnabled(false);FlightActor->Tags.Add(TEXT("HeliosBallisticTrailPool"));}
    }
    if(FlightActor) for(int32 I=0;I<64;++I)
    {
        auto MakeLayer=[&](UMaterialInterface* Base,TArray<TObjectPtr<UMaterialInstanceDynamic>>& Materials)
        {
            auto* Mesh=NewObject<UStaticMeshComponent>(FlightActor);
            Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(Shape);
            auto* Dynamic=UMaterialInstanceDynamic::Create(Base,Mesh);
            Mesh->SetMaterial(0,Dynamic);Materials.Add(Dynamic);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);
            Mesh->SetCanEverAffectNavigation(false);Mesh->SetCastShadow(false);Mesh->SetReceivesDecals(false);
            Mesh->SetVisibility(false);Mesh->RegisterComponentWithWorld(GetWorld());
            return Mesh;
        };
        FlightMeshes.Add(MakeLayer(Material,FlightMaterials));
        FlightHalos.Add(MakeLayer(Halo,HaloMaterials));
        ShotgunTails.Add(MakeLayer(ShotgunTail?ShotgunTail:Halo,ShotgunTailMaterials));
        ShotgunCoreMaterials.Add(UMaterialInstanceDynamic::Create(ShotgunCore?ShotgunCore:Material,FlightMeshes.Last()));
        ShotgunHaloMaterials.Add(UMaterialInstanceDynamic::Create(ShotgunHalo?ShotgunHalo:Halo,FlightHalos.Last()));
        FlightTrails.AddDefaulted();
    }
}

EHeliosImpactSurface UHeliosImpactPresentationSubsystem::ResolveSurface(const FHitResult& Hit)
{
    const bool bEnemy = Hit.GetActor() && Hit.GetActor()->IsA<APawn>();
    if (const UPhysicalMaterial* Physical = Hit.PhysMaterial.Get())
    {
        const int32 Surface = int32(Physical->SurfaceType.GetValue());
        if (Surface >= 1 && Surface <= 7) return EHeliosImpactSurface(Surface);
    }
    const UPrimitiveComponent* Component = Hit.GetComponent();
    FString Hint = Hit.BoneName.ToString();
    if (Component)
    {
        Hint += Component->GetName();
        if (UMaterialInterface* Material = Component->GetMaterial(FMath::Max(0, int32(Hit.ElementIndex))))
            Hint += Material->GetName();
        for (const FName Tag : Component->ComponentTags) Hint += Tag.ToString();
    }
    if (Hit.GetActor()) for (const FName Tag : Hit.GetActor()->Tags) Hint += Tag.ToString();
    Hint = Hint.ToLower();
    if (Hint.Contains(TEXT("armor"))) return EHeliosImpactSurface::Armor;
    if (bEnemy && (Hint.Contains(TEXT("skin")) || Hint.Contains(TEXT("head")) || Hint.Contains(TEXT("face")))) return EHeliosImpactSurface::Skin;
    if (bEnemy) return EHeliosImpactSurface::Cloth;
    if (Hint.Contains(TEXT("metal")) || Hint.Contains(TEXT("steel")) || Hint.Contains(TEXT("deck"))) return EHeliosImpactSurface::Metal;
    if (Hint.Contains(TEXT("wood")) || Hint.Contains(TEXT("timber"))) return EHeliosImpactSurface::Wood;
    if (Hint.Contains(TEXT("soil")) || Hint.Contains(TEXT("earth")) || Hint.Contains(TEXT("sand")) || Hint.Contains(TEXT("grass"))) return EHeliosImpactSurface::Soil;
    if (Hint.Contains(TEXT("concrete")) || Hint.Contains(TEXT("stone")) || Hint.Contains(TEXT("rock")) || Hint.Contains(TEXT("brick"))) return EHeliosImpactSurface::Concrete;
    return EHeliosImpactSurface::Default;
}

FHitResult UHeliosImpactPresentationSubsystem::RefineCosmeticHit(const FHitResult& Hit) const
{
    if (!Hit.GetActor() || !Hit.GetActor()->IsA<APawn>() || Cast<USkeletalMeshComponent>(Hit.GetComponent())) return Hit;
    // Capsule collision still owns damage. Refine only the attached visual contact.
    TInlineComponentArray<USkeletalMeshComponent*> Meshes(Hit.GetActor());
    const FVector Direction = (Hit.TraceEnd-Hit.TraceStart).GetSafeNormal(UE_SMALL_NUMBER,-FVector(Hit.ImpactNormal));
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HeliosCosmeticContact),false);
    Params.bReturnPhysicalMaterial = true;
    FHitResult Visual;
    for (USkeletalMeshComponent* Mesh : Meshes)
        if (Mesh && Mesh->IsVisible() && Mesh->LineTraceComponent(Visual,Hit.ImpactPoint-Direction*25.f,Hit.ImpactPoint+Direction*125.f,Params)) return Visual;
    return Hit;
}

void UHeliosImpactPresentationSubsystem::Burst(UNiagaraSystem* System,const FHitResult& Hit,float Lifetime,float Scale)
{
    if (!System) return;
    while (Bursts.Num() >= 24)
    {
        if (auto* Old = Bursts[0].Component.Get()) UHeliosWeaponVFX::ReleaseTracked(Old);
        Bursts.RemoveAt(0);
    }
    if (auto* FX = UHeliosWeaponVFX::SpawnOneShot(GetWorld(),System,Hit.ImpactPoint+Hit.ImpactNormal*.7f,
        Hit.ImpactNormal.Rotation(),FVector(Scale),Lifetime))
    {
        if (Hit.GetActor() && Hit.GetActor()->IsA<APawn>() && Hit.GetComponent())
            FX->AttachToComponent(Hit.GetComponent(),FAttachmentTransformRules::KeepWorldTransform,Hit.BoneName);
        Bursts.Add({FX,GetWorld()->GetTimeSeconds()+Lifetime});
    }
}

void UHeliosImpactPresentationSubsystem::ReportImpact(FName WeaponId,uint64 ShotId,const FHitResult& ActualHit,int32 Cluster,bool bCreateMark)
{
    if (!bPreloaded || !ActualHit.bBlockingHit || !ActualHit.GetComponent()) return;
    const float Now = GetWorld()->GetTimeSeconds();
    const uint64 Key = (ShotId << 4) | uint64(FMath::Clamp(Cluster,0,15));
    if (Seen.Contains(Key)) return;
    Seen.Add(Key,Now+2.f);
    FHitResult Hit = RefineCosmeticHit(ActualHit);
    const EHeliosImpactSurface Surface = ResolveSurface(Hit);
    const int32 Index = int32(Surface);
    const bool bEnemy = Hit.GetActor() && Hit.GetActor()->IsA<APawn>();
    const bool bLaser = WeaponId == TEXT("laser");
    if (bLaser)
    {
        const float* Last = LaserLast.Find(Hit.GetActor());
        if (Last && Now-*Last < .15f) return;
        LaserLast.Add(Hit.GetActor(),Now);
    }
    ++PresentedCount;
    if (bLaser && bEnemy)
    {
        // Energy contact is visible on skin as well as clothing and armour; no blood.
        Burst(LaserEnemyHit,Hit,.35f,1.f);
    }
    else
    {
        if (Surface != EHeliosImpactSurface::Skin)
            Burst(Effects[Index],Hit,.65f,WeaponId==TEXT("shotgun") ? 1.05f : .85f);
        if (bLaser) Burst(LaserHotspot,Hit,.23f,.65f);
    }
    if (WeaponId==TEXT("shotgun")) Burst(ShotgunFlare,Hit,.32f,bEnemy?.75f:1.f);
    const uint64 SoundKey = WeaponId==TEXT("shotgun") ? ShotId : uint64(HashCombineFast(GetTypeHash(ShotId),GetTypeHash(Hit.GetActor())));
    if (!SoundSeen.Contains(SoundKey))
    {
        SoundSeen.Add(SoundKey,Now+2.f);
        if (Sounds[Index]) HeliosAudio::PlaySoundAtLocation(GetWorld(),Sounds[Index],Hit.ImpactPoint,.42f,bLaser ? 1.2f : 1.f,0.f,Attenuation);
    }
    if (bCreateMark) SpawnMark(WeaponId,ShotId,Hit,Cluster);
}

void UHeliosImpactPresentationSubsystem::SpawnMark(FName WeaponId,uint64 ShotId,const FHitResult& Hit,int32 PelletIndex)
{
    if (!Hit.bBlockingHit || !IsValid(Hit.GetComponent()) || !IsValid(Hit.GetActor()) || Hit.GetActor()->IsA<APawn>()) return;
    const EHeliosImpactSurface Surface=ResolveSurface(Hit);
    const int32 Index=int32(Surface);
    if (Surface==EHeliosImpactSurface::Skin || Surface==EHeliosImpactSurface::Cloth || !Decals.IsValidIndex(Index) || !Decals[Index]) return;
    while (Marks.Num()>=96)
    {
        if(auto* Old=Marks[0].Component.Get()) Old->DestroyComponent();
        Marks.RemoveAt(0);
    }
    // Receiver opt-in is cosmetic only: no collision, trace or material reassignment.
    Hit.GetComponent()->SetReceivesDecals(true);
    const float Radius=Surface==EHeliosImpactSurface::Soil?5.f:(WeaponId==TEXT("shotgun")?1.8f:3.f);
    FRotator Rotation=(-Hit.ImpactNormal).Rotation();Rotation.Roll=float((ShotId*73+PelletIndex*31)%360);
    if(auto* Mark=UGameplayStatics::SpawnDecalAttached(Decals[Index],FVector(1.2f,Radius,Radius),Hit.GetComponent(),Hit.BoneName,
        Hit.ImpactPoint+Hit.ImpactNormal*.15f,Rotation,EAttachLocation::KeepWorldPosition,0.f))
    {
        Mark->SetFadeScreenSize(.00005f);Mark->SetFadeOut(30.f,3.f,false);
        Marks.Add({Mark,float(GetWorld()->GetTimeSeconds()+33.0)});
    }
}

void UHeliosImpactPresentationSubsystem::ReportPellets(uint64 ShotId,const TArray<FHitResult>& Hits)
{
    if (!bPreloaded) return;
    // Decal identity belongs to each pellet, independently of clustered dust/audio.
    const uint64 MarkKey=(ShotId<<4)|15;
    if (Seen.Contains(MarkKey)) return;
    Seen.Add(MarkKey,GetWorld()->GetTimeSeconds()+2.f);
    for(int32 I=0;I<Hits.Num();++I) SpawnMark(TEXT("shotgun"),ShotId,Hits[I],I);
    TArray<FHitResult> Clusters;
    for (const FHitResult& Hit : Hits)
    {
        if (!Hit.bBlockingHit) continue;
        const bool bNearby = Clusters.ContainsByPredicate([&Hit](const FHitResult& Existing)
        {
            return Hit.GetActor()==Existing.GetActor() && ResolveSurface(Hit)==ResolveSurface(Existing)
                && FVector::DistSquared(Hit.ImpactPoint,Existing.ImpactPoint)<FMath::Square(30.f);
        });
        if (!bNearby) Clusters.Add(Hit);
    }
    // Distribute the four representatives over all valid pellets, not just the first four.
    const int32 Count = FMath::Min(4,Clusters.Num());
    for (int32 I=0;I<Count;++I) ReportImpact(TEXT("shotgun"),ShotId,Clusters[I*Clusters.Num()/Count],I,false);
}

void UHeliosImpactPresentationSubsystem::SpawnTracer(const FVector& Start,const FVector& End,bool bShotgun)
{
    if(FVector::DistSquared(Start,End)<.01f) return;
    const int32 I=AcquireFlight();if(I==INDEX_NONE) return;
    auto& Flight=FlightTrails[I];Flight.Start=Start;Flight.End=End;Flight.bShotgun=bShotgun;
    if(bShotgun)
    {
        FlightMeshes[I]->SetMaterial(0,ShotgunCoreMaterials[I]);
        FlightHalos[I]->SetMaterial(0,ShotgunHaloMaterials[I]);
    }
    Flight.Duration=bShotgun?FMath::Clamp(FVector::Distance(Start,End)/14000.f,.085f,.22f):FMath::Clamp(FVector::Distance(Start,End)/18000.f,.055f,.18f);
    UpdateFlight(I,FMath::Lerp(Start,End,FMath::Min(20.f/FVector::Distance(Start,End),1.f)));
}

int32 UHeliosImpactPresentationSubsystem::AcquireFlight()
{
    if(FlightMeshes.IsEmpty()) return INDEX_NONE;
    const int32 I=NextFlight++%FlightMeshes.Num();
    FlightTrails[I]=FFlightTrail();FlightTrails[I].bActive=true;
    FlightMeshes[I]->SetVisibility(false);FlightHalos[I]->SetVisibility(false);ShotgunTails[I]->SetVisibility(false);
    FlightMeshes[I]->SetMaterial(0,FlightMaterials[I]);FlightHalos[I]->SetMaterial(0,HaloMaterials[I]);
    return I;
}

void UHeliosImpactPresentationSubsystem::TrackProjectile(AActor* Projectile)
{
    if(!IsValid(Projectile)||!Projectile->GetActorEnableCollision()) return;
    const int32 I=AcquireFlight();if(I==INDEX_NONE) return;
    auto& Flight=FlightTrails[I];Flight.Projectile=Projectile;
    Flight.Start=Projectile->GetActorLocation();Flight.LastHead=Flight.Start;
    Flight.bFollowingProjectile=true;Flight.Duration=4.f;
}

void UHeliosImpactPresentationSubsystem::StopProjectile(AActor* Projectile,const FHitResult& Hit)
{
    if(!Projectile) return;
    for(int32 I=0;I<FlightTrails.Num();++I)
    {
        auto& Flight=FlightTrails[I];
        if(!Flight.bActive || !Flight.bFollowingProjectile || Flight.Projectile.Get()!=Projectile) continue;
        // Use the collision surface, never the projectile centre or a predicted ray.
        Flight.End=Hit.ImpactPoint;Flight.bFading=true;Flight.bFollowingProjectile=false;
        Flight.Age=0;Flight.Duration=.06f;
        UpdateFlight(I,Flight.End);
        break;
    }
}

void UHeliosImpactPresentationSubsystem::UpdateFlight(int32 Index,const FVector& Head,float Opacity)
{
    auto& Flight=FlightTrails[Index];Flight.LastHead=Head;
    const FVector Path=Head-Flight.Start;
    const float Length=FMath::Min(Path.Size(),Flight.bShotgun?38.f:(Flight.Projectile.IsValid()?150.f:100.f));
    const bool bVisible=Length>.1f && Opacity>0;
    FlightMeshes[Index]->SetVisibility(bVisible);FlightHalos[Index]->SetVisibility(bVisible);
    ShotgunTails[Index]->SetVisibility(bVisible&&Flight.bShotgun);
    if(!bVisible) return;
    const FVector Direction=Path.GetSafeNormal();
    const FVector Center=Head-Direction*Length*.5f;
    const FRotator Rotation=FRotationMatrix::MakeFromZ(Direction).Rotator();
    FlightMeshes[Index]->SetWorldLocationAndRotation(Center,Rotation);
    FlightHalos[Index]->SetWorldLocationAndRotation(Center,Rotation);
    const float CoreWidth=Flight.bShotgun?.026f:.014f;
    const float HaloWidth=Flight.bShotgun?.085f:.045f;
    FlightMeshes[Index]->SetWorldScale3D(FVector(CoreWidth,CoreWidth,Length/100.f));
    FlightHalos[Index]->SetWorldScale3D(FVector(HaloWidth,HaloWidth,Length/100.f));
    auto* CoreMaterial=Flight.bShotgun?ShotgunCoreMaterials[Index].Get():FlightMaterials[Index].Get();
    auto* HaloMaterial=Flight.bShotgun?ShotgunHaloMaterials[Index].Get():HaloMaterials[Index].Get();
    CoreMaterial->SetScalarParameterValue(TEXT("TrailOpacity"),Opacity);
    HaloMaterial->SetScalarParameterValue(TEXT("TrailOpacity"),Opacity*(Flight.bShotgun?.8f:1.f));
    if(Flight.bShotgun)
    {
        // A long tapered ember follows the short white-hot head on the actual pellet ray.
        // Both ends stay inside the muzzle-to-contact segment, including during fade-out.
        const float TailLength=FMath::Min(Path.Size(),240.f);
        ShotgunTails[Index]->SetWorldLocationAndRotation(Head-Direction*TailLength*.5f,Rotation);
        ShotgunTails[Index]->SetWorldScale3D(FVector(.035f,.035f,TailLength/100.f));
        ShotgunTailMaterials[Index]->SetScalarParameterValue(TEXT("TrailOpacity"),Opacity*.65f);
    }
}

void UHeliosImpactPresentationSubsystem::Tick(float DeltaTime)
{
    const float Now=GetWorld()->GetTimeSeconds();
    for(int32 I=0;I<FlightTrails.Num();++I)
    {
        auto& Flight=FlightTrails[I];if(!Flight.bActive) continue;
        Flight.Age+=DeltaTime;
        if(Flight.bFading)
        {
            UpdateFlight(I,Flight.End,FMath::Clamp(1.f-Flight.Age/Flight.Duration,0.f,1.f));
            if(Flight.Age>=Flight.Duration) Flight.bActive=false;
            continue;
        }
        AActor* Projectile=Flight.Projectile.Get();
        const bool bStopped=Flight.bFollowingProjectile&&(!IsValid(Projectile)||Projectile->IsActorBeingDestroyed()||!Projectile->GetActorEnableCollision());
        if(bStopped||Flight.Age>=Flight.Duration)
        {
            if(Flight.bFollowingProjectile) Flight.End=Flight.LastHead;
            Flight.bFading=true;Flight.bFollowingProjectile=false;Flight.Age=0;Flight.Duration=Flight.bShotgun?.10f:.06f;
            UpdateFlight(I,Flight.End);
            continue;
        }
        const FVector Head=Flight.bFollowingProjectile?Projectile->GetActorLocation():FMath::Lerp(Flight.Start,Flight.End,Flight.Age/Flight.Duration);
        UpdateFlight(I,Head);
    }
    Bursts.RemoveAll([Now](const FBurst& Entry){return !Entry.Component.IsValid() || Entry.Ends<=Now;});
    Marks.RemoveAll([Now](const FMark& Entry)
    {
        if (Entry.Ends<=Now && Entry.Component.IsValid()) Entry.Component->DestroyComponent();
        return !Entry.Component.IsValid() || Entry.Ends<=Now;
    });
    for (auto It=Seen.CreateIterator();It;++It) if (It.Value()<Now) It.RemoveCurrent();
    for (auto It=SoundSeen.CreateIterator();It;++It) if (It.Value()<Now) It.RemoveCurrent();
    for (auto It=LaserLast.CreateIterator();It;++It) if (!It.Key().IsValid() || Now-It.Value()>2.f) It.RemoveCurrent();
}

void UHeliosImpactPresentationSubsystem::Clear(bool bIncludeDecals)
{
    for(int32 I=0;I<FlightTrails.Num();++I){FlightTrails[I].bActive=false;if(FlightMeshes[I])FlightMeshes[I]->SetVisibility(false);if(FlightHalos[I])FlightHalos[I]->SetVisibility(false);if(ShotgunTails[I])ShotgunTails[I]->SetVisibility(false);}
    for (const auto& Entry:Bursts) if (auto* FX=Entry.Component.Get()) UHeliosWeaponVFX::ReleaseTracked(FX);
    if(bIncludeDecals)
    {
        for (const auto& Entry:Marks) if (auto* Mark=Entry.Component.Get()) Mark->DestroyComponent();
        Marks.Empty();
    }
    Bursts.Empty(); Seen.Empty(); SoundSeen.Empty(); LaserLast.Empty();
}

void UHeliosImpactPresentationSubsystem::Deinitialize()
{
    Clear(true); Effects.Empty(); Decals.Empty(); Sounds.Empty(); LaserHotspot=nullptr; LaserEnemyHit=nullptr; Tracer=nullptr;
    AActor* FlightActor=FlightMeshes.IsEmpty()?nullptr:FlightMeshes[0]->GetOwner();
    for(const auto& Mesh:FlightMeshes)if(IsValid(Mesh))Mesh->DestroyComponent();
    for(const auto& Mesh:FlightHalos)if(IsValid(Mesh))Mesh->DestroyComponent();
    for(const auto& Mesh:ShotgunTails)if(IsValid(Mesh))Mesh->DestroyComponent();
    ShotgunTails.Empty();ShotgunCoreMaterials.Empty();ShotgunHaloMaterials.Empty();ShotgunTailMaterials.Empty();ShotgunFlare=nullptr;
    if(IsValid(FlightActor)&&!FlightActor->IsActorBeingDestroyed())FlightActor->Destroy();
    FlightMeshes.Empty();FlightHalos.Empty();FlightMaterials.Empty();HaloMaterials.Empty();FlightTrails.Empty();
    Super::Deinitialize();
}
