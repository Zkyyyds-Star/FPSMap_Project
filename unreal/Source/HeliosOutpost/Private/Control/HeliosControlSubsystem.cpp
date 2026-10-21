#include "Control/HeliosControlSubsystem.h"
#include "Player/HeliosCharacter.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosCharacterDefinition.h"
#include "Enemies/HeliosEnemyActor.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Weapons/HeliosWeaponDefinition.h"
#include "Weapons/HeliosWeaponRegistry.h"
#include "Town/HeliosTownSubsystem.h"
#include "World/HeliosMapTravelSubsystem.h"
#include "World/HeliosMapDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "World/HeliosZone.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "MCP/HeliosCodexMCPSubsystem.h"
#include "Control/HeliosAutonomyComponent.h"
#include "Engine/GameInstance.h"

void UHeliosControlSubsystem::Tick(float DeltaTime)
{
    if (!bAutomationActive)
    {
        return;
    }

    AHeliosCharacter* Character = AutomatedCharacter.Get();
    if (!Character || !Character->GetController())
    {
        StopAutomation(TEXT("actor_unavailable"), false);
        return;
    }

    Character->SetSprintingFromAutomation(bAutomatedSprint);
    Character->AddMovementInput(Character->GetActorRightVector(), AutomatedAxes.X);
    Character->AddMovementInput(Character->GetActorForwardVector(), AutomatedAxes.Y);

    RemainingSeconds -= DeltaTime;
    if (RemainingSeconds <= 0.0f)
    {
        StopAutomation(TEXT("duration_complete"), false);
    }
}

TStatId UHeliosControlSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UHeliosControlSubsystem, STATGROUP_Tickables);
}

bool UHeliosControlSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FHeliosControlResult UHeliosControlSubsystem::GetState(const FString& ActorId, FHeliosActorState& OutState) const
{
    if (!IsInGameThread())
    {
        return Failure(TEXT("wrong_thread"), TEXT("Control commands must execute on the Unreal game thread."));
    }

    AHeliosCharacter* Character = FindCharacter(ActorId);
    if (!Character)
    {
        return Failure(TEXT("invalid_actor"), FString::Printf(TEXT("Unknown actor '%s'."), *ActorId));
    }

    OutState.ActorId = Character->ActorId;
    OutState.LocationCm = Character->GetActorLocation();
    OutState.RotationDegrees = Character->GetControlRotation();
    OutState.VelocityCmPerSecond = Character->GetVelocity();
    OutState.bGrounded = Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsMovingOnGround();
    OutState.bAutomationActive = bAutomationActive && AutomatedCharacter.Get() == Character;
    OutState.AutomationRemainingSeconds = OutState.bAutomationActive ? FMath::Max(0.0f, RemainingSeconds) : 0.0f;
    OutState.LastStopReason = LastStopReason;

    double SmallestZoneVolume = TNumericLimits<double>::Max();
    for (TActorIterator<AHeliosZone> It(GetWorld()); It; ++It)
    {
        const double Volume = It->GetBoundsVolume();
        if (It->ContainsWorldLocation(OutState.LocationCm) && Volume < SmallestZoneVolume)
        {
            OutState.CurrentZoneId = It->ZoneId;
            SmallestZoneVolume = Volume;
        }
    }

    return Success(TEXT("State returned."));
}

FHeliosControlResult UHeliosControlSubsystem::Fire(const FString& ActorId, float DurationSeconds)
{
    if (!IsInGameThread()) return Failure(TEXT("wrong_thread"), TEXT("Control commands must execute on the Unreal game thread."));
    AHeliosArenaCharacter* Character = Cast<AHeliosArenaCharacter>(FindCharacter(ActorId));
    if (!Character) return Failure(TEXT("invalid_actor"), FString::Printf(TEXT("Unknown actor '%s'."), *ActorId));
    FString ErrorCode;
    if (!ValidateDuration(DurationSeconds, ErrorCode)) return Failure(ErrorCode, TEXT("Fire duration must be in (0,3]."));
    if (Character->IsDead() || GetWorld()->IsPaused()) return Failure(TEXT("actor_unavailable"), TEXT("Actor is dead or paused."));
    GetWorld()->GetTimerManager().ClearTimer(FireStopTimer);
    FireCharacter = Character;
    Character->StartFireFromAutomation();
    if (GetWorld())
    {
        TWeakObjectPtr<AHeliosArenaCharacter> WeakCharacter(Character);
        GetWorld()->GetTimerManager().SetTimer(FireStopTimer, [WeakCharacter]()
        {
            if (WeakCharacter.IsValid()) WeakCharacter->StopFireFromAutomation();
        }, DurationSeconds, false);
    }
    return Success(TEXT("Fire accepted."));
}

