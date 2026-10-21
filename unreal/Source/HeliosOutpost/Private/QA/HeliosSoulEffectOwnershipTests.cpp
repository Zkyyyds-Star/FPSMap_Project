#if WITH_DEV_AUTOMATION_TESTS
#include "Weapons/HeliosSoulBanner.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeliosSoulEffectOwnershipTest,"Helios.Loadout.SoulEffectIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHeliosSoulEffectOwnershipTest::RunTest(const FString&)
{
    FTestWorldWrapper Fixture;
    if(!TestTrue(TEXT("Create isolated gameplay world"),Fixture.CreateTestWorld(EWorldType::Game)))return false;
    UWorld* World=Fixture.GetTestWorld();World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
    if(!TestTrue(TEXT("Start gameplay world"),Fixture.BeginPlayInTestWorld()))return false;
    TArray<APawn*> Holders;TArray<UHeliosSoulArmy*> Armies;
    TArray<AHeliosSoulEffect*> Trails;TArray<UNiagaraComponent*> Bursts;
    for(int32 I=0;I<5;++I)
    {
        FActorSpawnParameters Params;Params.Owner=I?Holders[0]:nullptr;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Holder=World->SpawnActor<APawn>(FVector(I*50,0,0),FRotator::ZeroRotator,Params);
        if(!TestNotNull(TEXT("Player and four companion holders"),Holder))return false;
        Holder->Tags.Add(I?FName(TEXT("HeliosPet")):FName(TEXT("Player")));
        Holders.Add(Holder);Armies.Add(UHeliosSoulArmy::Get(Holder));
        // A summon is owned by a companion that is itself owned by the player.
        // Source resolution must stop at that companion's own army.
        Params.Owner=Holder;auto* Summon=World->SpawnActor<APawn>(Params);
        if(!TestNotNull(TEXT("Summoned pawn"),Summon))return false;Summon->Tags.Add(TEXT("HeliosSummon"));
        Params.Owner=Summon;auto* Source=World->SpawnActor<AActor>(Params);
        if(!TestNotNull(TEXT("Effect source owned by a summoned pawn"),Source))return false;
        Trails.Add(AHeliosSoulEffect::Trail(Source));
        Bursts.Add(AHeliosSoulEffect::Burst(Source,FVector(I*50,0,0),0));
        if(!TestNotNull(TEXT("Original soul ribbon"),Trails.Last())||!TestNotNull(TEXT("Original pooled soul burst"),Bursts.Last()))return false;
        if(!TestTrue(TEXT("Burst initially active"),Bursts.Last()->IsActive()))return false;
    }
    // A Niagara pool can give an old component to a different holder. Its old
    // weak reference must no longer authorize the original army to stop it.
    Armies[3]->TrackBurst(Bursts[0]);
    Armies[0]->Clear();
    TestTrue(TEXT("Player ribbon is removed"),Trails[0]->IsActorBeingDestroyed());
    TestTrue(TEXT("Reassigned pooled burst survives the previous owner's clear"),Bursts[0]->IsActive());
    for(int32 I=1;I<5;++I)
    {
        TestFalse(TEXT("Player clear preserves every companion ribbon"),Trails[I]->IsActorBeingDestroyed());
        TestTrue(TEXT("Player clear preserves every companion burst"),Bursts[I]->IsActive());
    }
    Armies[1]->Clear();Armies[1]->Clear();
    TestTrue(TEXT("Companion clear removes its own ribbon"),Trails[1]->IsActorBeingDestroyed());
    TestFalse(TEXT("Companion clear stops its own burst"),Bursts[1]->IsActive());
    for(int32 I=2;I<5;++I)
    {
        TestFalse(TEXT("Repeated dead-companion clear preserves another ribbon"),Trails[I]->IsActorBeingDestroyed());
        TestTrue(TEXT("Repeated dead-companion clear preserves another burst"),Bursts[I]->IsActive());
    }
    Armies[3]->Clear();
    TestFalse(TEXT("New pool lease owner can stop the reused burst"),Bursts[0]->IsActive());
    TestTrue(TEXT("New pool lease owner removes its own ribbon"),Trails[3]->IsActorBeingDestroyed());
    TestFalse(TEXT("New pool lease owner stops its own burst"),Bursts[3]->IsActive());
    TestTrue(TEXT("Fourth companion remains active"),Bursts[4]->IsActive());
    return true;
}
#endif
