#include "Control/HeliosAutonomyComponent.h"
#include "Player/HeliosArenaCharacter.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Control/HeliosControlSubsystem.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "MCP/HeliosMCPJson.h"
#include "Town/HeliosTownSubsystem.h"
#include "World/HeliosMapDefinition.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "HeliosOutpost.h"

using namespace HeliosMCP;
UHeliosAutonomyComponent::UHeliosAutonomyComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
AHeliosArenaCharacter* UHeliosAutonomyComponent::Player() const { return Cast<AHeliosArenaCharacter>(GetOwner()); }
void UHeliosAutonomyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Cancel(TEXT("world_changed")); Super::EndPlay(Reason);
}
void UHeliosAutonomyComponent::ClearInputs()
{
    ++PathGeneration; bPathPending=false; PathPoints.Reset(); PathIndex=0; bFiring=false;
    if(auto* P=Player()) { P->CancelAutomationCombat(); P->ClearMovementInput(); P->SetSprintingFromAutomation(false); }
}
void UHeliosAutonomyComponent::Finish(const FString& NewStatus,const FString& Reason)
{
    ClearInputs(); Status=NewStatus; Detail=Reason; Action=Reason;
    if(!GoalId.IsEmpty())
    {
        History.Add(GoalId,GoalStatus());
        if(History.Num()>128) { auto It=History.CreateIterator(); It.RemoveCurrent(); }
        UE_LOG(LogHeliosOutpost,Display,TEXT("HELIOS_MCP_GOAL id=%s status=%s reason=%s"),*GoalId,*Status,*Reason);
    }
}
void UHeliosAutonomyComponent::Cancel(const FString& Reason)
{
    if(IsActive()) Finish(TEXT("cancelled"),Reason); else ClearInputs();
}
void UHeliosAutonomyComponent::ResetMemory() { Cancel(TEXT("run_changed")); Memory.Reset(); Visited.Reset(); }