FHeliosControlResult UHeliosControlSubsystem::Reload(const FString& ActorId)
{
    if (!IsInGameThread()) return Failure(TEXT("wrong_thread"), TEXT("Control commands must execute on the Unreal game thread."));
    AHeliosArenaCharacter* Character = Cast<AHeliosArenaCharacter>(FindCharacter(ActorId));
    if (!Character) return Failure(TEXT("invalid_actor"), FString::Printf(TEXT("Unknown actor '%s'."), *ActorId));
    if (Character->IsDead() || GetWorld()->IsPaused()) return Failure(TEXT("actor_unavailable"), TEXT("Actor is dead or paused."));
    FireCharacter = Character;
    GetWorld()->GetTimerManager().ClearTimer(FireStopTimer);
    Character->StartReloadFromAutomation();
    return Success(TEXT("Reload requested."));
}

FHeliosControlResult UHeliosControlSubsystem::Jump(const FString& ActorId)
{
    if (!IsInGameThread()) return Failure(TEXT("wrong_thread"), TEXT("Control commands must execute on the Unreal game thread."));
    AHeliosArenaCharacter* Character = Cast<AHeliosArenaCharacter>(FindCharacter(ActorId));
    if (!Character) return Failure(TEXT("invalid_actor"), FString::Printf(TEXT("Unknown actor '%s'."), *ActorId));
    if (Character->IsDead() || GetWorld()->IsPaused()) return Failure(TEXT("actor_unavailable"), TEXT("Actor is dead or paused."));
    FireCharacter = Character;
    Character->JumpFromAutomation();
    TWeakObjectPtr<AHeliosArenaCharacter> WeakCharacter(Character);
    GetWorld()->GetTimerManager().SetTimer(JumpStopTimer, [WeakCharacter]() { if (WeakCharacter.IsValid()) WeakCharacter->StopJumping(); }, 0.15f, false);
    return Success(TEXT("Jump requested."));
}

FHeliosControlResult UHeliosControlSubsystem::Move(const FString& ActorId, FVector2D LocalAxes, float DurationSeconds, bool bSprint)
{
    if (!IsInGameThread())
    {
        return Failure(TEXT("wrong_thread"), TEXT("Control commands must execute on the Unreal game thread."));
    }

    AHeliosCharacter* Character = FindCharacter(ActorId);
    if (!Character)
    {
        return Failure(TEXT("invalid_actor"), FString::Printf(TEXT("Unknown actor '%s'."), *ActorId));
    }

    if (Character->ActorHasTag(TEXT("Dead")) || UGameplayStatics::IsGamePaused(GetWorld()))
    {
        return Failure(TEXT("actor_unavailable"), TEXT("Actor cannot be controlled while dead or paused."));
    }

    if (!FMath::IsFinite(LocalAxes.X) || !FMath::IsFinite(LocalAxes.Y))
    {
        return Failure(TEXT("invalid_command"), TEXT("Move axes must be finite."));
    }

    FString ErrorCode;
    if (!ValidateDuration(DurationSeconds, ErrorCode))
    {
        return Failure(ErrorCode, TEXT("Duration must be finite, greater than zero, and no more than 3 seconds."));
    }

    LocalAxes = ClampMoveAxes(LocalAxes);
    if (LocalAxes.IsNearlyZero())
    {
        return Failure(TEXT("invalid_command"), TEXT("Move axes cannot both be zero."));
    }

    StopAutomation(TEXT("replaced"), false);
    AutomatedCharacter = Character;
    AutomatedAxes = LocalAxes;
    RemainingSeconds = DurationSeconds;
    bAutomatedSprint = bSprint;
    bAutomationActive = true;
    LastStopReason = TEXT("active");
    return Success(TEXT("Move accepted."));
}

