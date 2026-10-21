#include "Weapons/HeliosWeaponVFX.h"

#include "Engine/Engine.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

TMap<TWeakObjectPtr<UWorld>, TArray<TWeakObjectPtr<UNiagaraComponent>>> UHeliosWeaponVFX::TrackedByWorld;
TMap<TWeakObjectPtr<UNiagaraComponent>, FTimerHandle> UHeliosWeaponVFX::ReleaseTimers;

UNiagaraComponent* UHeliosWeaponVFX::SpawnOneShot(
    const UObject* WorldContextObject,
    UNiagaraSystem* System,
    const FVector& Location,
    const FRotator& Rotation,
    const FVector& Scale,
    float MaxLifetimeSeconds)
{
    if (!WorldContextObject || !System)
    {
        return nullptr;
    }

    const bool bTimedRelease = MaxLifetimeSeconds > 0.0f;
    UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        WorldContextObject,
        System,
        Location,
        Rotation,
        Scale,
        !bTimedRelease,
        true,
        bTimedRelease ? ENCPoolMethod::ManualRelease : ENCPoolMethod::AutoRelease,
        true);

    if (Component && bTimedRelease)
    {
        Track(Component);
        FTimerHandle& Timer = ReleaseTimers.FindOrAdd(Component);
        TWeakObjectPtr<UNiagaraComponent> WeakComponent(Component);
        Component->GetWorld()->GetTimerManager().SetTimer(
            Timer,
            FTimerDelegate::CreateLambda([WeakComponent]()
            {
                if (UNiagaraComponent* TimedComponent = WeakComponent.Get())
                {
                    UHeliosWeaponVFX::ReleaseTracked(TimedComponent);
                }
            }),
            MaxLifetimeSeconds,
            false);
    }
    return Component;
}

UNiagaraComponent* UHeliosWeaponVFX::SpawnAttachedTrail(
    UNiagaraSystem* System,
    USceneComponent* AttachTo,
    FName SocketName,
    const FVector& RelativeLocation,
    const FRotator& RelativeRotation)
{
    if (!System || !IsValid(AttachTo) || !AttachTo->GetWorld())
    {
        return nullptr;
    }

    UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
        System,
        AttachTo,
        SocketName,
        RelativeLocation,
        RelativeRotation,
        EAttachLocation::KeepRelativeOffset,
        false,
        true,
        ENCPoolMethod::ManualRelease,
        true);

    if (Component)
    {
        Track(Component);
    }
    return Component;
}

void UHeliosWeaponVFX::SetTracerEndpoints(UNiagaraComponent* Component, const FVector& Start, const FVector& End)
{
    if (!IsValid(Component))
    {
        return;
    }
    Component->SetVariablePosition(TEXT("User.Start"), Start);
    Component->SetVariablePosition(TEXT("User.End"), End);
    const FVector Delta = End - Start;
    const float Distance = Delta.Size();
    if (Distance > UE_KINDA_SMALL_NUMBER)
    {
        // The authored tracer is a stationary 100 cm local-X sprite. Stretch
        // and center it so the visible streak terminates exactly at Start/End.
        Component->SetWorldLocation((Start + End) * 0.5f);
        Component->SetWorldRotation(Delta.Rotation());
        Component->SetWorldScale3D(FVector(FMath::Max(Distance / 100.0f, 0.01f), 1.0f, 1.0f));
    }
}

void UHeliosWeaponVFX::ReleaseTracked(UNiagaraComponent* Component)
{
    if (!IsValid(Component))
    {
        return;
    }

    UWorld* World = Component->GetWorld();
    bool bWasTracked = ReleaseTimers.Contains(Component);
    if (FTimerHandle* Timer = ReleaseTimers.Find(Component))
    {
        if (World)
        {
            World->GetTimerManager().ClearTimer(*Timer);
        }
        ReleaseTimers.Remove(Component);
    }
    if (TArray<TWeakObjectPtr<UNiagaraComponent>>* Components = TrackedByWorld.Find(World))
    {
        bWasTracked |= Components->RemoveAll([Component](const TWeakObjectPtr<UNiagaraComponent>& Entry)
        {
            return !Entry.IsValid() || Entry.Get() == Component;
        }) > 0;
        if (Components->IsEmpty())
        {
            TrackedByWorld.Remove(World);
        }
    }

    // A pooled component may already have been released by ClearWorld. Never
    // release it twice: the pool may have handed the same object to another FX.
    if (!bWasTracked)
    {
        return;
    }

    Component->DeactivateImmediate();
    Component->ReleaseToPool();
}

void UHeliosWeaponVFX::ClearWorld(const UObject* WorldContextObject)
{
    if (!WorldContextObject || !GEngine)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    TArray<TWeakObjectPtr<UNiagaraComponent>> Components;
    if (!World || !TrackedByWorld.RemoveAndCopyValue(World, Components))
    {
        return;
    }

    for (const TWeakObjectPtr<UNiagaraComponent>& Entry : Components)
    {
        if (UNiagaraComponent* Component = Entry.Get())
        {
            if (FTimerHandle* Timer = ReleaseTimers.Find(Component))
            {
                World->GetTimerManager().ClearTimer(*Timer);
            }
            Component->DeactivateImmediate();
            Component->ReleaseToPool();
            ReleaseTimers.Remove(Component);
        }
    }
}

int32 UHeliosWeaponVFX::GetTrackedCount(const UObject* WorldContextObject)
{
    if (!WorldContextObject || !GEngine)
    {
        return 0;
    }
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World)
    {
        return 0;
    }
    Compact(World);
    const TArray<TWeakObjectPtr<UNiagaraComponent>>* Components = TrackedByWorld.Find(World);
    return Components ? Components->Num() : 0;
}

void UHeliosWeaponVFX::Track(UNiagaraComponent* Component)
{
    if (!IsValid(Component) || !Component->GetWorld())
    {
        return;
    }
    UWorld* World = Component->GetWorld();
    Compact(World);
    TrackedByWorld.FindOrAdd(World).AddUnique(Component);
}

void UHeliosWeaponVFX::Compact(UWorld* World)
{
    for (auto It = ReleaseTimers.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
        }
    }
    for (auto It = TrackedByWorld.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
            continue;
        }
        It.Value().RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Entry)
        {
            return !Entry.IsValid();
        });
    }

    if (TArray<TWeakObjectPtr<UNiagaraComponent>>* Components = TrackedByWorld.Find(World); Components && Components->IsEmpty())
    {
        TrackedByWorld.Remove(World);
    }
}
