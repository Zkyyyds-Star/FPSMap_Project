#include "MCP/HeliosCodexMCPSubsystem.h"
#include "MCP/HeliosMCPTransport.h"
#include "MCP/HeliosMCPJson.h"
#include "Control/HeliosAutonomyComponent.h"
#include "Control/HeliosControlSubsystem.h"
#include "Player/HeliosArenaCharacter.h"
#include "Adventure/HeliosAdventureDirector.h"
#include "Town/HeliosTownSubsystem.h"
#include "World/HeliosMapDefinition.h"
#include "HeliosOutpost.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

using namespace HeliosMCP;
namespace
{
bool ValidId(const FString& S)
{
    if(S.IsEmpty()||S.Len()>64) return false;
    for(TCHAR C:S) if(!FChar::IsAlnum(C)&&C!=TEXT('_')&&C!=TEXT('-')) return false;
    return true;
}
double UtcSeconds() { return (FDateTime::UtcNow()-FDateTime(1970,1,1)).GetTotalSeconds(); }
}
bool UHeliosCodexMCPSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    return Super::ShouldCreateSubsystem(Outer)&&FParse::Param(FCommandLine::Get(),TEXT("HeliosCodexMCP"));
}
void UHeliosCodexMCPSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    SessionId=FGuid::NewGuid().ToString(EGuidFormats::Digits); Token=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    bDiagnostics=FParse::Param(FCommandLine::Get(),TEXT("HeliosMCPDiagnostics"));
    DiscoveryPath=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("CodexMCP/v2/sessions"),SessionId+TEXT(".json"));
    Transport=new FHeliosMCPTransport();
    if(!Transport->Start()) { delete Transport; Transport=nullptr; UE_LOG(LogHeliosOutpost,Error,TEXT("HELIOS_MCP_V2 listen failed")); return; }
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UHeliosCodexMCPSubsystem::Tick));
    WriteDiscovery(true);
    UE_LOG(LogHeliosOutpost,Display,TEXT("HELIOS_MCP_V2 ready session=%s pid=%u port=%d"),*SessionId,FPlatformProcess::GetCurrentProcessId(),Transport->Port);
}
void UHeliosCodexMCPSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); RevokeControl(TEXT("shutdown")); WriteDiscovery(false);
    delete Transport; Transport=nullptr; Super::Deinitialize();
}
void UHeliosCodexMCPSubsystem::WriteDiscovery(bool Ready)
{
    if(!Transport) return;
    auto Out=Object(); Out->SetNumberField(TEXT("protocol_version"),2); Out->SetStringField(TEXT("session_id"),SessionId);
    Out->SetStringField(TEXT("host"),TEXT("127.0.0.1")); Out->SetNumberField(TEXT("port"),Transport->Port);
    Out->SetNumberField(TEXT("pid"),FPlatformProcess::GetCurrentProcessId()); Out->SetStringField(TEXT("token"),Token);
    Out->SetBoolField(TEXT("ready"),Ready); Out->SetNumberField(TEXT("updated_at"),UtcSeconds());
    Out->SetStringField(TEXT("map"),GetWorld()?GetWorld()->GetMapName():TEXT(""));
    const auto* Town=GetWorld()?GetWorld()->GetSubsystem<UHeliosTownSubsystem>():nullptr;const bool IsTown=Town&&Town->IsTownWorld();const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld());
    Out->SetStringField(TEXT("map_id"),Map?Map->Id.ToString():FString());
    Out->SetStringField(TEXT("map_display_name"),Map?Map->DisplayName.ToString():FString());
    Out->SetBoolField(TEXT("world_ready"),Ready&&GetWorld()&&Autonomy.IsValid()&&(!IsTown||Town->IsWorldReady()));
    Out->SetStringField(TEXT("navigation_scope"),Map?Map->NavigationScope.ToString():FString());
    if(Map){Out->SetNumberField(TEXT("district_count"),Map->DistrictCount);Out->SetStringField(TEXT("gameplay_rules"),Map->GameplayRules.ToString());}
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(DiscoveryPath),true);
    const FString Temp=DiscoveryPath+TEXT(".tmp");
    if(!FFileHelper::SaveStringToFile(Text(Out),*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||
        !IFileManager::Get().Move(*DiscoveryPath,*Temp,true,true,false,true))
        UE_LOG(LogHeliosOutpost,Warning,TEXT("HELIOS_MCP_V2 discovery write failed"));
}
void UHeliosCodexMCPSubsystem::RevokeControl(const FString& Reason)
{
    const bool HadControl=OwnerPeer!=0||(Autonomy.IsValid()&&Autonomy->IsActive());
    ++Epoch; OwnerPeer=0; OwnerId.Reset(); ActiveRequestKey.Reset();
    // Ordinary human movement must not cancel ordinary human firing/reloading.
    if(!HadControl&&Reason==TEXT("manual_input")) return;
    if(Autonomy.IsValid()) Autonomy->Cancel(Reason);
    if(auto* World=GetWorld())
        if(auto* Control=World->GetSubsystem<UHeliosControlSubsystem>()) Control->Stop(TEXT("player_01"));
}
void UHeliosCodexMCPSubsystem::UpdateWorld()
{
    UWorld* World=GetWorld();
    if(!World||!World->IsGameWorld()) return;
    auto* Adventure=AHeliosAdventureDirector::Find(World);
    const int32 Serial=Adventure?Adventure->GetRunSerial():-1;
    if(ObservedWorld.Get()!=World)
    {
        RevokeControl(TEXT("world_changed")); ObservedWorld=World; Autonomy.Reset(); ++WorldSerial; LastRunSerial=-2;
    }
    if(auto* Player=Cast<AHeliosArenaCharacter>(UGameplayStatics::GetPlayerCharacter(World,0)))
    {
        if(!Autonomy.IsValid()||Autonomy->GetOwner()!=Player)
        {
            RevokeControl(TEXT("pawn_changed"));
            auto* Component=Player->FindComponentByClass<UHeliosAutonomyComponent>();
            if(!Component) { Component=NewObject<UHeliosAutonomyComponent>(Player); Player->AddInstanceComponent(Component); Component->RegisterComponent(); }
            Autonomy=Component;
        }
        if(OwnerPeer&&(Player->IsDead()||World->IsPaused())) RevokeControl(Player->IsDead()?TEXT("dead"):TEXT("paused"));
    }
    if(LastRunSerial!=Serial)
    {
        RevokeControl(TEXT("run_changed")); LastRunSerial=Serial; RunId=FString::Printf(TEXT("%d:%d"),WorldSerial,Serial);
        if(Autonomy.IsValid()) Autonomy->ResetMemory();
    }
}
bool UHeliosCodexMCPSubsystem::Tick(float Delta)
{
    if(!Transport) return false;
    UpdateWorld(); const double Now=FPlatformTime::Seconds();
    if(OwnerPeer&&(!Clients.Contains(OwnerPeer)||Now-Clients[OwnerPeer].Heartbeat>=3)) RevokeControl(TEXT("connection_lost"));
    if(Now>=NextDiscovery) { WriteDiscovery(true); NextDiscovery=Now+1; }
    for(auto It=Cache.CreateIterator();It;++It) if(Now-It.Value().At>120) It.RemoveCurrent();
    for(auto It=Cancelled.CreateIterator();It;++It) if(Now-It.Value()>120) It.RemoveCurrent();
    TArray<FHeliosMCPPacket> Batch; FHeliosMCPPacket Packet;
    while(Batch.Num()<256&&Transport->Receive(Packet)) Batch.Add(MoveTemp(Packet));
    // Stable priority: stop/cancel/disconnect fence every older queued operation in this batch.
    Batch.StableSort([](const auto& A,const auto& B)
    {
        auto Priority=[](const auto& P) { const FString C=String(P.Message,TEXT("command")); return P.bDisconnected||C==TEXT("stop")||C==TEXT("cancel_request"); };
        return Priority(A)&&!Priority(B);
    });
    for(const auto& Item:Batch) Process(Item);
    return true;
}
void UHeliosCodexMCPSubsystem::Reply(uint64 Peer,const FString& RequestId,const TSharedPtr<FJsonObject>& Payload)
{
    auto Out=Payload?Parse(Text(Payload)):Result(false,TEXT("invalid_response"));
    Out->SetNumberField(TEXT("protocol_version"),2); Out->SetStringField(TEXT("request_id"),RequestId);
    Out->SetStringField(TEXT("session_id"),SessionId); Out->SetStringField(TEXT("run_id"),RunId);
    Out->SetNumberField(TEXT("control_epoch"),Epoch); Transport->Send(Peer,Out);
}
void UHeliosCodexMCPSubsystem::Process(const FHeliosMCPPacket& Packet)
{
    if(Packet.bDisconnected) { if(OwnerPeer==Packet.Peer) RevokeControl(TEXT("connection_lost")); Clients.Remove(Packet.Peer); return; }
    const auto& Req=Packet.Message; const FString Id=String(Req,TEXT("request_id"));
    auto Error=[&](const TCHAR* Code,const TCHAR* Message=TEXT("")){ Reply(Packet.Peer,Id,Result(false,Code,Message)); };
    if(!Req) { Error(TEXT("invalid_json")); return; }
    double Version=0; if(!Req->TryGetNumberField(TEXT("protocol_version"),Version)||Version!=2||!ValidId(Id)) { Error(TEXT("invalid_request")); return; }
    if(String(Req,TEXT("session_id"))!=SessionId) { Error(TEXT("stale_session")); return; }
    const FString Command=String(Req,TEXT("command")),ClientId=String(Req,TEXT("client_id"));
    const TSharedPtr<FJsonObject>* ParamsPtr=nullptr;
    if(!Req->TryGetObjectField(TEXT("params"),ParamsPtr)||!ValidId(ClientId)) { Error(TEXT("invalid_request")); return; }
    const auto Params=*ParamsPtr;
    if(Command==TEXT("hello"))
    {
        if(String(Params,TEXT("token"))!=Token) { Error(TEXT("unauthorized")); return; }
        if(Clients.Contains(Packet.Peer)&&Clients[Packet.Peer].Id!=ClientId) { Error(TEXT("client_mismatch")); return; }
        Clients.Add(Packet.Peer,{ClientId,FPlatformTime::Seconds()}); Reply(Packet.Peer,Id,Result()); return;
    }
    auto* Client=Clients.Find(Packet.Peer);
    if(!Client||Client->Id!=ClientId) { Error(TEXT("unauthorized")); return; }
    if(Command==TEXT("heartbeat")) { Client->Heartbeat=FPlatformTime::Seconds(); Reply(Packet.Peer,Id,Result()); return; }
    const FString Key=ClientId+TEXT(":")+Id;
    if(Command==TEXT("get_request_result"))
    {
        if(auto* Previous=Cache.Find(ClientId+TEXT(":")+String(Params,TEXT("request_id"))))
        { auto Out=Result(); Out->SetObjectField(TEXT("original_result"),Previous->Result); Reply(Packet.Peer,Id,Out); }
        else Error(TEXT("request_not_found"),TEXT("No retained result; do not assume the operation ran."));
        return;
    }
    if(Command==TEXT("stop")) { RevokeControl(TEXT("stop")); Reply(Packet.Peer,Id,Result()); return; }
    if(Command==TEXT("cancel_request"))
    {
        const FString CancelKey=ClientId+TEXT(":")+String(Params,TEXT("request_id"));
        Cancelled.Add(CancelKey,FPlatformTime::Seconds());
        if(CancelKey==ActiveRequestKey&&OwnerPeer==Packet.Peer) RevokeControl(TEXT("request_cancelled"));
        Reply(Packet.Peer,Id,Result()); return;
    }
    if(Command==TEXT("qa"))
    {
#if WITH_DEV_AUTOMATION_TESTS
        if(FParse::Param(FCommandLine::Get(),TEXT("HeliosMCPQA"))) { Reply(Packet.Peer,Id,RunQA(Params)); return; }
#endif
        Error(TEXT("qa_disabled")); return;
    }
    const bool ReadOnly=Command==TEXT("get_connection_status")||Command==TEXT("get_observation")||Command==TEXT("get_available_weapons")||Command==TEXT("get_available_characters")||Command==TEXT("get_goal_status")||Command==TEXT("capture_frame");
    const bool Diagnostic=Command==TEXT("move")||Command==TEXT("look")||Command==TEXT("fire")||Command==TEXT("reload")||Command==TEXT("jump");
    if(!ReadOnly&&Command!=TEXT("start_adventure")&&Command!=TEXT("restart_adventure")&&Command!=TEXT("set_goal")&&!Diagnostic)
    { Error(TEXT("invalid_command")); return; }
    if(Diagnostic&&!bDiagnostics) { Error(TEXT("diagnostics_disabled")); return; }
    const FString Signature=Command+TEXT(":")+Text(Params);
    if(!ReadOnly)
    {
        if(const auto* Previous=Cache.Find(Key))
        {
            if(Previous->Signature!=Signature) Error(TEXT("request_id_conflict")); else Reply(Packet.Peer,Id,Previous->Result);
            return;
        }
        if(Cancelled.Contains(Key)) { Error(TEXT("request_cancelled")); return; }
        if(CompletedRequestIds.Contains(Key)) { Error(TEXT("result_expired"),TEXT("This request already ran; its result is no longer retained.")); return; }
    }
    double Issued=0,Timeout=0;
    if(!Req->TryGetNumberField(TEXT("issued_at"),Issued)||!Req->TryGetNumberField(TEXT("timeout_ms"),Timeout)||!FMath::IsFinite(Issued)||!FMath::IsFinite(Timeout)||Timeout<=0||Timeout>15000||Issued>UtcSeconds()+1||Issued+Timeout/1000<=UtcSeconds())
    { Error(TEXT("request_expired")); return; }
    if(!ReadOnly)
    {
        double RequestEpoch=-1;
        if(String(Req,TEXT("run_id"))!=RunId) { Error(TEXT("stale_run")); return; }
        if(!Req->TryGetNumberField(TEXT("control_epoch"),RequestEpoch)||RequestEpoch!=Epoch) { Error(TEXT("control_changed"),TEXT("Observe again before issuing a new command.")); return; }
        if(OwnerPeer&&OwnerPeer!=Packet.Peer) { Error(TEXT("control_busy"),TEXT("Another client owns this game.")); return; }
        if(Cache.Num()>=4096||CompletedRequestIds.Num()>=65536) { Error(TEXT("server_busy")); return; }
    }
    auto Output=Execute(Command,Params); UpdateWorld();
    if(!Output) Output=Result(false,TEXT("invalid_response"));
    bool OK=false; Output->TryGetBoolField(TEXT("ok"),OK);
    if(!ReadOnly)
    {
        if(OK&&(Command==TEXT("set_goal")||Diagnostic))
        { OwnerPeer=Packet.Peer; OwnerId=ClientId; ActiveRequestKey=Key; Client->Heartbeat=FPlatformTime::Seconds(); }
        Cache.Add(Key,{Signature,Parse(Text(Output)),FPlatformTime::Seconds()});
        CompletedRequestIds.Add(Key);
    }
    Reply(Packet.Peer,Id,Output);
}
TSharedPtr<FJsonObject> UHeliosCodexMCPSubsystem::Execute(const FString& Command,const TSharedPtr<FJsonObject>& Params)
{
    if(Command==TEXT("get_connection_status"))
    {
        auto Out=Result(); Out->SetBoolField(TEXT("connected"),true); Out->SetBoolField(TEXT("service_ready"),true);
        const auto* Town=GetWorld()?GetWorld()->GetSubsystem<UHeliosTownSubsystem>():nullptr;const bool IsTown=Town&&Town->IsTownWorld();const auto* Map=FHeliosMapRegistry::ForWorld(GetWorld());
        const bool WorldReady=Autonomy.IsValid()&&AHeliosAdventureDirector::Find(GetWorld())&&(!IsTown||Town->IsWorldReady());
        Out->SetBoolField(TEXT("game_ready"),WorldReady);Out->SetBoolField(TEXT("world_ready"),WorldReady);
        Out->SetStringField(TEXT("map_id"),Map?Map->Id.ToString():FString());
        Out->SetStringField(TEXT("map_display_name"),Map?Map->DisplayName.ToString():FString());
        Out->SetStringField(TEXT("navigation_scope"),Map?Map->NavigationScope.ToString():FString());
    if(Map){Out->SetNumberField(TEXT("district_count"),Map->DistrictCount);Out->SetStringField(TEXT("gameplay_rules"),Map->GameplayRules.ToString());}
        Out->SetBoolField(TEXT("paused"),GetWorld()&&GetWorld()->IsPaused()); Out->SetStringField(TEXT("owner_client_id"),OwnerId);
        Out->SetStringField(TEXT("map"),GetWorld()?GetWorld()->GetMapName():TEXT("")); Out->SetNumberField(TEXT("pid"),FPlatformProcess::GetCurrentProcessId());
        return Out;
    }
    if(!Autonomy.IsValid()||!GetWorld()) return Result(false,TEXT("world_unavailable"));
    if(Command==TEXT("capture_frame"))
    {
        auto* ViewportClient=GetGameInstance()->GetGameViewportClient();
        if(FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))||!ViewportClient||!ViewportClient->Viewport)
            return Result(false,TEXT("screenshot_unavailable"),TEXT("This game has no rendered viewport."));
        TArray<FColor> Pixels; FIntPoint Size=ViewportClient->Viewport->GetSizeXY(); bool Captured=false;
        // Read the game's renderer, never the OS desktop (which may be covered by another app).
        if(FSlateApplication::IsInitialized()&&ViewportClient->GetWindow().IsValid())
        {
            FIntVector SlateSize;
            Captured=FSlateApplication::Get().TakeScreenshot(ViewportClient->GetWindow().ToSharedRef(),Pixels,SlateSize);
            if(Captured) Size=FIntPoint(SlateSize.X,SlateSize.Y);
        }
        if(!Captured) Captured=ViewportClient->Viewport->ReadPixels(Pixels);
        if(!Captured||Size.X<=0||Size.Y<=0||Pixels.Num()!=Size.X*Size.Y)
            return Result(false,TEXT("screenshot_unavailable"),TEXT("The game renderer could not capture a frame."));
        const double CapturedAt=UtcSeconds();
        FIntPoint OutputSize=FIntPoint::ZeroValue; TArray64<uint8> PNG;
        for(int32 MaxDimension : {960,640,320})
        {
            const double Scale=FMath::Min(1.0,static_cast<double>(MaxDimension)/FMath::Max(Size.X,Size.Y));
            OutputSize=FIntPoint(FMath::Max(1,FMath::RoundToInt(Size.X*Scale)),FMath::Max(1,FMath::RoundToInt(Size.Y*Scale)));
            TArray<FColor> Small; FImageUtils::ImageResize(Size.X,Size.Y,Pixels,OutputSize.X,OutputSize.Y,Small,true);
            PNG.Reset(); FImageUtils::PNGCompressImageArray(OutputSize.X,OutputSize.Y,Small,PNG);
            if(PNG.Num()>0&&PNG.Num()<=700000) break;
        }
        if(PNG.IsEmpty()||PNG.Num()>700000) return Result(false,TEXT("screenshot_too_large"),TEXT("Frame exceeds the bounded image size."));
        auto Out=Result(); Out->SetStringField(TEXT("image_base64"),FBase64::Encode(PNG.GetData(),static_cast<uint32>(PNG.Num())));
        Out->SetBoolField(TEXT("screenshot_available"),true); Out->SetNumberField(TEXT("screenshot_pid"),FPlatformProcess::GetCurrentProcessId());
        Out->SetStringField(TEXT("screenshot_source"),TEXT("unreal_render_target"));
        Out->SetNumberField(TEXT("screenshot_frame_id"),static_cast<double>(GFrameCounter));
        Out->SetNumberField(TEXT("screenshot_width"),OutputSize.X); Out->SetNumberField(TEXT("screenshot_height"),OutputSize.Y);
        Out->SetNumberField(TEXT("screenshot_captured_at"),CapturedAt); return Out;
    }
    if(Command==TEXT("get_observation")) return Autonomy->Observation();
    if(Command==TEXT("get_goal_status")) return Autonomy->GoalStatus(String(Params,TEXT("goal_id")));
    if(Command==TEXT("set_goal")) return Autonomy->SetGoal(Params);
    if(Command!=TEXT("get_available_weapons")&&Command!=TEXT("get_available_characters")) Autonomy->Cancel(TEXT("manual_command"));
    auto* Control=GetWorld()->GetSubsystem<UHeliosControlSubsystem>();
    return Control?Parse(Control->DispatchLocalCommand(Command,TEXT("player_01"),Text(Params))):Result(false,TEXT("world_unavailable"));
}