FHeliosControlResult UHeliosControlSubsystem::Look(const FString& ActorId, float DeltaYawDegrees, float DeltaPitchDegrees)
{
    if (!IsInGameThread())
    {
        return Failure(TEXT("wrong_thread"), TEXT("Control commands must execute on the Unreal game thread."));
    }

    AHeliosCharacter* Character = FindCharacter(ActorId);
    if (!Character)
    {
        return Failure(TEXT("invalid_actor"), FString::Printf(TEXT("Unknown actor '%s'."), *ActorId));
    }

    if (Character->ActorHasTag(TEXT("Dead")) || UGameplayStatics::IsGamePaused(GetWorld()))
    {
        return Failure(TEXT("actor_unavailable"), TEXT("Actor cannot be controlled while dead or paused."));
    }

    if (!FMath::IsFinite(DeltaYawDegrees) || !FMath::IsFinite(DeltaPitchDegrees))
    {
        return Failure(TEXT("invalid_command"), TEXT("Look deltas must be finite."));
    }

    AController* Controller = Character->GetController();
    if (!Controller)
    {
        return Failure(TEXT("actor_unavailable"), TEXT("Actor is not possessed."));
    }

    const float YawDelta = FMath::Clamp(DeltaYawDegrees, -180.0f, 180.0f);
    const float PitchDelta = FMath::Clamp(DeltaPitchDegrees, -89.0f, 89.0f);
    FRotator Rotation = Controller->GetControlRotation();
    Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + YawDelta);
    Rotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rotation.Pitch + PitchDelta), -89.0f, 89.0f);
    Rotation.Roll = 0.0f;
    Controller->SetControlRotation(Rotation);
    return Success(TEXT("Look applied."));
}

FHeliosControlResult UHeliosControlSubsystem::Stop(const FString& ActorId)
{
    if (!IsInGameThread())
    {
        return Failure(TEXT("wrong_thread"), TEXT("Control commands must execute on the Unreal game thread."));
    }

    AHeliosCharacter* Character = FindCharacter(ActorId);
    if (!Character)
    {
        return Failure(TEXT("invalid_actor"), FString::Printf(TEXT("Unknown actor '%s'."), *ActorId));
    }

    if (AutomatedCharacter.IsValid() && AutomatedCharacter.Get() != Character)
    {
        return Failure(TEXT("invalid_actor"), TEXT("The requested actor is not under automation."));
    }

    AutomatedCharacter = Character;
    StopAutomation(TEXT("stop_command"), true);
    CancelActionTimers();
    if (AHeliosArenaCharacter* Arena = Cast<AHeliosArenaCharacter>(Character)) Arena->CancelAutomationCombat();
    return Success(TEXT("Actor stopped."));
}

