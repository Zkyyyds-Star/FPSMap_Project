#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/Queue.h"
#include "Dom/JsonObject.h"

struct FHeliosMCPPacket
{
    uint64 Peer=0;
    FString Text;
    bool bDisconnected=false;
    // Populated only after dequeue on the game thread; JSON shared pointers never cross threads.
    TSharedPtr<FJsonObject> Message;
};

/** Socket worker owns all FSocket objects; never touches gameplay objects. */
class FHeliosMCPTransport : public FRunnable
{
public:
    ~FHeliosMCPTransport();
    bool Start();
    virtual uint32 Run() override;
    virtual void Stop() override { bStop.Store(true); }
    bool Receive(FHeliosMCPPacket& Out);
    void Send(uint64 Peer,const TSharedPtr<FJsonObject>& Message);
    int32 Port=0;
private:
    struct FPeer
    {
        uint64 Id=0;
        class FSocket* Socket=nullptr;
        TArray<uint8> Input,Output;
    };
    void Drop(int32 Index);
    class FSocket* Listener=nullptr;
    class FRunnableThread* Thread=nullptr;
    TArray<FPeer> Peers;
    TQueue<FHeliosMCPPacket,EQueueMode::Spsc> Inbound;
    TQueue<FHeliosMCPPacket,EQueueMode::Spsc> Outbound;
    TAtomic<bool> bStop{false};
    TAtomic<int32> Queued{0};
    uint64 NextPeer=1;
};