bool UHeliosAutonomyComponent::IsVisible(AHeliosEnemyActor* Enemy) const
{
    auto* P=Player(); if(!P||!IsValid(Enemy)) return false;
    FVector Eye; FRotator Rotation; P->GetActorEyesViewPoint(Eye,Rotation);
    const FVector Aim=Enemy->GetActorLocation()+FVector(0,0,70);
    const FVector Offset=Aim-Eye;
    if(Offset.SizeSquared()>FMath::Square(12000.0)) return false;
    const FVector Local=Rotation.UnrotateVector(Offset);
    const auto* Camera=P->FindComponentByClass<UCameraComponent>();
    const double TanH=FMath::Tan(FMath::DegreesToRadians((Camera?Camera->FieldOfView:90.f)*0.5f));
    int32 W=16,H=9; if(auto* PC=Cast<APlayerController>(P->GetController())) PC->GetViewportSize(W,H);
    const double Aspect=H>0?static_cast<double>(W)/H:16.0/9.0;
    if(Local.X<=0||FMath::Abs(Local.Y)>Local.X*TanH||FMath::Abs(Local.Z)>Local.X*TanH/FMath::Max(0.1,Aspect)) return false;
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(HeliosMCPSight),true,P);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Eye,Aim,ECC_Visibility,Query)||Hit.GetActor()==Enemy;
}
void UHeliosAutonomyComponent::Sense()
{
    const double Now=FPlatformTime::Seconds();
    for(auto& Item:Memory) Item.Value.bVisible=false;
    for(TActorIterator<AHeliosEnemyActor> It(GetWorld());It;++It)
    {
        auto* Enemy=*It;
        if(Enemy->RuntimeActorId.IsEmpty()||!IsVisible(Enemy)) continue;
        auto& Seen=Memory.FindOrAdd(Enemy->RuntimeActorId);
        Seen.Actor=Enemy; Seen.Location=Enemy->GetActorLocation(); Seen.Type=Enemy->EnemyTypeId;
        Seen.Health=Enemy->GetHealth(); Seen.SeenAt=Now; Seen.bVisible=true;
    }
    for(auto It=Memory.CreateIterator();It;++It)
        if(Now-It.Value().SeenAt>30) It.RemoveCurrent();
}
TSharedPtr<FJsonObject> UHeliosAutonomyComponent::SetGoal(const TSharedPtr<FJsonObject>& Params)
{
    auto* P=Player(); auto* Adventure=AHeliosAdventureDirector::Find(GetWorld());
    if(!P||!P->GetController()||P->IsDead()||GetWorld()->IsPaused()||!Adventure||Adventure->IsSelectingWeapon()||Adventure->IsRunOver())
        return Result(false,TEXT("actor_unavailable"),TEXT("An active unpaused adventure is required."));
    if(!UHeliosWeaponRegistry::FindWeapon(P->GetWeaponId()) || P->IsMechMode() || P->IsFighterMode())
        return Result(false,TEXT("unsupported_weapon"),TEXT("Autonomy requires an equipped handheld primary weapon."));
    const FString NewKind=String(Params,TEXT("kind"));
    if(NewKind!=TEXT("explore")&&NewKind!=TEXT("navigate")&&NewKind!=TEXT("engage"))
        return Result(false,TEXT("invalid_goal"),TEXT("Use explore, navigate or engage."));
    FVector NewDestination=FVector::ZeroVector;
    if(NewKind==TEXT("navigate")&&!ReadVector(Params,TEXT("location_cm"),NewDestination))
        return Result(false,TEXT("invalid_goal"),TEXT("location_cm must contain three finite coordinates."));
    if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())if(Town->IsTownWorld())
    {
        if(!Town->IsWorldReady())return Result(false,TEXT("world_not_ready"));
        const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld());
        if(Map&&NewKind==TEXT("navigate")&&FVector::Dist2D(P->GetActorLocation(),NewDestination)>Map->NavigationRadiusCm)
            return Result(false,TEXT("target_outside_loaded_navigation"),TEXT("Town navigation is local_loaded; request a destination within 120 meters."));
    }
    Sense(); const FString NewTarget=String(Params,TEXT("actor_id"));
    if(NewKind==TEXT("engage")&&(!Memory.Contains(NewTarget)||Memory[NewTarget].Health<=0||FPlatformTime::Seconds()-Memory[NewTarget].SeenAt>3))
        return Result(false,TEXT("target_unavailable"),TEXT("Target must have been observed in the last three seconds."));
    Cancel(TEXT("replaced"));
    if(auto* Control=GetWorld()->GetSubsystem<UHeliosControlSubsystem>()) Control->Stop(TEXT("player_01"));
    GoalId=FGuid::NewGuid().ToString(EGuidFormats::Digits); Kind=NewKind; TargetId=NewTarget; Destination=NewDestination;
    Status=TEXT("accepted"); Action=TEXT("planning"); Detail.Reset(); CombatTarget.Reset(); Failures=0;
    StartedAt=ProgressAt=FPlatformTime::Seconds(); ProgressLocation=P->GetActorLocation(); DecisionRemaining=0;
    UE_LOG(LogHeliosOutpost,Display,TEXT("HELIOS_MCP_GOAL id=%s accepted kind=%s"),*GoalId,*Kind);
    auto Out=Result(); Out->SetObjectField(TEXT("goal"),GoalStatus()); return Out;
}
TSharedPtr<FJsonObject> UHeliosAutonomyComponent::GoalStatus(const FString& Id) const
{
    if(!Id.IsEmpty()&&Id!=GoalId)
    {
        if(const auto* Old=History.Find(Id)) return *Old;
        return Result(false,TEXT("goal_not_found"),TEXT("No retained goal has this id."));
    }
    auto Out=Result(); Out->SetStringField(TEXT("goal_id"),GoalId); Out->SetStringField(TEXT("kind"),Kind);
    Out->SetStringField(TEXT("status"),Status); Out->SetStringField(TEXT("action"),Action); Out->SetStringField(TEXT("reason"),Detail);
    Out->SetNumberField(TEXT("path_failures"),Failures); Out->SetStringField(TEXT("target_id"),CombatTarget.IsEmpty()?TargetId:CombatTarget);
    Out->SetArrayField(TEXT("destination_cm"),Vector(Destination)); return Out;
}
void UHeliosAutonomyComponent::PathFailed()
{
    bPathPending=false; PathPoints.Reset(); Action=TEXT("replanning");
    if(++Failures>=3) Finish(TEXT("failed"),TEXT("navigation_blocked"));
}
void UHeliosAutonomyComponent::RequestPath(const FVector& Location)
{
    if(bPathPending||!IsActive()) return;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); auto* P=Player();
    if(P)if(const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld()))if(Map->NavigationScope==TEXT("local_loaded")&&FVector::Dist2D(P->GetActorLocation(),Location)>Map->NavigationRadiusCm)
    { Finish(TEXT("failed"),TEXT("target_outside_loaded_navigation"));return; }
    FNavLocation Projected;
    if(!Nav||!P||!Nav->ProjectPointToNavigation(Location,Projected,FVector(100,100,200))) { PathFailed(); return; }
    const auto& Props=P->GetCharacterMovement()->GetNavAgentPropertiesRef();
    const ANavigationData* Data=Nav->GetNavDataForProps(Props,P->GetActorLocation());
    if(!Data) { PathFailed(); return; }
    PathDestination=Projected.Location; LastPathAt=FPlatformTime::Seconds();
    FPathFindingQuery Query(P,*Data,P->GetNavAgentLocation(),Projected.Location);
    Query.SetAllowPartialPaths(false); bPathPending=true; const uint32 Generation=++PathGeneration;
    const TWeakObjectPtr<UHeliosAutonomyComponent> WeakThis(this);
    const uint32 QueryId=Nav->FindPathAsync(Props,Query,FNavPathQueryDelegate::CreateLambda(
        [WeakThis,Generation](uint32,ENavigationQueryResult::Type ResultType,FNavPathSharedPtr Path)
        {
            if(!WeakThis.IsValid()||WeakThis->PathGeneration!=Generation||!WeakThis->IsActive()) return;
            auto* Self=WeakThis.Get(); Self->bPathPending=false;
            if(ResultType!=ENavigationQueryResult::Success||!Path.IsValid()||Path->IsPartial()) { Self->PathFailed(); return; }
            Self->PathPoints.Reset(); for(const auto& Point:Path->GetPathPoints()) Self->PathPoints.Add(Point.Location);
            Self->PathIndex=Self->PathPoints.Num()>1?1:0; Self->Action=TEXT("moving");
            Self->ProgressAt=FPlatformTime::Seconds(); Self->ProgressLocation=Self->Player()->GetActorLocation();
        }));
    if(QueryId==INVALID_NAVQUERYID) PathFailed();
}
void UHeliosAutonomyComponent::Decide()
{
    auto* P=Player(); const double Now=FPlatformTime::Seconds(); CombatTarget.Reset();
    if(Kind==TEXT("engage"))
    {
        auto* Seen=Memory.Find(TargetId);
        if(!Seen||Now-Seen->SeenAt>3) { Finish(TEXT("failed"),TEXT("target_lost")); return; }
        if(Seen->bVisible&&Seen->Health<=0) { Finish(TEXT("completed"),TEXT("target_eliminated")); return; }
        Destination=Seen->Location;
        if(Seen->bVisible) CombatTarget=TargetId;
    }
    else if(Kind==TEXT("explore"))
    {
        double Nearest=DBL_MAX;
        for(const auto& Pair:Memory)
            if(Pair.Value.bVisible&&Pair.Value.Health>0)
            {
                const double Dist=FVector::DistSquared(Pair.Value.Location,P->GetActorLocation());
                if(Dist<Nearest) { Nearest=Dist; CombatTarget=Pair.Key; }
            }
        if(!CombatTarget.IsEmpty()) Destination=Memory[CombatTarget].Location;
        else if(PathPoints.IsEmpty()&&!bPathPending)
        {
            auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
            bool Found=false; double Best=DBL_MAX;
            for(int I=0;Nav&&I<8;++I)
            {
                FNavLocation Candidate;
                if(!Nav->GetRandomReachablePointInRadius(P->GetActorLocation(),2000,Candidate)) continue;
                if(FVector::Dist2D(Candidate.Location,P->GetActorLocation())<400) continue;
                const FString Cell=FString::Printf(TEXT("%d:%d:%d"),FMath::FloorToInt(Candidate.Location.X/500),FMath::FloorToInt(Candidate.Location.Y/500),FMath::FloorToInt(Candidate.Location.Z/250));
                const double Cost=Visited.FindRef(Cell)*10000-FVector::Dist2D(Candidate.Location,P->GetActorLocation());
                if(Cost<Best) { Best=Cost; Destination=Candidate.Location; Found=true; }
            }
            if(!Found) { PathFailed(); return; }
        }
    }
    if(!CombatTarget.IsEmpty())
    {
        const auto* Weapon=UHeliosWeaponRegistry::FindWeapon(P->GetWeaponId());
        const double Range=Weapon?Weapon->Range:10000;
        if(FVector::Dist(P->GetActorLocation(),Destination)<Range*0.75)
        {
            ++PathGeneration; bPathPending=false; PathPoints.Reset();
            P->GetCharacterMovement()->StopMovementImmediately(); Action=P->IsReloading()?TEXT("reloading"):TEXT("aiming"); return;
        }
    }
    if(!bPathPending&&(PathPoints.IsEmpty()||(Now-LastPathAt>1&&FVector::Dist2D(Destination,PathDestination)>200))) RequestPath(Destination);
}
void UHeliosAutonomyComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,TickType,Function);
    SenseRemaining-=Delta; if(SenseRemaining<=0) { Sense(); SenseRemaining=.2f; }
    if(!IsActive()) return;
    auto* P=Player();
    if(!P||P->IsDead()||!P->GetController()||GetWorld()->IsPaused()) { Cancel(TEXT("actor_unavailable")); return; }
    if(!UHeliosWeaponRegistry::FindWeapon(P->GetWeaponId()) || P->IsMechMode() || P->IsFighterMode())
        { Cancel(TEXT("weapon_changed")); return; }
    Status=TEXT("running"); DecisionRemaining-=Delta;
    if(DecisionRemaining<=0) { Decide(); DecisionRemaining=.2f; }
    if(!IsActive()) return;
    FVector Aim; bool HasAim=false, CanFire=false;
    if(auto* Seen=Memory.Find(CombatTarget))
    {
        if(Seen->bVisible&&Seen->Actor.IsValid()&&Seen->Health>0&&IsVisible(Seen->Actor.Get())&&!Seen->Actor->IsEnemyDead())
        {
            Aim=Seen->Actor->GetActorLocation()+FVector(0,0,70); HasAim=true;
            FVector Eye; FRotator EyeRotation; P->GetActorEyesViewPoint(Eye,EyeRotation);
            const auto* Weapon=UHeliosWeaponRegistry::FindWeapon(P->GetWeaponId());
            CanFire=FVector::DotProduct(EyeRotation.Vector(),(Aim-Eye).GetSafeNormal())>=FMath::Cos(FMath::DegreesToRadians(3.f))
                &&FVector::Dist(Eye,Aim)<=(Weapon?Weapon->Range:10000.f);
        }
    }
    if(CanFire&&!P->IsReloading())
    {
        if(P->GetAmmo()<=0) { P->StopFireFromAutomation(); bFiring=false; P->StartReloadFromAutomation(); Action=TEXT("reloading"); }
        else { if(!bFiring) P->StartFireFromAutomation(); bFiring=true; Action=TEXT("firing"); }
    }
    else if(bFiring) { P->StopFireFromAutomation(); bFiring=false; }
    if(PathPoints.IsValidIndex(PathIndex))
    {
        FVector Offset=PathPoints[PathIndex]-P->GetNavAgentLocation();
        if(Offset.Size2D()<75&&FMath::Abs(Offset.Z)<120)
        {
            if(++PathIndex>=PathPoints.Num())
            {
                PathPoints.Reset(); P->GetCharacterMovement()->StopMovementImmediately();
                const FString Cell=FString::Printf(TEXT("%d:%d:%d"),FMath::FloorToInt(Destination.X/500),FMath::FloorToInt(Destination.Y/500),FMath::FloorToInt(Destination.Z/250));
                ++Visited.FindOrAdd(Cell); Failures=0;
                if(Kind==TEXT("navigate")) { Finish(TEXT("completed"),TEXT("arrived")); return; }
            }
        }
        if(PathPoints.IsValidIndex(PathIndex))
        {
            const FVector Direction=(PathPoints[PathIndex]-P->GetActorLocation()).GetSafeNormal2D();
            P->AddMovementInput(Direction,1);
            if(!HasAim) { Aim=P->GetActorLocation()+Direction*1000+FVector(0,0,64); HasAim=true; }
            const double Now=FPlatformTime::Seconds();
            if(FVector::Dist2D(P->GetActorLocation(),ProgressLocation)>50) { ProgressAt=Now; ProgressLocation=P->GetActorLocation(); }
            else if(Now-ProgressAt>2) { ++PathGeneration; PathFailed(); ProgressAt=Now; }
        }
    }
    if(HasAim)
    {
        FVector Eye; FRotator Rot; P->GetActorEyesViewPoint(Eye,Rot);
        FRotator Next=FMath::RInterpConstantTo(P->GetControlRotation(),(Aim-Eye).Rotation(),Delta,180.f);
        Next.Roll=0; Next.Pitch=FMath::Clamp(FRotator::NormalizeAxis(Next.Pitch),-80.f,80.f); P->GetController()->SetControlRotation(Next);
    }
}
TSharedPtr<FJsonObject> UHeliosAutonomyComponent::Observation()
{
    Sense(); auto Out=Result(); auto* P=Player(); if(!P) return Result(false,TEXT("actor_unavailable"));
    const auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>();const bool IsTown=Town&&Town->IsTownWorld();const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld());
    Out->SetStringField(TEXT("map_id"),Map?Map->Id.ToString():FString());
    Out->SetStringField(TEXT("map_display_name"),Map?Map->DisplayName.ToString():FString());
    Out->SetBoolField(TEXT("world_ready"),!IsTown||Town->IsWorldReady());
    Out->SetStringField(TEXT("navigation_scope"),Map?Map->NavigationScope.ToString():FString());
    if(Map){Out->SetNumberField(TEXT("district_count"),Map->DistrictCount);Out->SetStringField(TEXT("gameplay_rules"),Map->GameplayRules.ToString());}
    if(IsTown) { Out->SetObjectField(TEXT("town"),Parse(Town->GetStateJson())); Out->SetNumberField(TEXT("navigation_radius_cm"),Map?Map->NavigationRadiusCm:0); }
    Out->SetNumberField(TEXT("frame_id"),static_cast<double>(GFrameCounter)); Out->SetNumberField(TEXT("observed_at"),FDateTime::UtcNow().GetTicks()/10000000.0-62135596800.0);
    FHeliosActorState State; auto* Control=GetWorld()->GetSubsystem<UHeliosControlSubsystem>(); if(Control) Control->GetState(TEXT("player_01"),State);
    auto PlayerJson=Object(); PlayerJson->SetStringField(TEXT("actor_id"),TEXT("player_01"));
    PlayerJson->SetArrayField(TEXT("location_cm"),Vector(P->GetActorLocation()));
    PlayerJson->SetArrayField(TEXT("rotation_degrees"),Vector(FVector(P->GetControlRotation().Pitch,P->GetControlRotation().Yaw,0)));
    PlayerJson->SetArrayField(TEXT("velocity_cm_per_second"),Vector(P->GetVelocity()));
    PlayerJson->SetBoolField(TEXT("grounded"),P->GetCharacterMovement()->IsMovingOnGround());
    PlayerJson->SetStringField(TEXT("zone_id"),IsTown?Town->GetCurrentZoneId():State.CurrentZoneId); PlayerJson->SetNumberField(TEXT("health"),P->GetHealth());
    PlayerJson->SetNumberField(TEXT("ammo"),P->GetAmmo()); PlayerJson->SetNumberField(TEXT("magazine_size"),P->GetMagazineSize());
    PlayerJson->SetBoolField(TEXT("reloading"),P->IsReloading()); PlayerJson->SetBoolField(TEXT("dead"),P->IsDead());
    HeliosLoadout::WriteJson(PlayerJson.ToSharedRef(),P->GetLoadout()); Out->SetObjectField(TEXT("player"),PlayerJson);
    TArray<TSharedPtr<FJsonValue>> Visible,Remembered;
    for(const auto& Pair:Memory)
    {
        auto E=Object(); E->SetStringField(TEXT("actor_id"),Pair.Key); E->SetStringField(TEXT("type_id"),Pair.Value.Type);
        E->SetArrayField(TEXT("location_cm"),Vector(Pair.Value.Location)); E->SetNumberField(TEXT("health"),Pair.Value.Health);
        E->SetNumberField(TEXT("last_seen_age_seconds"),FPlatformTime::Seconds()-Pair.Value.SeenAt);
        (Pair.Value.bVisible?Visible:Remembered).Add(MakeShared<FJsonValueObject>(E));
    }
    Out->SetArrayField(TEXT("visible_enemies"),Visible); Out->SetArrayField(TEXT("enemy_memory"),Remembered);
    Out->SetObjectField(TEXT("goal"),GoalStatus()); Out->SetBoolField(TEXT("screenshot_available"),false);
    TArray<TSharedPtr<FJsonValue>> Route; for(int32 I=PathIndex;I<PathPoints.Num();++I) Route.Add(MakeShared<FJsonValueArray>(Vector(PathPoints[I])));
    Out->SetArrayField(TEXT("path_cm"),Route);
    auto Navigation=Object(); Navigation->SetStringField(TEXT("scope"),Map?Map->NavigationScope.ToString():FString());if(IsTown)Navigation->SetNumberField(TEXT("radius_cm"),Map?Map->NavigationRadiusCm:0); Navigation->SetBoolField(TEXT("path_pending"),bPathPending);
    Navigation->SetNumberField(TEXT("remaining_waypoints"),FMath::Max(0,PathPoints.Num()-PathIndex));
    Navigation->SetNumberField(TEXT("path_failures"),Failures);
    Navigation->SetNumberField(TEXT("arrival_tolerance_cm"),75); Out->SetObjectField(TEXT("navigation"),Navigation);
    TArray<TSharedPtr<FJsonValue>> Exploration;
    for(const auto& Cell:Visited)
    {
        auto Entry=Object(); Entry->SetStringField(TEXT("cell_id"),Cell.Key); Entry->SetNumberField(TEXT("visit_count"),Cell.Value);
        Exploration.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Out->SetArrayField(TEXT("exploration_memory"),Exploration);
    if(auto* Adventure=AHeliosAdventureDirector::Find(GetWorld()))
    {
        auto StateJson=Parse(Adventure->GetStateJson());
        if(StateJson) { StateJson->RemoveField(TEXT("bosses")); Out->SetObjectField(TEXT("adventure"),StateJson); }
    }
    return Out;
}