FString UHeliosControlSubsystem::DispatchLocalCommand(const FString& Command, const FString& ActorId, const FString& ParametersJson)
{
    if (!IsInGameThread())
    {
        return SerializeDispatchResult(Failure(
            TEXT("wrong_thread"), TEXT("Control commands must execute on the Unreal game thread.")));
    }

    const bool bKnownCommand = Command == TEXT("get_state")
        || Command == TEXT("move")
        || Command == TEXT("look")
        || Command == TEXT("stop")
        || Command == TEXT("get_adventure_state")
        || Command == TEXT("restart_adventure")
        || Command == TEXT("get_available_weapons") || Command == TEXT("get_available_characters")
        || Command == TEXT("start_adventure")
        || Command == TEXT("fire") || Command == TEXT("reload") || Command == TEXT("jump")
        ;
    if (!bKnownCommand)
    {
        return SerializeDispatchResult(Failure(
            TEXT("invalid_command"), FString::Printf(TEXT("Unsupported command '%s'."), *Command)));
    }

    TSharedPtr<FJsonObject> Parameters = MakeShared<FJsonObject>();
    if (!ParametersJson.IsEmpty())
    {
        TSharedPtr<FJsonObject> ParsedParameters;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParametersJson);
        if (!FJsonSerializer::Deserialize(Reader, ParsedParameters) || !ParsedParameters.IsValid())
        {
            return SerializeDispatchResult(Failure(
                TEXT("invalid_json"), TEXT("ParametersJson must be a valid JSON object.")));
        }
        Parameters = ParsedParameters;
    }

    if (Command == TEXT("get_adventure_state") || Command == TEXT("restart_adventure")
        || Command == TEXT("get_available_weapons") || Command == TEXT("get_available_characters") || Command == TEXT("start_adventure"))
    {
        if (!FindCharacter(ActorId)) return SerializeDispatchResult(Failure(TEXT("invalid_actor"), TEXT("Unknown actor.")));
        AHeliosAdventureDirector* Adventure = AHeliosAdventureDirector::Find(GetWorld());
        if (!Adventure) return SerializeDispatchResult(Failure(TEXT("mode_unavailable"), TEXT("Adventure mode is not running.")));
        if (Command == TEXT("get_available_characters"))
        {
            TArray<TSharedPtr<FJsonValue>> Entries;
            for(const auto* D:FHeliosCharacterRegistry::GetAvailableCharacters())
            {
                auto Entry=MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("character_id"),D->CharacterId.ToString());
                Entry->SetStringField(TEXT("name"),D->DisplayName.ToString());
                Entry->SetBoolField(TEXT("presentation_ready"),D->bPresentationReady);
                Entry->SetStringField(TEXT("default_variant_id"),D->DefaultVariantId.ToString());
                TArray<TSharedPtr<FJsonValue>> Forms;
                for(const auto& Form:D->Forms)
                {
                    auto Value=MakeShared<FJsonObject>();
                    Value->SetStringField(TEXT("character_variant_id"),Form.VariantId.ToString());
                    Value->SetStringField(TEXT("name"),Form.DisplayName.ToString());
                    Value->SetBoolField(TEXT("presentation_ready"),Form.bPresentationReady);
                    Forms.Add(MakeShared<FJsonValueObject>(Value));
                }
                Entry->SetArrayField(TEXT("forms"),Forms);
                Entries.Add(MakeShared<FJsonValueObject>(Entry));
            }
            auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("ok"),true);Result->SetArrayField(TEXT("characters"),Entries);
            FString Output;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Output));return Output;
        }
        if (Command == TEXT("get_available_weapons"))
        {
            TArray<TSharedPtr<FJsonValue>> Entries;
            for (const UHeliosWeaponDefinition* Def : Adventure->GetAvailableWeapons())
            {
                if (!Def) continue;
                TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("weapon_id"), Def->WeaponId.ToString());
                Entry->SetStringField(TEXT("name"), Def->DisplayName.ToString());
                Entry->SetStringField(TEXT("description"), Def->Description.ToString());
                Entries.Add(MakeShared<FJsonValueObject>(Entry));
            }
            TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("ok"), true);
            Result->SetArrayField(TEXT("weapons"), Entries);
            FString Output; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Output));
            return Output;
        }
        if (Command == TEXT("start_adventure"))
        {
            FString WeaponId;
            if(!Parameters->TryGetStringField(TEXT("primary_weapon_id"),WeaponId) && !Parameters->TryGetStringField(TEXT("weapon_id"),WeaponId))
                return SerializeDispatchResult(Failure(TEXT("invalid_command"),TEXT("primary_weapon_id or weapon_id is required.")));
            FHeliosLoadout Loadout=Adventure->GetSelectedLoadout();Loadout.PrimaryWeaponId=FName(*WeaponId);
            FString Value;
            if(Parameters->TryGetStringField(TEXT("character_id"),Value))
            {
                const FName Character(*Value);
                if(Character!=Loadout.CharacterId)Loadout.CharacterVariantId=NAME_None;
                Loadout.CharacterId=Character;
            }
            if(Parameters->TryGetStringField(TEXT("character_variant_id"),Value))Loadout.CharacterVariantId=FName(*Value);
            if(Parameters->TryGetStringField(TEXT("secondary_weapon_id"),Value))Loadout.SecondaryWeaponId=FName(*Value);
            if(Parameters->TryGetStringField(TEXT("melee_weapon_id"),Value))Loadout.MeleeWeaponId=FName(*Value);
            FString Error;
            if(!FHeliosLoadout::Validate(Loadout,Error)||Loadout.CharacterId==TEXT("sword_dog"))
                return SerializeDispatchResult(Failure(UHeliosWeaponRegistry::FindWeapon(Loadout.PrimaryWeaponId)?TEXT("invalid_loadout"):TEXT("invalid_weapon"),Error));
            if (!Adventure->IsSelectingWeapon())
                return SerializeDispatchResult(Failure(TEXT("invalid_phase"), TEXT("Weapons can only be selected before a run.")));
            FString MapId;
            if(Parameters->TryGetStringField(TEXT("map_id"),MapId))
            {
                const auto* Definition=FHeliosMapRegistry::Find(FName(*MapId));
                if(!Definition)return SerializeDispatchResult(Failure(TEXT("invalid_map"),TEXT("Unknown map_id.")));
                const auto* CurrentMap=FHeliosMapRegistry::ForWorld(GetWorld());
                if(!CurrentMap || CurrentMap->Id!=Definition->Id)
                {
                    auto* Travel=GetWorld()->GetGameInstance()->GetSubsystem<UHeliosMapTravelSubsystem>();
                    if(!Travel||!Travel->RequestTravelWithLoadout(FName(*MapId),Loadout))return SerializeDispatchResult(Failure(TEXT("map_unavailable"),TEXT("Map asset unavailable.")));
                    return SerializeDispatchResult(Success(TEXT("Travel accepted; observe world_ready before further commands.")));
                }
            }
            if(auto* Town=GetWorld()->GetSubsystem<UHeliosTownSubsystem>())if(Town->IsTownWorld()&&!Town->IsWorldReady())
                return SerializeDispatchResult(Failure(TEXT("world_not_ready"),TEXT("Town streaming or local navigation is not ready.")));
            if (!Adventure->StartRunWithLoadout(Loadout))
                return SerializeDispatchResult(Failure(TEXT("invalid_weapon"), TEXT("Unknown or unavailable weapon.")));
        }
        if (Command == TEXT("restart_adventure")) Adventure->RestartRun();
        return Adventure->GetStateJson();
    }

    if (Command == TEXT("get_state"))
    {
        FHeliosActorState State;
        const FHeliosControlResult Result = GetState(ActorId, State);
        return SerializeDispatchResult(Result, Result.bSuccess ? &State : nullptr);
    }

    if (Command == TEXT("stop"))
    {
        return SerializeDispatchResult(Stop(ActorId));
    }

    if (Command == TEXT("move"))
    {
        double Right = 0.0;
        double Forward = 0.0;
        double DurationSeconds = 0.0;
        if (!Parameters->TryGetNumberField(TEXT("right"), Right)
            || !Parameters->TryGetNumberField(TEXT("forward"), Forward)
            || !Parameters->TryGetNumberField(TEXT("duration_seconds"), DurationSeconds))
        {
            return SerializeDispatchResult(Failure(TEXT("invalid_command"),
                TEXT("move requires numeric right, forward, and duration_seconds fields.")));
        }

        bool bSprint = false;
        if (Parameters->HasField(TEXT("sprint")) && !Parameters->TryGetBoolField(TEXT("sprint"), bSprint))
        {
            return SerializeDispatchResult(Failure(
                TEXT("invalid_command"), TEXT("move sprint must be a boolean.")));
        }
        return SerializeDispatchResult(Move(
            ActorId, FVector2D(Right, Forward), static_cast<float>(DurationSeconds), bSprint));
    }

    if (Command == TEXT("fire"))
    {
        double DurationSeconds = 0.0;
        if (!Parameters->TryGetNumberField(TEXT("duration_seconds"), DurationSeconds))
            return SerializeDispatchResult(Failure(TEXT("invalid_command"), TEXT("fire requires duration_seconds.")));
        return SerializeDispatchResult(Fire(ActorId, static_cast<float>(DurationSeconds)));
    }
    if (Command == TEXT("reload")) return SerializeDispatchResult(Reload(ActorId));
    if (Command == TEXT("jump")) return SerializeDispatchResult(Jump(ActorId));
    double DeltaYawDegrees = 0.0;
    double DeltaPitchDegrees = 0.0;
    if (!Parameters->TryGetNumberField(TEXT("delta_yaw_degrees"), DeltaYawDegrees)
        || !Parameters->TryGetNumberField(TEXT("delta_pitch_degrees"), DeltaPitchDegrees))
    {
        return SerializeDispatchResult(Failure(TEXT("invalid_command"),
            TEXT("look requires numeric delta_yaw_degrees and delta_pitch_degrees fields.")));
    }
    return SerializeDispatchResult(Look(
        ActorId, static_cast<float>(DeltaYawDegrees), static_cast<float>(DeltaPitchDegrees)));
}

