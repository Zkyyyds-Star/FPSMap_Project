#include "QA/HeliosBioRoleQA.h"
#include "Bio/HeliosBioHotelGameMode.h"
#include "Bio/HeliosBioNPC.h"
#include "Bio/HeliosBioPlayer.h"
#include "Bio/HeliosBioRoleDefinition.h"
#include "Bio/HeliosBioRoundInventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/Controller.h"
#include "Dom/JsonObject.h"
#include "ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

void AHeliosBioRoleQA::TickSerum(double Now)
{
    auto* Hunter=Cast<AHeliosBioNPC>(Mode->ParticipantActor(HunterId));
    auto* Target=Cast<AHeliosBioNPC>(Mode->ParticipantActor(VictimId));
    const auto* P=Mode->Model.FindParticipant(0);const auto* H=Mode->Model.FindParticipant(HunterId);const auto* V=Mode->Model.FindParticipant(VictimId);
    if(!Hunter||!Target||!P||!H||!V||!P->bAlive||!H->bAlive||!V->bAlive){Finish(TEXT("Live serum source actor/target missing"));return;}
    if(Now-LastSample>=.025)
    {
        if(Stage==251||Stage==255||Stage==259)SamplePose(TEXT("serum_FP"),FirstPersonMesh());
        if(Stage==253||Stage==257)SamplePose(TEXT("serum_TP"),Hunter->GetMesh());LastSample=Now;
    }
    if(Now<Next)return;
    const FVector Side(-FixtureForward.Y,FixtureForward.X,0);
    const auto Observe=[&](const TCHAR* Action)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("kind"),TEXT("serum_runtime"));Row->SetStringField(TEXT("action"),Action);
        Row->SetNumberField(TEXT("shots"),Mode->TotalSerumShots);Row->SetNumberField(TEXT("cures"),Mode->TotalSerumCures);Row->SetNumberField(TEXT("heals"),Mode->TotalSerumHeals);Row->SetNumberField(TEXT("blocks"),Mode->TotalSerumBlocked);
        Row->SetStringField(TEXT("actual_projectile_diagnostic"),Mode->LastSerumDiagnostic);Row->SetStringField(TEXT("scope"),TEXT("Real source gun/public G or NPC FireSerumAt, original native poses and swept projectile. Explicit fixture placement and singleplayer timing; no original client dispatch claim."));Observations.Add(MakeShared<FJsonValueObject>(Row));
    };
    int32 Infector=INDEX_NONE;for(const auto& Other:Mode->Model.GetParticipants())if(Other.Id!=VictimId&&Other.Team==EHeliosBioTeam::Zombie&&Other.bAlive){Infector=Other.Id;break;}
    const auto Reinfect=[&](){return Infector!=INDEX_NONE&&Mode->Model.Infect(VictimId,Infector);};
    switch(Stage)
    {
    case 250:
    {
        if(!Definition->bSerumSourceVerified){Finish(TEXT("Exact visible serum source proof not ready"));return;}
        Hunter->bQAFreeze=false;Hunter->bQAHoldDecisions=true;
        if(Mode->Player->GetBioAttackCooldown()>0||P->SecondaryCooldownRemaining>0){Next=Now+.1;break;}
        PlaceCombatTarget(500);auto* Inventory=Target->GetRoundInventory();SerumVictimWeapon=Inventory->GetWeapon(0);SerumVictimWeaponId=Inventory->GetWeaponId(0);SerumVictimReserve=Inventory->GetReserve(0);
        Check(TEXT("serum_target_real_original_inventory_retained"),SerumVictimWeapon.IsValid()&&!SerumVictimWeaponId.IsNone());
        SerumShotsBefore=Mode->TotalSerumShots;SerumEffectsBefore=Mode->TotalSerumCures;Mode->Player->StartBioSpecial();
        Check(TEXT("player_G_fires_real_serum_and_starts_cooldown"),Mode->TotalSerumShots==SerumShotsBefore+1&&P->SecondaryCooldownRemaining>0);
        Mode->Player->StartBioSpecial();Check(TEXT("player_serum_cooldown_blocks_repeated_G"),Mode->TotalSerumShots==SerumShotsBefore+1);
        Capture(TEXT("role_06_serum_player"));Next=Now+.8;Stage=251;break;
    }
    case 251:
    {
        auto* Inventory=Target->GetRoundInventory();
        Check(TEXT("player_serum_real_first_hit_cures_zombie"),Mode->TotalSerumCures==SerumEffectsBefore+1&&V->Team==EHeliosBioTeam::Human);
        Check(TEXT("serum_cure_restores_same_human_actor_and_inventory"),Inventory->GetWeapon(0)==SerumVictimWeapon.Get()&&Inventory->GetWeaponId(0)==SerumVictimWeaponId&&Inventory->GetReserve(0)==SerumVictimReserve&&Inventory->GetActiveSlot()==0);
        CheckPose(TEXT("serum_FP"),true);Observe(TEXT("player_cure"));Check(TEXT("serum_reinfection_uses_real_rule"),Reinfect());Next=Now+.5;Stage=252;break;
    }
    case 252:
        PlaceActor(Hunter,FixtureOrigin);PlaceActor(Target,FixtureOrigin+FixtureForward*500);PlaceActor(Mode->Player,FixtureOrigin+Side*140);
        if(H->SecondaryCooldownRemaining>0||Hunter->GetBioAttackCooldown()>0){Next=Now+.1;break;}
        SerumShotsBefore=Mode->TotalSerumShots;SerumEffectsBefore=Mode->TotalSerumCures;
        Check(TEXT("NPC_actual_TP_serum_fire_accepted"),Hunter->FireSerumAt(Target));Observe(TEXT("NPC_cure_launch_result"));Check(TEXT("NPC_serum_cooldown_blocks_repeat"),!Hunter->FireSerumAt(Target)&&Mode->TotalSerumShots==SerumShotsBefore+1);
        Next=Now+.8;Stage=253;break;
    case 253:
        Check(TEXT("NPC_serum_real_projectile_cures_zombie"),Mode->TotalSerumCures==SerumEffectsBefore+1&&V->Team==EHeliosBioTeam::Human);CheckPose(TEXT("serum_TP"),true);Observe(TEXT("NPC_cure"));
        Next=Now+FMath::Max(.1f,P->SecondaryCooldownRemaining+.1f);Stage=254;break;
    case 254:
        PlaceCombatTarget(500);PlaceActor(Target,FixtureOrigin+Side*140);PlaceActor(Hunter,FixtureOrigin+FixtureForward*500);
        if(Mode->Player->GetController())Mode->Player->GetController()->SetControlRotation((Hunter->GetActorLocation()-Mode->Player->FirstPersonCamera->GetComponentLocation()).Rotation());
        {FMinimalViewInfo Camera;Mode->Player->CalcCamera(0,Camera);}
        Check(TEXT("serum_heal_fixture_real_nonlethal_damage"),Mode->DamageParticipant(HunterId,Infector,100,false)&&H->Health<H->MaxHealth);
        HealthBefore=H->Health;SerumShotsBefore=Mode->TotalSerumShots;SerumEffectsBefore=Mode->TotalSerumHeals;Mode->Player->StartBioSpecial();Next=Now+.8;Stage=255;break;
    case 255:
        Check(TEXT("player_serum_hunter_full_health_same_role"),Mode->TotalSerumShots==SerumShotsBefore+1&&Mode->TotalSerumHeals==SerumEffectsBefore+1&&H->Health==H->MaxHealth&&H->HunterRoleId==TEXT("BioRedeemerHunter")&&HealthBefore<H->Health);
        Observe(TEXT("player_heals_hunter"));Next=Now+FMath::Max(.1f,H->SecondaryCooldownRemaining+.1f);Stage=256;break;
    case 256:
        PlaceActor(Hunter,FixtureOrigin);PlaceActor(Mode->Player,FixtureOrigin+FixtureForward*500);PlaceActor(Target,FixtureOrigin+Side*140);
        Check(TEXT("serum_NPC_heal_fixture_real_damage"),Mode->DamageParticipant(0,Infector,100,false)&&P->Health<P->MaxHealth);
        SerumShotsBefore=Mode->TotalSerumShots;SerumEffectsBefore=Mode->TotalSerumHeals;Check(TEXT("NPC_serum_aims_injured_hunter"),Hunter->FireSerumAt(Mode->Player));Observe(TEXT("NPC_heal_launch_result"));Next=Now+.8;Stage=257;break;
    case 257:
        Check(TEXT("NPC_serum_restores_player_hunter_to_full"),Mode->TotalSerumShots==SerumShotsBefore+1&&Mode->TotalSerumHeals==SerumEffectsBefore+1&&P->Health==P->MaxHealth&&P->HunterRoleId==TEXT("BioRedeemerHunter"));
        Observe(TEXT("NPC_heals_player"));Next=Now+FMath::Max(.1f,P->SecondaryCooldownRemaining+.1f);Stage=258;break;
    case 258:
        Check(TEXT("serum_wall_target_reinfected"),Reinfect());PlaceCombatTarget(500);MakeObstacle(250);SerumBlocksBefore=Mode->TotalSerumBlocked;SerumEffectsBefore=Mode->TotalSerumCures;SerumShotsBefore=Mode->TotalSerumShots;
        Mode->Player->StartBioSpecial();Next=Now+.8;Stage=259;break;
    case 259:
        Check(TEXT("serum_real_wall_first_collision_prevents_cure"),Mode->TotalSerumShots==SerumShotsBefore+1&&Mode->TotalSerumBlocked==SerumBlocksBefore+1&&Mode->TotalSerumCures==SerumEffectsBefore&&V->Team==EHeliosBioTeam::Zombie);
        Observe(TEXT("wall_blocks_serum"));RemoveObstacle();Next=Now+FMath::Max(.1f,P->SecondaryCooldownRemaining+.1f);Stage=260;break;
    case 260:
    {
        // Explicitly display the real serum Idle before testing its socket guard;
        // an absent visible gun must not accidentally satisfy this assertion.
        auto* Gun=FirstPersonMesh();if(!Gun){Finish(TEXT("Native player arms missing for isolated socket guard"));return;}
        Gun->SetSkeletalMesh(Definition->SecondaryFirstPersonMesh.LoadSynchronous());Gun->SetRelativeTransform(Definition->SecondaryWeaponTransform);
        Gun->PlayAnimation(Definition->FPSecondaryIdle.LoadSynchronous(),true);Gun->TickAnimation(0,false);Gun->RefreshBoneTransforms();Gun->UpdateComponentToWorld();
        FVector Muzzle;const bool Has=Mode->GetParticipantSerumMuzzle(0,Muzzle);SerumShotsBefore=Mode->TotalSerumShots;
        Check(TEXT("serum_socket_guard_fixture_has_actual_visible_source_muzzle"),Has);
        Check(TEXT("serum_wrong_muzzle_rejected_without_cooldown"),Has&&!Mode->FireParticipantSerum(0,Muzzle+FVector(50,0,0),FixtureForward)&&P->SecondaryCooldownRemaining==0&&Mode->TotalSerumShots==SerumShotsBefore);
        Observe(TEXT("invalid_muzzle_rejected"));
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BioSerumRangeFixture),true,Mode->Player);TArray<AActor*> Attached;Mode->Player->GetAttachedActors(Attached,true,true);Query.AddIgnoredActors(Attached);
        FVector RangeDirection=FVector::UpVector;bool Clear=false;
        // The combat lane can be under an authored roof. Look for a real clear
        // range ray, then a normal source spawn if necessary; geometry remains
        // active and every tested socket/ray is recorded, including obstructions.
        for(int32 Position=-1;Position<Mode->Model.GetParticipants().Num()&&!Clear;++Position)
        {
            if(Position<0&&!Has)continue;
            if(Position>=0)
            {
                if(!PlaceActor(Mode->Player,Mode->SpawnLocation(Position)))continue;
                Gun->TickAnimation(0,false);Gun->RefreshBoneTransforms();Gun->UpdateComponentToWorld();
                if(!Mode->GetParticipantSerumMuzzle(0,Muzzle))continue;
            }
            for(float Pitch:{90.f,70.f,45.f})for(int32 Yaw=0;Yaw<8&&!Clear;++Yaw)
            {
                if(Pitch==90.f&&Yaw>0)break;
                const FVector Direction=FRotator(Pitch,Yaw*45.f,0).Vector();FHitResult Hit;
                const bool Blocked=GetWorld()->SweepSingleByChannel(Hit,Muzzle,Muzzle+Direction*(Definition->SecondaryRange+10.f),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(Definition->SerumProjectileRadius),Query);
                auto Ray=MakeShared<FJsonObject>();Ray->SetStringField(TEXT("kind"),TEXT("serum_actual_range_lane"));Ray->SetNumberField(TEXT("source_spawn"),Position);Ray->SetStringField(TEXT("actual_muzzle"),Muzzle.ToString());Ray->SetStringField(TEXT("direction"),Direction.ToString());Ray->SetBoolField(TEXT("blocked"),Blocked);Ray->SetStringField(TEXT("blocking_component"),GetPathNameSafe(Hit.GetComponent()));Ray->SetStringField(TEXT("impact"),Hit.ImpactPoint.ToString());Observations.Add(MakeShared<FJsonValueObject>(Ray));
                if(!Blocked){Clear=true;RangeDirection=Direction;}
            }
        }
        // Range exhaustion is an isolated projectile check. If interior spawn
        // lanes are roofed, use the actual topmost original static surface at a
        // source spawn's XY. This explicit fixture placement does not count as
        // reachable gameplay, platform travel, or a natural-round action.
        for(int32 Position=0;Position<Mode->Model.GetParticipants().Num()&&!Clear;++Position)
        {
            const FVector SourceSpawn=Mode->SpawnLocation(Position);FHitResult Roof;
            if(!GetWorld()->LineTraceSingleByObjectType(Roof,FVector(SourceSpawn.X,SourceSpawn.Y,100000),FVector(SourceSpawn.X,SourceSpawn.Y,-1000),FCollisionObjectQueryParams(ECC_WorldStatic),Query)||Roof.ImpactNormal.Z<.95f)continue;
            if(!PlaceActor(Mode->Player,Roof.ImpactPoint+FVector(0,0,Mode->Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2)))continue;
            Gun->TickAnimation(0,false);Gun->RefreshBoneTransforms();Gun->UpdateComponentToWorld();
            if(!Mode->GetParticipantSerumMuzzle(0,Muzzle))continue;
            FHitResult Hit;const bool Blocked=GetWorld()->SweepSingleByChannel(Hit,Muzzle,Muzzle+FVector::UpVector*(Definition->SecondaryRange+10.f),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(Definition->SerumProjectileRadius),Query);
            auto Ray=MakeShared<FJsonObject>();Ray->SetStringField(TEXT("kind"),TEXT("serum_actual_range_lane"));Ray->SetNumberField(TEXT("source_spawn"),Position);Ray->SetBoolField(TEXT("explicit_topmost_original_surface_fixture"),true);Ray->SetStringField(TEXT("original_support"),GetPathNameSafe(Roof.GetComponent()));Ray->SetStringField(TEXT("original_support_point"),Roof.ImpactPoint.ToString());Ray->SetStringField(TEXT("actual_muzzle"),Muzzle.ToString());Ray->SetStringField(TEXT("direction"),FVector::UpVector.ToString());Ray->SetBoolField(TEXT("blocked"),Blocked);Ray->SetStringField(TEXT("blocking_component"),GetPathNameSafe(Hit.GetComponent()));Ray->SetStringField(TEXT("impact"),Hit.ImpactPoint.ToString());Observations.Add(MakeShared<FJsonValueObject>(Ray));
            if(!Blocked){Clear=true;RangeDirection=FVector::UpVector;}
        }
        Check(TEXT("serum_range_fixture_has_actual_clear_original_scene_ray"),Clear);
        if(!Clear){Finish(TEXT("Original scene/source spawns have no unobstructed serum range lane"));return;}
        SerumCancelledBefore=Mode->TotalSerumCancelled;SerumEffectsBefore=Mode->TotalSerumCures+Mode->TotalSerumHeals;
        Check(TEXT("serum_real_range_projectile_fires_from_actual_source_socket"),Mode->FireParticipantSerum(0,Muzzle,RangeDirection));
        Next=Now+Definition->SecondaryRange/Definition->SerumProjectileSpeed+.5;Stage=261;break;
    }
    case 261:
    {
        Check(TEXT("serum_actual_physical_range_exhaustion_cancels_without_effect"),Mode->TotalSerumCancelled==SerumCancelledBefore+1&&Mode->LastSerumDiagnostic.Contains(TEXT("range_exhausted"))&&Mode->TotalSerumCures+Mode->TotalSerumHeals==SerumEffectsBefore);
        Observe(TEXT("physical_range_exhausted"));FinishCombatFixture(Now);break;
    }
    default:Finish(TEXT("Invalid serum fixture stage"));break;
    }
}
