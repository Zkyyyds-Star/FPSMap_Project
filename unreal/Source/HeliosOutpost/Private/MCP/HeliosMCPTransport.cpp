#include "MCP/HeliosMCPTransport.h"
#include "MCP/HeliosMCPJson.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"

bool FHeliosMCPTransport::Start()
{
    auto* System=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    Listener=System->CreateSocket(NAME_Stream,TEXT("HeliosMCPv2"),false);
    if(!Listener) return false;
    auto Address=System->CreateInternetAddr(); bool Valid=false;
    Address->SetIp(TEXT("127.0.0.1"),Valid); Address->SetPort(0);
    if(!Valid||!Listener->SetNonBlocking(true)||!Listener->Bind(*Address)||!Listener->Listen(16)) return false;
    Listener->GetAddress(*Address); Port=Address->GetPort();
    Thread=FRunnableThread::Create(this,TEXT("HeliosMCPSockets")); return Thread!=nullptr;
}
FHeliosMCPTransport::~FHeliosMCPTransport()
{
    Stop(); if(Thread) { Thread->WaitForCompletion(); delete Thread; }
    auto* System=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    for(auto& Peer:Peers) { Peer.Socket->Close(); System->DestroySocket(Peer.Socket); }
    if(Listener) { Listener->Close(); System->DestroySocket(Listener); }
}
bool FHeliosMCPTransport::Receive(FHeliosMCPPacket& Out)
{
    if(!Inbound.Dequeue(Out)) return false; --Queued;
    if(!Out.bDisconnected) Out.Message=HeliosMCP::Parse(Out.Text);
    return true;
}
void FHeliosMCPTransport::Send(uint64 Peer,const TSharedPtr<FJsonObject>& Message)
{
    Outbound.Enqueue({Peer,HeliosMCP::Text(Message),false,nullptr});
}
void FHeliosMCPTransport::Drop(int32 Index)
{
    auto& Peer=Peers[Index]; Inbound.Enqueue({Peer.Id,FString(),true,nullptr}); ++Queued;
    Peer.Socket->Close(); ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Peer.Socket);
    Peers.RemoveAtSwap(Index);
}
uint32 FHeliosMCPTransport::Run()
{
    constexpr int32 MaxFrame=1024*1024;
    auto* System=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    while(!bStop.Load())
    {
        bool Pending=false;
        if(Listener->HasPendingConnection(Pending)&&Pending)
        {
            if(auto* Socket=Listener->Accept(TEXT("HeliosClient")))
            {
                if(Peers.Num()>=16) { Socket->Close(); System->DestroySocket(Socket); }
                else { Socket->SetNonBlocking(true); Socket->SetNoDelay(true); FPeer Peer; Peer.Id=NextPeer++; Peer.Socket=Socket; Peers.Add(MoveTemp(Peer)); }
            }
        }
        FHeliosMCPPacket Reply;
        while(Outbound.Dequeue(Reply))
        {
            auto* Peer=Peers.FindByPredicate([&](const FPeer& P){return P.Id==Reply.Peer;}); if(!Peer) continue;
            FTCHARToUTF8 Bytes(*Reply.Text); const uint32 N=Bytes.Length();
            if(N>MaxFrame||Peer->Output.Num()+static_cast<int32>(N)+4>4*MaxFrame) { Peer->Socket->Close(); continue; }
            const uint8 Header[4]={static_cast<uint8>(N>>24),static_cast<uint8>(N>>16),static_cast<uint8>(N>>8),static_cast<uint8>(N)};
            Peer->Output.Append(Header,4); Peer->Output.Append(reinterpret_cast<const uint8*>(Bytes.Get()),N);
        }
        for(int32 I=Peers.Num()-1;I>=0;--I)
        {
            auto& Peer=Peers[I]; uint8 Buffer[65536]; int32 Read=0;
            const bool Received=Peer.Socket->Recv(Buffer,sizeof(Buffer),Read);
            // UE reports would-block as true with zero bytes; false is EOF/error.
            if(!Received) { Drop(I); continue; }
            if(Read>0) Peer.Input.Append(Buffer,Read);
            bool Bad=Peer.Input.Num()>2*MaxFrame;
            for(int J=0;!Bad&&J<32&&Peer.Input.Num()>=4;++J)
            {
                const uint32 N=(uint32(Peer.Input[0])<<24)|(uint32(Peer.Input[1])<<16)|(uint32(Peer.Input[2])<<8)|Peer.Input[3];
                if(N==0||N>MaxFrame) { Bad=true; break; }
                if(Peer.Input.Num()<static_cast<int32>(N+4)) break;
                FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Peer.Input.GetData()+4),N);
                if(Queued.Load()>=256) { Bad=true; break; }
                Inbound.Enqueue({Peer.Id,FString(Text.Length(),Text.Get()),false,nullptr}); ++Queued; Peer.Input.RemoveAt(0,N+4,EAllowShrinking::No);
            }
            if(Bad) { Drop(I); continue; }
            if(!Peer.Output.IsEmpty())
            {
                int32 Sent=0; Peer.Socket->Send(Peer.Output.GetData(),Peer.Output.Num(),Sent);
                if(Sent>0) Peer.Output.RemoveAt(0,Sent,EAllowShrinking::No);
            }
        }
        FPlatformProcess::Sleep(.005f);
    }
    return 0;
}