void UHeliosControlSubsystem::CancelActionTimers()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(FireStopTimer);
        GetWorld()->GetTimerManager().ClearTimer(JumpStopTimer);
    }
    if (FireCharacter.IsValid()) FireCharacter->CancelAutomationCombat();
    FireCharacter.Reset();
}

void UHeliosControlSubsystem::NotifyManualInput(AHeliosCharacter* Character)
{
    StopAutomation(TEXT("manual_input"), false);
    CancelActionTimers();
    if (auto* World = GetWorld())
        if (auto* Instance = World->GetGameInstance())
            if (auto* Bridge = Instance->GetSubsystem<UHeliosCodexMCPSubsystem>())
                Bridge->RevokeControl(Character && Character->ActorHasTag(TEXT("Dead")) ? TEXT("dead") : TEXT("manual_input"));
}

bool UHeliosControlSubsystem::IsAutonomyActive() const
{
    if (auto* Character = FindCharacter(TEXT("player_01")))
        if (auto* AI = Character->FindComponentByClass<UHeliosAutonomyComponent>()) return AI->IsActive();
    return false;
}

bool UHeliosControlSubsystem::ValidateDuration(float DurationSeconds, FString& OutErrorCode)
{
    if (!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0f || DurationSeconds > 3.0f)
    {
        OutErrorCode = TEXT("invalid_duration");
        return false;
    }
    OutErrorCode.Reset();
    return true;
}

