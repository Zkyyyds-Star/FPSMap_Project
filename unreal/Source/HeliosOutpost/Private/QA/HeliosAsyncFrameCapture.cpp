#include "QA/HeliosAsyncFrameCapture.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/SlateViewportProvider.h"
#include "RHIGPUReadback.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "Async/Async.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"

namespace HeliosAsyncFrameCapture
{
struct FSlot
{
    TUniquePtr<FRHIGPUTextureReadback> Readback;
    FString Filename;
    FIntPoint Size;
    EPixelFormat Format=PF_Unknown;
};
static TArray<FSlot> Slots;
static TArray<TFuture<bool>> Writes;
static FString Pending;
static FDelegateHandle Handle;
static int32 Requested=0,Submitted=0,Dropped=0;
static IImageWrapperModule* Images=nullptr;
static void Harvest()
{
    for(int32 Index=Slots.Num()-1;Index>=0;--Index)
    {
        auto& S=Slots[Index];if(!S.Readback->IsReady())continue;
        int32 Pitch=0;const void* Data=S.Readback->Lock(Pitch);
        TArray64<uint8> Pixels;Pixels.SetNumUninitialized(int64(S.Size.X)*S.Size.Y*4);
        for(int32 Y=0;Y<S.Size.Y;++Y)FMemory::Memcpy(Pixels.GetData()+int64(Y)*S.Size.X*4,static_cast<const uint8*>(Data)+int64(Y)*Pitch*4,int64(S.Size.X)*4);
        S.Readback->Unlock();const FString Path=S.Filename;const FIntPoint Size=S.Size;const EPixelFormat Format=S.Format;
        Writes.Add(Async(EAsyncExecution::ThreadPool,[Path,Size,Format,Pixels=MoveTemp(Pixels)]() mutable
        {
            if(Format==PF_A2B10G10R10)
            {
                auto* Packed=reinterpret_cast<uint32*>(Pixels.GetData());
                for(int64 I=0;I<int64(Size.X)*Size.Y;++I){const uint32 V=Packed[I];Packed[I]=0xff000000u|(((V&1023u)*255u/1023u)<<16)|((((V>>10)&1023u)*255u/1023u)<<8)|(((V>>20)&1023u)*255u/1023u);}
            }
            auto Image=Images->CreateImageWrapper(EImageFormat::JPEG);
            if(!Image->SetRaw(Pixels.GetData(),Pixels.Num(),Size.X,Size.Y,ERGBFormat::BGRA,8))return false;
            return FFileHelper::SaveArrayToFile(Image->GetCompressed(92),*Path);
        }));
        Slots.RemoveAt(Index);
    }
}
void Request(const FString& Filename)
{
    if(!Handle.IsValid())
    {
        Images=&FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        Handle=FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddLambda([](SWindow&,ISlateViewportProvider& Viewport)
        {
            const FString Path=MoveTemp(Pending);Pending.Empty();
            FRHITexture* Texture=Viewport.GetBackBufferResource();if(!Texture)return;
            const FIntPoint Size(Texture->GetSizeX(),Texture->GetSizeY());
            auto& Cmd=FRHICommandListExecutor::GetImmediateCommandList();
                Harvest();if(Path.IsEmpty())return;
                if(Slots.Num()>=8||(Texture->GetFormat()!=PF_B8G8R8A8&&Texture->GetFormat()!=PF_A2B10G10R10)){++Dropped;UE_LOG(LogTemp,Warning,TEXT("ASYNC_CAPTURE_DROP format=%d slots=%d"),int32(Texture->GetFormat()),Slots.Num());return;}
                FSlot S;S.Readback=MakeUnique<FRHIGPUTextureReadback>(TEXT("Helios motion QA"));S.Filename=Path;S.Size=Size;S.Format=Texture->GetFormat();
                S.Readback->EnqueueCopy(Cmd,Texture,FResolveRect(0,0,Size.X,Size.Y));Slots.Add(MoveTemp(S));++Submitted;
        });
    }
    ENQUEUE_RENDER_COMMAND(HeliosAsyncQARequest)([Filename](FRHICommandListImmediate&){Pending=Filename;});
    ++Requested;UE_LOG(LogTemp,Display,TEXT("ASYNC_CAPTURE_REQUEST file=%s"),*Filename);
}
void Finish(const FString& Directory)
{
    if(!Handle.IsValid())return;
    FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(Handle);Handle.Reset();
    // Synchronize only after the complete action sequence; no screenshot flush
    // or game/render-thread stall is introduced into the movement being observed.
    ENQUEUE_RENDER_COMMAND(HeliosAsyncQADrain)([](FRHICommandListImmediate& Cmd){Cmd.SubmitAndBlockUntilGPUIdle();Harvest();});
    FlushRenderingCommands();int32 Saved=0;for(auto& Write:Writes)if(Write.Get())++Saved;
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"requested\":%d,\"submitted\":%d,\"saved\":%d,\"dropped\":%d,\"synchronized_during_sequence\":false}"),Requested,Submitted,Saved,Dropped),*(Directory/TEXT("async_capture.json")));
    Writes.Reset();Slots.Reset();
}
}
