#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "HeliosCodexMCPSubsystem.generated.h"
class UHeliosAutonomyComponent;
class FJsonObject;
class FHeliosMCPTransport;
struct FHeliosMCPPacket;

/** Opt-in v2 loopback service. GameInstance lifetime, all commands executed on game thread. */
UCLASS()
class HELIOSOUTPOST_API UHeliosCodexMCPSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void RevokeControl(const FString& Reason);
private:
    bool Tick(float Delta);
    void UpdateWorld();
    void WriteDiscovery(bool Ready);
    void Process(const FHeliosMCPPacket& Packet);
    void Reply(uint64 Peer,const FString& RequestId,const TSharedPtr<FJsonObject>& Result);
    TSharedPtr<FJsonObject> Execute(const FString& Command,const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> RunQA(const TSharedPtr<FJsonObject>& Params);
    struct FClient { FString Id; double Heartbeat=0; };
    struct FCached { FString Signature; TSharedPtr<FJsonObject> Result; double At=0; };
    TMap<uint64,FClient> Clients;
    TMap<FString,FCached> Cache;
    TSet<FString> CompletedRequestIds;
    TMap<FString,double> Cancelled;
    FHeliosMCPTransport* Transport=nullptr;
    FTSTicker::FDelegateHandle TickHandle;
    TWeakObjectPtr<UWorld> ObservedWorld;
    TWeakObjectPtr<UHeliosAutonomyComponent> Autonomy;
    FString SessionId,Token,RunId,DiscoveryPath,OwnerId,ActiveRequestKey;
    uint64 OwnerPeer=0;
    int32 Epoch=0, WorldSerial=0, LastRunSerial=-1;
    double NextDiscovery=0;
    bool bDiagnostics=false;
};