FVector2D UHeliosControlSubsystem::ClampMoveAxes(FVector2D LocalAxes)
{
    LocalAxes.X = FMath::Clamp(LocalAxes.X, -1.0, 1.0);
    LocalAxes.Y = FMath::Clamp(LocalAxes.Y, -1.0, 1.0);
    return LocalAxes.GetClampedToMaxSize(1.0);
}

AHeliosCharacter* UHeliosControlSubsystem::FindCharacter(const FString& ActorId) const
{
    if (!GetWorld() || ActorId.IsEmpty())
    {
        return nullptr;
    }

    for (TActorIterator<AHeliosCharacter> It(GetWorld()); It; ++It)
    {
        if (It->ActorId.Equals(ActorId, ESearchCase::CaseSensitive))
        {
            return *It;
        }
    }
    return nullptr;
}

void UHeliosControlSubsystem::StopAutomation(const FString& Reason, bool bStopVelocity)
{
    if (AHeliosCharacter* Character = AutomatedCharacter.Get())
    {
        Character->SetSprintingFromAutomation(false);
        if (bStopVelocity && Character->GetCharacterMovement())
        {
            Character->ConsumeMovementInputVector();
            Character->GetCharacterMovement()->StopMovementImmediately();
        }
    }

    bAutomationActive = false;
    RemainingSeconds = 0.0f;
    AutomatedAxes = FVector2D::ZeroVector;
    bAutomatedSprint = false;
    LastStopReason = Reason;
    AutomatedCharacter.Reset();
}

FHeliosControlResult UHeliosControlSubsystem::Success(const FString& Message)
{
    FHeliosControlResult Result;
    Result.bSuccess = true;
    Result.Message = Message;
    return Result;
}

FHeliosControlResult UHeliosControlSubsystem::Failure(const FString& Code, const FString& Message)
{
    FHeliosControlResult Result;
    Result.bSuccess = false;
    Result.ErrorCode = Code;
    Result.Message = Message;
    return Result;
}

FString UHeliosControlSubsystem::SerializeDispatchResult(const FHeliosControlResult& Result, const FHeliosActorState* State)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("ok"), Result.bSuccess);
    Root->SetStringField(TEXT("error_code"), Result.ErrorCode);
    Root->SetStringField(TEXT("message"), Result.Message);

    if (State)
    {
        auto VectorJson = [](const FVector& Vector)
        {
            TArray<TSharedPtr<FJsonValue>> Values;
            Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
            Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
            Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
            return Values;
        };

        TSharedPtr<FJsonObject> StateJson = MakeShared<FJsonObject>();
        StateJson->SetStringField(TEXT("actor_id"), State->ActorId);
        StateJson->SetArrayField(TEXT("location_cm"), VectorJson(State->LocationCm));
        StateJson->SetArrayField(TEXT("rotation_degrees"), VectorJson(FVector(
            State->RotationDegrees.Pitch, State->RotationDegrees.Yaw, State->RotationDegrees.Roll)));
        StateJson->SetArrayField(TEXT("velocity_cm_per_second"), VectorJson(State->VelocityCmPerSecond));
        StateJson->SetBoolField(TEXT("grounded"), State->bGrounded);
        StateJson->SetBoolField(TEXT("automation_active"), State->bAutomationActive);
        StateJson->SetNumberField(TEXT("automation_remaining_seconds"), State->AutomationRemainingSeconds);
        StateJson->SetStringField(TEXT("current_zone_id"), State->CurrentZoneId);
        StateJson->SetStringField(TEXT("last_stop_reason"), State->LastStopReason);
        Root->SetObjectField(TEXT("state"), StateJson);
    }

    FString Output;
    FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
    return Output;
}
