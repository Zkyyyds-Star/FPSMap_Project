#include "UI/HeliosEnemyHUD.h"
#include "Player/HeliosArenaCharacter.h"
#include "Player/HeliosQingYaAbilityComponent.h"
#include "Enemies/HeliosEnemyTestDirector.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

void AHeliosEnemyHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner) return;
    const AHeliosArenaCharacter* Player=Cast<AHeliosArenaCharacter>(PlayerOwner->GetPawn());
    if (!Player) return;
    UFont* Font=GEngine?GEngine->GetMediumFont():nullptr;
    const float W=Canvas->ClipX,H=Canvas->ClipY;
    const FLinearColor Cyan(.24f,.86f,1.f),White(.91f,.94f,.98f),Dim(.57f,.66f,.74f);
    DrawRect(FLinearColor(.015f,.025f,.04f,.83f),20,18,480,112);
    DrawText(TEXT("HELIOS / ENEMY TESTBED"),Cyan,36,28,Font,1.3f,false);
    int32 Mode=5,Alive=0;
    for (TActorIterator<AHeliosEnemyTestDirector> It(GetWorld());It;++It)
    {Mode=It->GetSelectedMode();Alive=It->GetAliveEnemyCount();break;}
    const FString Label=Mode==5?TEXT("MIXED / ALL FOUR"):FString::Printf(TEXT("SINGLE / E%02d"),Mode);
    DrawText(FString::Printf(TEXT("%s     ENEMIES %d"),*Label,Alive),White,36,67,Font,1.f,false);
    DrawText(TEXT("1-4 SELECT TYPE    5 MIXED    F5 RESET"),Dim,36,99,Font,.85f,false);
    DrawRect(FLinearColor(.015f,.025f,.04f,.85f),20,H-140,470,80);
    const float HealthFraction = FMath::Clamp(Player->GetHealth()/Player->GetMaxHealth(),0.f,1.f);
    const FLinearColor HPColor=HealthFraction<.3f?FLinearColor(1.f,.26f,.2f):Cyan;
    DrawText(FString::Printf(TEXT("HEALTH %03.0f"),Player->GetHealth()),HPColor,36,H-130,Font,1.6f,false);
    DrawText(Player->IsReloading()?TEXT("RELOADING..."):FString::Printf(TEXT("AMMO %02d / 30"),Player->GetAmmo()),White,250,H-130,Font,1.3f,false);
    DrawRect(FLinearColor(.13f,.19f,.23f),36,H-83,430,7);
    DrawRect(HPColor,36,H-83,430*HealthFraction,7);
    DrawText(TEXT("WASD MOVE   MOUSE LOOK   SHIFT SPRINT   SPACE JUMP   LMB FIRE   R RELOAD   ESC EXIT"),Dim,28,H-38,Font,.9f,false);
    if(Player->GetQingYaAbility()->IsSelected())
    {
        DrawRect(FLinearColor(.015f,.025f,.04f,.83f),W-520,18,500,75);
        DrawText(FString::Printf(TEXT("QINGYA / %s"),*Player->GetLoadout().CharacterVariantId.ToString()),Cyan,W-504,30,Font,1.1f,false);
        DrawText(TEXT("H FORM   J LOVE   K GUN GESTURE   T KICK"),White,W-504,64,Font,.85f,false);
    }
    if (Player->IsDead())
    {
        DrawRect(FLinearColor(.07f,.005f,.008f,.45f),0,0,W,H);
        DrawText(TEXT("DEFEATED  /  RETURNING TO LANDING PAD"),White,W*.5f-250,H*.46f,Font,1.3f,false);
    }
    else
    {
        DrawRect(Cyan,W*.5f-1,H*.5f-8,2,5);DrawRect(Cyan,W*.5f-1,H*.5f+3,2,5);
        DrawRect(Cyan,W*.5f-8,H*.5f-1,5,2);DrawRect(Cyan,W*.5f+3,H*.5f-1,5,2);
    }
}
